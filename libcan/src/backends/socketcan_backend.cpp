/**
 * @file socketcan_backend.cpp
 * @brief Linux SocketCAN backend implementation
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "socketcan_backend.h"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
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

    while (auto* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (name.rfind("can", 0) != 0 && name.rfind("vcan", 0) != 0) continue;
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
    if (socket_fd_ >= 0) {
        recordError("Backend already open");
        return false;
    }
    if (cfg.channel_id.empty()) {
        recordError("Empty channel_id");
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

    // Always enable error frame reception so we can populate bus_state and
    // error counters in status(). Caller can ignore them via the higher
    // layer if they don't want them surfaced.
    can_err_mask_t err_mask = CAN_ERR_MASK;
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    int recv_own = cfg.receive_own_messages ? 1 : 0;
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own));

    // CAN_RAW_LOOPBACK is left at the kernel default (on). That makes TX'd
    // frames visible to other sockets on the same interface, which is what
    // monitoring/sniffer tools rely on. We only override if the caller
    // explicitly disables it via a future config flag.

    socket_fd_ = fd;
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
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    std::lock_guard<std::mutex> lock(status_mutex_);
    bus_state_ = BusState::Unknown;
}

bool SocketCanBackend::send(const Frame& frame) {
    if (socket_fd_ < 0) {
        recordError("send() on closed backend");
        return false;
    }

    can_frame raw{};
    raw.can_id = frame.id;
    if (frame.is_extended_id) raw.can_id |= CAN_EFF_FLAG;
    if (frame.is_remote_frame) raw.can_id |= CAN_RTR_FLAG;
    raw.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
    std::memcpy(raw.data, frame.data, raw.can_dlc);

    ssize_t n = ::write(socket_fd_, &raw, sizeof(raw));
    if (n != static_cast<ssize_t>(sizeof(raw))) {
        recordError(std::string("write() failed: ") + std::strerror(errno));
        return false;
    }
    return true;
}

bool SocketCanBackend::receive(Frame& frame, std::chrono::milliseconds timeout) {
    if (socket_fd_ < 0) return false;

    // Always select() — including with timeout=0ms, which is the standard "non-blocking poll"
    // semantics callers expect. The previous optimization that skipped select() when
    // timeout==0 turned the read() below into a *blocking* call (default socket mode), which
    // hangs a polling caller indefinitely on an idle bus and breaks clean thread shutdown.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(socket_fd_, &rfds);

    timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;

    int sel = ::select(socket_fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) return false; // timeout (incl. 0ms with nothing pending) or error

    can_frame raw{};
    ssize_t n = ::read(socket_fd_, &raw, sizeof(raw));
    if (n != static_cast<ssize_t>(sizeof(raw))) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            recordError(std::string("read() failed: ") + std::strerror(errno));
        }
        return false;
    }

    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    bool is_err = (raw.can_id & CAN_ERR_FLAG) != 0;
    bool is_ext = (raw.can_id & CAN_EFF_FLAG) != 0;
    bool is_rtr = (raw.can_id & CAN_RTR_FLAG) != 0;

    frame.id = raw.can_id & (is_ext ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.dlc = raw.can_dlc;
    std::memcpy(frame.data, raw.data, raw.can_dlc);
    frame.timestamp_us = static_cast<uint64_t>(now_us);
    frame.is_extended_id = is_ext;
    frame.is_remote_frame = is_rtr;
    frame.is_error_frame = is_err;
    frame.is_fd_frame = false;

    if (is_err) {
        updateStateFromErrorFrame(raw.can_id, raw.data, raw.can_dlc);
        // If the caller didn't ask for error frames, drop them.
        if (!config_.receive_error_frames) return false;
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
    if (socket_fd_ < 0) return info_;
    // Refresh dynamic fields each call so callers see live driver/firmware
    // strings even if they queried before open().
    return buildSocketCanInfo(config_.channel_id);
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
