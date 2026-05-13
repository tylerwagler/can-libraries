/**
 * @file socketcan_backend.cpp
 * @brief Linux SocketCAN backend implementation
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "socketcan_backend.h"

#include "can/can_types.h"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

namespace can {

namespace {

/// Read the entire contents of a sysfs/file path, trimming trailing whitespace.
/// Returns empty string on failure.
std::string readSysfs(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

/// Resolve a relative-or-absolute path to its real (canonical) form.
std::string realpathOf(const std::string& p) {
    char buf[PATH_MAX];
    if (realpath(p.c_str(), buf) == nullptr) return {};
    return std::string(buf);
}

/// Parse a "x.y.z" or "x.y" version triple from any string. Returns empty
/// if no version-like token is found. Used to recover firmware version from
/// PCAN-USB's product string ("PCAN-USB (8.7.0)" -> "8.7.0").
std::string extractVersion(const std::string& s) {
    static const std::regex re(R"((\d+\.\d+(?:\.\d+)?))");
    std::smatch m;
    if (std::regex_search(s, m, re)) return m[1].str();
    return {};
}

/// Call SIOCETHTOOL with ETHTOOL_GDRVINFO to retrieve driver name, kernel
/// driver version, firmware version, and bus path.
bool ethtoolDrvInfo(const std::string& iface, ethtool_drvinfo& out) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    std::memset(&out, 0, sizeof(out));
    out.cmd = ETHTOOL_GDRVINFO;
    ifr.ifr_data = reinterpret_cast<char*>(&out);
    int rc = ::ioctl(fd, SIOCETHTOOL, &ifr);
    ::close(fd);
    return rc == 0;
}

/// Walk up from /sys/class/net/<iface>/device until we find a directory
/// containing the standard USB device descriptor files (idVendor/idProduct).
/// Returns the path to that directory, or empty on failure (non-USB CAN
/// interfaces — virtual can, PCI, native — return empty and we just skip
/// the USB-specific fields).
std::string findUsbDeviceDir(const std::string& iface) {
    std::string p = realpathOf("/sys/class/net/" + iface + "/device");
    if (p.empty()) return {};

    // Walk up at most ~8 levels looking for idVendor.
    for (int i = 0; i < 8 && !p.empty() && p != "/"; ++i) {
        std::ifstream test(p + "/idVendor");
        if (test.is_open()) return p;
        auto slash = p.find_last_of('/');
        if (slash == std::string::npos) break;
        p = p.substr(0, slash);
    }
    return {};
}

/// Populate USB-specific fields of an AdapterInfo from a USB sysfs dir.
void fillFromUsbDir(AdapterInfo& info, const std::string& usb_dir) {
    if (usb_dir.empty()) return;

    std::string product      = readSysfs(usb_dir + "/product");
    std::string manufacturer = readSysfs(usb_dir + "/manufacturer");
    std::string serial       = readSysfs(usb_dir + "/serial");
    std::string id_vendor    = readSysfs(usb_dir + "/idVendor");
    std::string id_product   = readSysfs(usb_dir + "/idProduct");
    std::string bcd_device   = readSysfs(usb_dir + "/bcdDevice");

    if (!product.empty())      info.device_name = product;
    if (!manufacturer.empty()) info.extra["usb_manufacturer"] = manufacturer;
    if (!serial.empty())       info.serial_number = serial;
    if (!id_vendor.empty() && !id_product.empty()) {
        info.extra["usb_vid_pid"] = id_vendor + ":" + id_product;
    }
    if (!bcd_device.empty()) info.extra["usb_bcd_device"] = bcd_device;

    // Many older PCAN-USB units don't expose iSerialNumber via the descriptor
    // and instead embed firmware version in the product string. Recover it.
    if (info.firmware_version.empty()) {
        std::string ver = extractVersion(product);
        if (!ver.empty()) info.firmware_version = ver;
    }
}

/// Monotonic timestamp used for TX-echo deque deadlines and as a fallback
/// when the kernel doesn't deliver SCM_TIMESTAMPNS. Microseconds.
uint64_t nowMicros() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

/// FNV-1a 64-bit hash of a payload. Used (with id + dlc + fd flag) as the
/// match key for TX-echo detection. Collisions are tolerable: the worst
/// case is a single misattributed direction flag on a frame the app sent
/// the exact same payload for within the echo window.
uint64_t fnv1aHash(const uint8_t* data, std::size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/// Build a fully-populated AdapterInfo for a SocketCAN interface name.
AdapterInfo buildSocketCanInfo(const std::string& iface) {
    AdapterInfo a;
    a.backend = BackendKind::SocketCan;
    a.channel_id = iface;
    a.device_name = iface;       // overwritten below if we can do better
    a.driver_version = "Linux SocketCAN";

    ethtool_drvinfo drv{};
    if (ethtoolDrvInfo(iface, drv)) {
        if (drv.driver[0])     a.extra["kernel_driver"] = drv.driver;
        if (drv.version[0])    a.driver_version = drv.version;
        if (drv.fw_version[0]) a.firmware_version = drv.fw_version;
        if (drv.bus_info[0])   a.extra["bus_path"] = drv.bus_info;
    }

    fillFromUsbDir(a, findUsbDeviceDir(iface));
    return a;
}

} // anonymous namespace

SocketCanBackend::SocketCanBackend() = default;

SocketCanBackend::~SocketCanBackend() {
    SocketCanBackend::close();
}

BackendCapabilities SocketCanBackend::capabilities() const {
    BackendCapabilities caps;
    caps.supports_can_fd = true;       // SocketCAN supports FD if the kernel/iface does
    caps.supports_listen_only = true;  // via CAN_CTRLMODE_LISTENONLY (set externally with `ip link`)
    caps.supports_loopback = true;
    caps.supports_receive_own = true;
    caps.supports_acceptance_filters = true;
    caps.exposes_error_counters = true;     // via error frames
    caps.exposes_bus_load = false;          // computed by higher layer
    caps.exposes_firmware_version = false;
    caps.exposes_serial_number = false;
    caps.exposes_receive_fd = true;
    return caps;
}

std::vector<AdapterInfo> SocketCanBackend::enumerateAdapters() {
    std::vector<AdapterInfo> adapters;

    DIR* d = opendir("/sys/class/net");
    if (!d) return adapters;

    // Match common CAN interface naming conventions: kernel native (can*),
    // virtual CAN (vcan*), and serial-line CAN (slcan*). Anything more
    // exotic the user can still pass directly to open() — this filter
    // only governs *enumeration*.
    auto starts_with = [](const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    };
    while (auto* entry = readdir(d)) {
        const std::string name = entry->d_name;
        if (!starts_with(name, "can") &&
            !starts_with(name, "vcan") &&
            !starts_with(name, "slcan")) continue;
        adapters.push_back(buildSocketCanInfo(name));
    }
    closedir(d);

    // Stable order so UI dropdowns don't shuffle.
    std::sort(adapters.begin(), adapters.end(),
              [](const AdapterInfo& a, const AdapterInfo& b) {
                  return a.channel_id < b.channel_id;
              });
    return adapters;
}

bool SocketCanBackend::open(const ChannelConfig& cfg) {
    // open() is a lifecycle method — by contract callers serialize it
    // against close() and the operational methods, so a plain load is fine.
    if (socket_fd_.load(std::memory_order_acquire) >= 0) {
        recordError("Backend already open");
        return false;
    }
    if (cfg.channel_id.empty()) {
        recordError("Empty channel_id");
        return false;
    }
    // SocketCAN interface names live in ifreq::ifr_name, which is
    // IFNAMSIZ bytes including the trailing NUL. Without this check
    // strncpy() below silently truncates a too-long name, and we'd
    // either fail to find the interface (best case) or end up bound
    // to a *different* interface that happens to be a prefix.
    if (cfg.channel_id.size() >= IFNAMSIZ) {
        recordError("channel_id too long (max " + std::to_string(IFNAMSIZ - 1)
                    + " chars): " + cfg.channel_id);
        return false;
    }

    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        recordError(std::string("socket() failed: ") + std::strerror(errno));
        return false;
    }

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, cfg.channel_id.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        recordError("Interface not found: " + cfg.channel_id);
        ::close(fd);
        return false;
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        recordError(std::string("bind() failed: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }

    // Best-effort: enable error frame reception so we can populate
    // bus_state and error counters in status(). On a kernel that
    // doesn't support this filter, the call fails; status() will just
    // continue to report BusState::Unknown. Not fatal to open().
    can_err_mask_t err_mask = CAN_ERR_MASK;
    (void)::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    // Receive own messages: only opt in if the caller explicitly asked.
    // The default kernel state is "off", so skipping the syscall when
    // disabled avoids probe-failing on old kernels that don't recognize
    // the option. When the caller requested it and the kernel can't
    // honor it, fail open() rather than silently dropping is_tx echo
    // detection — the prior code would have left frame.is_tx stuck at
    // false with no signal to the caller.
    if (cfg.receive_own_messages) {
        int recv_own = 1;
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                         &recv_own, sizeof(recv_own)) < 0) {
            recordError(std::string("CAN_RAW_RECV_OWN_MSGS not supported: ")
                        + std::strerror(errno));
            ::close(fd);
            return false;
        }
    }

    // Request nanosecond-resolution kernel timestamps; we extract them
    // from the recvmsg cmsg buffer and populate Frame::timestamp_us.
    // Best-effort: if the kernel rejects it, the receive() path falls
    // back to a system_clock host-side timestamp.
    int ts_on = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS, &ts_on, sizeof(ts_on));

    // Opt the socket into receiving CAN-FD frames. Old kernels or
    // non-FD-capable interfaces return an error; that's OK — the socket
    // falls back to classic-only behavior.
    int fd_on = 1;
    fd_enabled_ = ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &fd_on, sizeof(fd_on)) == 0;

    // CAN_RAW_LOOPBACK is left at the kernel default (on). That makes TX'd
    // frames visible to other sockets on the same interface, which is what
    // monitoring/sniffer tools rely on. We only override if the caller
    // explicitly disables it via a future config flag.

    // Sideband eventfd: receive() selects on both the CAN socket and this
    // fd, so close() can wake a blocked worker by writing to it. Without
    // this an in-flight receive() would wait out its full timeout before
    // a graceful shutdown can proceed (shutdown(2) is a no-op on
    // AF_CAN SOCK_RAW, so it can't be used for the wakeup).
    int evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd < 0) {
        recordError(std::string("eventfd() failed: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }
    shutdown_eventfd_.store(evfd, std::memory_order_release);

    // Release-store so that operational threads observing socket_fd_ >= 0
    // are guaranteed to see the shutdown_eventfd_ / config_ / info_ writes
    // that precede it.
    socket_fd_.store(fd, std::memory_order_release);
    config_ = cfg;

    info_ = buildSocketCanInfo(cfg.channel_id);

    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        bus_state_ = BusState::ErrorActive;  // assume good after open
    }
    tx_err_counter_ = 0;
    rx_err_counter_ = 0;
    bus_errors_ = 0;
    rx_overruns_ = 0;

    return true;
}

void SocketCanBackend::close() {
    // Race shape we have to handle: receive() can be running on another
    // thread, mid-select(), when close() is invoked. The eventfd is the
    // wakeup mechanism; the atomic exchanges fence out new entrants and
    // make the "close raced me" check in receive() reliable.
    //
    // Ordering:
    //   1. Exchange shutdown_eventfd_ to -1 first. Any worker that has
    //      NOT yet loaded shutdown_eventfd_ will now see -1 and bail out
    //      of receive() before it ever consults the CAN fd.
    //   2. Write to the *snapshotted* old eventfd to wake any worker
    //      that loaded it BEFORE step 1 — they're sitting in select(),
    //      this is what wakes them.
    //   3. Exchange socket_fd_ to -1 so any racing send/receive that
    //      sneaks in here-on gets a fast -1 and bails.
    //   4. Close the kernel fds.
    // Steps 1 & 3 use exchange so close() is idempotent: a second call
    // gets back -1 and skips the syscalls.
    int evfd = shutdown_eventfd_.exchange(-1, std::memory_order_acq_rel);
    if (evfd >= 0) {
        const uint64_t one = 1;
        ssize_t w = ::write(evfd, &one, sizeof(one));
        (void)w;
    }
    int fd = socket_fd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) ::close(fd);
    if (evfd >= 0) ::close(evfd);

    fd_enabled_ = false;
    {
        std::lock_guard<std::mutex> lock(recent_tx_mutex_);
        recent_tx_.clear();
    }
    std::lock_guard<std::mutex> lock(status_mutex_);
    bus_state_ = BusState::Unknown;
}

bool SocketCanBackend::send(const Frame& frame) {
    // Load once. If close() races us, the fd we cached may already be
    // gone — kernel returns EBADF and we surface that as a send failure.
    const int fd = socket_fd_.load(std::memory_order_acquire);
    if (fd < 0) {
        recordError("send() on closed backend");
        return false;
    }

    ssize_t n;
    if (frame.is_fd_frame) {
        if (!fd_enabled_) {
            recordError("send: CAN-FD frame requested but socket is not FD-enabled");
            return false;
        }
        canfd_frame raw{};
        raw.can_id = frame.id;
        if (frame.is_extended_id) raw.can_id |= CAN_EFF_FLAG;
        // RTR is not valid for CAN-FD; ignore the flag if set.
        raw.len = frame.dlc > MAX_CAN_FD_DLC ? static_cast<uint8_t>(MAX_CAN_FD_DLC) : frame.dlc;
        raw.flags = 0;
        if (frame.is_brs) raw.flags |= CANFD_BRS;
        if (frame.is_esi) raw.flags |= CANFD_ESI;
        std::memcpy(raw.data, frame.data.data(), raw.len);

        n = ::write(fd, &raw, sizeof(raw));
        if (n != static_cast<ssize_t>(sizeof(raw))) {
            recordError(std::string("write(canfd_frame) failed: ") + std::strerror(errno));
            return false;
        }
    } else {
        can_frame raw{};
        raw.can_id = frame.id;
        if (frame.is_extended_id) raw.can_id |= CAN_EFF_FLAG;
        if (frame.is_remote_frame) raw.can_id |= CAN_RTR_FLAG;
        raw.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
        std::memcpy(raw.data, frame.data.data(), raw.can_dlc);

        n = ::write(fd, &raw, sizeof(raw));
        if (n != static_cast<ssize_t>(sizeof(raw))) {
            recordError(std::string("write() failed: ") + std::strerror(errno));
            return false;
        }
    }

    // Record the send so the matching kernel echo (delivered when
    // CAN_RAW_RECV_OWN_MSGS is on) can be flagged as TX on the way back.
    if (config_.receive_own_messages) {
        pushRecentTx(frame);
    }
    return true;
}

void SocketCanBackend::pushRecentTx(const Frame& frame) {
    // Echoes typically arrive within sub-millisecond; 200ms is a generous
    // window that still bounds memory. The deque is also capped by entry
    // count below, so a flood of unmatched TX (e.g. RECV_OWN was turned
    // off after open()) can't grow it unbounded.
    constexpr uint64_t kEchoWindowUs = 200'000;
    constexpr std::size_t kMaxEntries = 256;

    const uint64_t now = nowMicros();
    RecentTx entry;
    entry.id = frame.id;
    entry.dlc = frame.dlc;
    entry.is_fd = frame.is_fd_frame;
    entry.payload_hash = fnv1aHash(frame.data.data(), frame.dlc);
    entry.deadline_us = now + kEchoWindowUs;

    std::lock_guard<std::mutex> lock(recent_tx_mutex_);
    // Drop expired entries from the front (deque is roughly time-ordered
    // since deadlines all use the same offset).
    while (!recent_tx_.empty() && recent_tx_.front().deadline_us < now) {
        recent_tx_.pop_front();
    }
    if (recent_tx_.size() >= kMaxEntries) {
        recent_tx_.pop_front();
    }
    recent_tx_.push_back(entry);
}

bool SocketCanBackend::consumeRecentTx(const Frame& frame) {
    const uint64_t now = nowMicros();
    const uint64_t h = fnv1aHash(frame.data.data(), frame.dlc);

    std::lock_guard<std::mutex> lock(recent_tx_mutex_);
    for (auto it = recent_tx_.begin(); it != recent_tx_.end(); ++it) {
        if (it->deadline_us < now) continue;        // expired; let next prune sweep it
        if (it->id != frame.id) continue;
        if (it->dlc != frame.dlc) continue;
        if (it->is_fd != frame.is_fd_frame) continue;
        if (it->payload_hash != h) continue;
        recent_tx_.erase(it);
        return true;
    }
    return false;
}

bool SocketCanBackend::receive(Frame& frame, std::chrono::milliseconds timeout) {
    // Load both fds before issuing syscalls. close() invalidates them
    // in this order: shutdown_eventfd_ first, then socket_fd_ — so if
    // we observe shutdown_eventfd_ < 0 below, close() is mid-flight and
    // we bail without touching either kernel fd. Conversely, if we
    // observe a valid eventfd, any concurrent close() will signal it
    // (the signal targets the value we snapshotted) and our select()
    // returns promptly.
    const int fd = socket_fd_.load(std::memory_order_acquire);
    if (fd < 0) return false;
    const int evfd = shutdown_eventfd_.load(std::memory_order_acquire);
    if (evfd < 0) return false;

    // Always select() — including with timeout=0ms, which is the standard "non-blocking poll"
    // semantics callers expect. The previous optimization that skipped select() when
    // timeout==0 turned the read() below into a *blocking* call (default socket mode), which
    // hangs a polling caller indefinitely on an idle bus and breaks clean thread shutdown.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    if (evfd >= 0) FD_SET(evfd, &rfds);

    timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;

    const int nfds = (evfd > fd ? evfd : fd) + 1;
    int sel = ::select(nfds, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) return false; // timeout (incl. 0ms with nothing pending) or error

    // close() signalled us — exit promptly without touching the CAN
    // socket (which may already be torn down).
    if (evfd >= 0 && FD_ISSET(evfd, &rfds)) return false;

    // recvmsg lets us pick up the kernel's SO_TIMESTAMPNS cmsg alongside the
    // frame payload. The iov buffer is sized for canfd_frame (72 B), which
    // is a superset of can_frame (16 B); we dispatch on the actual byte
    // count returned.
    union {
        struct can_frame   classic;
        struct canfd_frame fd;
        uint8_t            bytes[sizeof(struct canfd_frame)];
    } buf;

    iovec iov{};
    iov.iov_base = buf.bytes;
    iov.iov_len  = sizeof(buf);

    alignas(struct cmsghdr) uint8_t cmsg_buf[CMSG_SPACE(sizeof(struct timespec))];

    msghdr msg{};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t n = ::recvmsg(fd, &msg, 0);
    if (n <= 0) {
        // n == 0 happens when close()/shutdown() races us between select()
        // returning readable and recvmsg() running — graceful exit, not an
        // error. n < 0 with EAGAIN/EWOULDBLOCK is the same: stale wakeup.
        // EBADF means close() already swapped the fd out; also benign.
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EBADF) {
            recordError(std::string("recvmsg() failed: ") + std::strerror(errno));
        }
        return false;
    }

    // Extract the kernel timestamp (nanoseconds) from the cmsg buffer.
    uint64_t ts_us = 0;
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS) {
            struct timespec ts;
            std::memcpy(&ts, CMSG_DATA(c), sizeof(ts));
            ts_us = static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL
                  + static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
            break;
        }
    }
    if (ts_us == 0) {
        // No cmsg timestamp — driver doesn't support SO_TIMESTAMPNS, or
        // setsockopt was rejected at open() time. Synthesize a host-side
        // timestamp using system_clock so the epoch matches the kernel
        // path (SO_TIMESTAMPNS is CLOCK_REALTIME; system_clock on Linux
        // wraps the same clock). Using steady_clock here, as the prior
        // code did, would give Frame::timestamp_us two different epochs
        // depending on which path executed — surprising for consumers
        // that timestamp-correlate across frames.
        ts_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    // Reset all fields; older callers may reuse the frame across calls
    // and we don't want stale flags leaking through (e.g. is_brs from a
    // previous FD frame surviving onto a classic frame).
    frame = Frame{};
    frame.timestamp_us = ts_us;

    if (n == static_cast<ssize_t>(sizeof(struct canfd_frame))) {
        // CAN-FD frame.
        const auto& fd = buf.fd;
        const bool is_err = (fd.can_id & CAN_ERR_FLAG) != 0;
        const bool is_ext = (fd.can_id & CAN_EFF_FLAG) != 0;

        frame.id = fd.can_id & (is_ext ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.dlc = fd.len > MAX_CAN_FD_DLC ? static_cast<uint8_t>(MAX_CAN_FD_DLC) : fd.len;
        std::memcpy(frame.data.data(), fd.data, frame.dlc);
        frame.is_extended_id  = is_ext;
        frame.is_remote_frame = false;  // RTR is not defined for CAN-FD
        frame.is_error_frame  = is_err;
        frame.is_fd_frame     = true;
        frame.is_brs          = (fd.flags & CANFD_BRS) != 0;
        frame.is_esi          = (fd.flags & CANFD_ESI) != 0;

        if (is_err) {
            updateStateFromErrorFrame(fd.can_id, fd.data, frame.dlc);
            if (!config_.receive_error_frames) return false;
        }
    } else if (n == static_cast<ssize_t>(sizeof(struct can_frame))) {
        // Classic CAN frame.
        const auto& raw = buf.classic;
        const bool is_err = (raw.can_id & CAN_ERR_FLAG) != 0;
        const bool is_ext = (raw.can_id & CAN_EFF_FLAG) != 0;
        const bool is_rtr = (raw.can_id & CAN_RTR_FLAG) != 0;

        frame.id = raw.can_id & (is_ext ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.dlc = raw.can_dlc;
        std::memcpy(frame.data.data(), raw.data, raw.can_dlc);
        frame.is_extended_id  = is_ext;
        frame.is_remote_frame = is_rtr;
        frame.is_error_frame  = is_err;
        frame.is_fd_frame     = false;

        if (is_err) {
            updateStateFromErrorFrame(raw.can_id, raw.data, raw.can_dlc);
            if (!config_.receive_error_frames) return false;
        }
    } else {
        recordError("recvmsg returned unexpected size " + std::to_string(n));
        return false;
    }

    // If echo reception is on, try to attribute this frame to one of our
    // recent sends. Frames that don't match any recent TX stay is_tx=false.
    if (config_.receive_own_messages) {
        frame.is_tx = consumeRecentTx(frame);
    }

    return true;
}

ChannelStatus SocketCanBackend::status() const {
    ChannelStatus s;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        s.bus_state = bus_state_;
    }
    s.tx_error_counter = tx_err_counter_.load();
    s.rx_error_counter = rx_err_counter_.load();
    s.bus_errors = bus_errors_.load();
    s.rx_queue_overruns = rx_overruns_.load();
    s.bus_load_percent = -1.0; // not measured at backend level
    return s;
}

AdapterInfo SocketCanBackend::info() const {
    // info_ is populated in open() from buildSocketCanInfo() (which
    // reads sysfs + runs SIOCETHTOOL). Returning the cached copy avoids
    // a syscall storm on apps that poll info() alongside status(); the
    // firmware/driver strings don't change at runtime. Pre-open() calls
    // get the default-constructed info_ (mostly empty), which matches
    // the documented contract that info() is only well-defined after
    // open() returns.
    return info_;
}

std::string SocketCanBackend::lastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void SocketCanBackend::recordError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = msg;
}

void SocketCanBackend::updateStateFromErrorFrame(uint32_t can_id,
                                                 const uint8_t* data,
                                                 uint8_t dlc) {
    bus_errors_.fetch_add(1);

    // Per linux/can/error.h, error frames carry a bitmask of error classes
    // in can_id and details in data[].
    if (can_id & CAN_ERR_BUSOFF) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        bus_state_ = BusState::BusOff;
    } else if (can_id & CAN_ERR_RESTARTED) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        bus_state_ = BusState::ErrorActive;
    } else if (can_id & CAN_ERR_CRTL) {
        // Controller status — data[1] carries the new state.
        if (dlc >= 2) {
            uint8_t flags = data[1];
            std::lock_guard<std::mutex> lock(status_mutex_);
            if (flags & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) {
                bus_state_ = BusState::ErrorPassive;
            } else if (flags & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) {
                bus_state_ = BusState::ErrorWarning;
            } else if (flags & (CAN_ERR_CRTL_RX_OVERFLOW | CAN_ERR_CRTL_TX_OVERFLOW)) {
                rx_overruns_.fetch_add(1);
            }
        }
        if (dlc >= 8) {
            // data[6] = tx error counter, data[7] = rx error counter
            tx_err_counter_.store(data[6]);
            rx_err_counter_.store(data[7]);
        }
    }
}

// Factory function. Picked up via extern "C"-equivalent forward declaration
// in i_can_backend.cpp.
std::unique_ptr<ICanBackend> createSocketCanBackend() {
    return std::unique_ptr<ICanBackend>(new SocketCanBackend());
}

} // namespace can
