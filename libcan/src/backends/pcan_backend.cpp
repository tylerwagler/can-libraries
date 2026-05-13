/**
 * @file pcan_backend.cpp
 * @brief PEAK-System PCANBasic backend implementation
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "pcan_backend.h"

#include <PCANBasic.h>

#include <chrono>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <cerrno>
#  include <sys/eventfd.h>
#  include <sys/select.h>
#  include <unistd.h>
#endif

namespace can {

namespace {

/// All channel handles we'll probe during enumerateAdapters().
/// Covers USB (1..16), PCI (1..8), LAN (1..8). PCANBasic does define
/// PCAN_PCIBUS9..16 and PCAN_LANBUS9..16 in current SDKs; extend the
/// list here if you have a chassis that lights more than eight of
/// either interface up.
constexpr TPCANHandle kProbeHandles[] = {
    PCAN_USBBUS1,  PCAN_USBBUS2,  PCAN_USBBUS3,  PCAN_USBBUS4,
    PCAN_USBBUS5,  PCAN_USBBUS6,  PCAN_USBBUS7,  PCAN_USBBUS8,
    PCAN_USBBUS9,  PCAN_USBBUS10, PCAN_USBBUS11, PCAN_USBBUS12,
    PCAN_USBBUS13, PCAN_USBBUS14, PCAN_USBBUS15, PCAN_USBBUS16,
    PCAN_PCIBUS1,  PCAN_PCIBUS2,  PCAN_PCIBUS3,  PCAN_PCIBUS4,
    PCAN_PCIBUS5,  PCAN_PCIBUS6,  PCAN_PCIBUS7,  PCAN_PCIBUS8,
    PCAN_LANBUS1,  PCAN_LANBUS2,  PCAN_LANBUS3,  PCAN_LANBUS4,
    PCAN_LANBUS5,  PCAN_LANBUS6,  PCAN_LANBUS7,  PCAN_LANBUS8,
};

std::string handleToChannelId(TPCANHandle h) {
    // PCAN_USBBUSn = 0x51 + (n-1), PCAN_PCIBUSn = 0x41 + (n-1),
    // PCAN_LANBUSn = 0x801 + (n-1). Encode in a stable string the user
    // can pass back to open(channel_id).
    std::ostringstream oss;
    if (h >= PCAN_USBBUS1 && h <= PCAN_USBBUS16) {
        oss << "PCAN_USBBUS" << (h - PCAN_USBBUS1 + 1);
    } else if (h >= PCAN_PCIBUS1 && h <= PCAN_PCIBUS8) {
        oss << "PCAN_PCIBUS" << (h - PCAN_PCIBUS1 + 1);
    } else if (h >= PCAN_LANBUS1 && h <= PCAN_LANBUS8) {
        oss << "PCAN_LANBUS" << (h - PCAN_LANBUS1 + 1);
    } else {
        oss << "PCAN_HANDLE_0x" << std::hex << h;
    }
    return oss.str();
}

TPCANHandle channelIdToHandle(const std::string& id) {
    auto starts_with = [&](const char* s) {
        return id.compare(0, std::strlen(s), s) == 0;
    };
    auto idx = [&](const char* prefix) -> int {
        try { return std::stoi(id.substr(std::strlen(prefix))); }
        catch (...) { return 0; }
    };
    if (starts_with("PCAN_USBBUS")) {
        int n = idx("PCAN_USBBUS");
        if (n >= 1 && n <= 16) return PCAN_USBBUS1 + (n - 1);
    } else if (starts_with("PCAN_PCIBUS")) {
        int n = idx("PCAN_PCIBUS");
        if (n >= 1 && n <= 8) return PCAN_PCIBUS1 + (n - 1);
    } else if (starts_with("PCAN_LANBUS")) {
        int n = idx("PCAN_LANBUS");
        if (n >= 1 && n <= 8) return PCAN_LANBUS1 + (n - 1);
    }
    return 0;
}

std::string pcanErrorText(TPCANStatus status) {
    char buf[256] = {0};
    if (CAN_GetErrorText(status, 0x09 /* PCAN_LANGUAGE_ENGLISH */, buf) == PCAN_ERROR_OK) {
        return std::string(buf);
    }
    std::ostringstream oss;
    oss << "PCAN status 0x" << std::hex << status;
    return oss.str();
}

std::string pcanStringValue(TPCANHandle h, TPCANParameter param) {
    char buf[256] = {0};
    if (CAN_GetValue(h, param, buf, sizeof(buf)) == PCAN_ERROR_OK) {
        return std::string(buf);
    }
    return {};
}

uint32_t pcanDwordValue(TPCANHandle h, TPCANParameter param) {
    DWORD v = 0;
    if (CAN_GetValue(h, param, &v, sizeof(v)) == PCAN_ERROR_OK) {
        return static_cast<uint32_t>(v);
    }
    return 0;
}

} // namespace

PcanBackend::PcanBackend() = default;

PcanBackend::~PcanBackend() {
    PcanBackend::close();
}

BackendCapabilities PcanBackend::capabilities() const {
    BackendCapabilities caps;
    // CAN-FD is not yet wired through this backend: open() refuses
    // data_bitrate > 0, send()/receive() use the classic TPCANMsg types
    // (FD would need TPCANMsgFD with CAN_WriteFD / CAN_ReadFD). Advertise
    // false here so consumers don't try to send FD frames thinking they'll
    // round-trip. Re-enable once the FD path lands end-to-end.
    caps.supports_can_fd = false;
    caps.supports_listen_only = true;
    caps.supports_loopback = false; // not exposed via PCANBasic in a portable way
    caps.supports_receive_own = false;
    caps.supports_acceptance_filters = true;
    caps.exposes_error_counters = false; // PCAN status flags only, not raw counters
    caps.exposes_bus_load = true;        // via PCAN_BUSSPEED_NOMINAL? Not directly; computed at higher layer.
    caps.exposes_firmware_version = true;
    caps.exposes_serial_number = true;
#ifdef _WIN32
    caps.exposes_receive_fd = false;
#else
    caps.exposes_receive_fd = true;
#endif
    return caps;
}

bool PcanBackend::mapBitrate(uint32_t bps, uint16_t& out) {
    switch (bps) {
        case 1000000: out = PCAN_BAUD_1M;   return true;
        case 800000:  out = PCAN_BAUD_800K; return true;
        case 500000:  out = PCAN_BAUD_500K; return true;
        case 250000:  out = PCAN_BAUD_250K; return true;
        case 125000:  out = PCAN_BAUD_125K; return true;
        case 100000:  out = PCAN_BAUD_100K; return true;
        case 95000:   out = PCAN_BAUD_95K;  return true;
        case 83000:   out = PCAN_BAUD_83K;  return true;
        case 50000:   out = PCAN_BAUD_50K;  return true;
        case 47000:   out = PCAN_BAUD_47K;  return true;
        case 33000:   out = PCAN_BAUD_33K;  return true;
        case 20000:   out = PCAN_BAUD_20K;  return true;
        case 10000:   out = PCAN_BAUD_10K;  return true;
        case 5000:    out = PCAN_BAUD_5K;   return true;
        default: return false;
    }
}

AdapterInfo PcanBackend::buildAdapterInfo(uint16_t handle) {
    AdapterInfo a;
    a.backend = BackendKind::PcanBasic;
    a.channel_id = handleToChannelId(handle);
    a.channel_index = handle;
    a.device_name = pcanStringValue(handle, PCAN_HARDWARE_NAME);
    if (a.device_name.empty()) a.device_name = a.channel_id;
    a.firmware_version = pcanStringValue(handle, PCAN_CHANNEL_VERSION);
    a.driver_version = pcanStringValue(handle, PCAN_API_VERSION);
    a.hardware_part_number = pcanStringValue(handle, PCAN_DEVICE_PART_NUMBER);

    DWORD device_id = pcanDwordValue(handle, PCAN_DEVICE_ID);
    if (device_id != 0) {
        std::ostringstream oss;
        oss << device_id;
        a.serial_number = oss.str();
    }

    DWORD condition = pcanDwordValue(handle, PCAN_CHANNEL_CONDITION);
    if (condition == PCAN_CHANNEL_AVAILABLE) {
        a.extra["condition"] = "available";
    } else if (condition == PCAN_CHANNEL_OCCUPIED) {
        a.extra["condition"] = "occupied";
    } else if (condition == PCAN_CHANNEL_PCANVIEW) {
        a.extra["condition"] = "in-use-by-pcanview";
    } else {
        a.extra["condition"] = "unavailable";
    }
    return a;
}

std::vector<AdapterInfo> PcanBackend::enumerateAdapters() {
    std::vector<AdapterInfo> result;
    for (TPCANHandle h : kProbeHandles) {
        DWORD condition = pcanDwordValue(h, PCAN_CHANNEL_CONDITION);
        if (condition == PCAN_CHANNEL_UNAVAILABLE) continue;
        result.push_back(buildAdapterInfo(h));
    }
    return result;
}

bool PcanBackend::open(const ChannelConfig& cfg) {
    // open() is a lifecycle method — by contract callers serialize it
    // against close() and the operational methods, so a plain load is fine.
    if (channel_handle_.load(std::memory_order_acquire) != 0) {
        recordError("PCAN backend already open");
        return false;
    }

    TPCANHandle h = channelIdToHandle(cfg.channel_id);
    if (h == 0) {
        recordError("Unknown PCAN channel: " + cfg.channel_id);
        return false;
    }

    TPCANStatus st;
    if (cfg.data_bitrate > 0) {
        // CAN-FD: build a bitrate FD string like "f_clock=80000000, nom_brp=10, nom_tseg1=12, ..."
        // Caller is expected to pass a valid string in cfg.extra["pcan_fd_bitrate"];
        // we do not auto-derive timing parameters here.
        recordError("CAN-FD requires explicit timing string; not implemented yet");
        return false;
    }

    uint16_t btr0btr1;
    if (!mapBitrate(cfg.bitrate, btr0btr1)) {
        recordError("Unsupported bitrate for PCAN: " + std::to_string(cfg.bitrate));
        return false;
    }

    st = CAN_Initialize(h, btr0btr1, 0, 0, 0);
    if (st != PCAN_ERROR_OK) {
        recordPcanError("CAN_Initialize", st);
        return false;
    }

    // Listen-only mode if requested.
    if (cfg.listen_only) {
        DWORD on = PCAN_PARAMETER_ON;
        st = CAN_SetValue(h, PCAN_LISTEN_ONLY, &on, sizeof(on));
        if (st != PCAN_ERROR_OK) {
            recordPcanError("CAN_SetValue(LISTEN_ONLY)", st);
            CAN_Uninitialize(h);
            return false;
        }
    }

    config_ = cfg;
    info_ = buildAdapterInfo(h);

#ifdef _WIN32
    // Windows: retrieve the auto-reset Win32 event HANDLE that PCANBasic
    // signals when frames arrive, then wait on it with WaitForMultipleObjects.
    // This is the event-driven path — no polling.
    HANDLE evt = nullptr;
    if (CAN_GetValue(h, PCAN_RECEIVE_EVENT, &evt, sizeof(evt)) == PCAN_ERROR_OK) {
        receive_handle_.store(evt, std::memory_order_release);
    } else {
        receive_handle_.store(nullptr, std::memory_order_release);
    }

    // Sideband event so close() can wake a blocked WaitForMultipleObjects
    // promptly instead of forcing the worker to wait out its receive()
    // timeout. Manual-reset auto-cleared by close(); see header.
    HANDLE shutdown_evt = CreateEventA(nullptr, /*manualReset=*/TRUE,
                                       /*initialState=*/FALSE, nullptr);
    if (shutdown_evt == nullptr) {
        recordError("CreateEvent(shutdown) failed");
        CAN_Uninitialize(h);
        return false;
    }
    shutdown_event_handle_.store(shutdown_evt, std::memory_order_release);
#else
    // Linux: retrieve the receive event fd so callers (and select()) can
    // wait without polling.
    int fd = -1;
    if (CAN_GetValue(h, PCAN_RECEIVE_EVENT, &fd, sizeof(fd)) == PCAN_ERROR_OK) {
        receive_fd_.store(fd, std::memory_order_release);
    } else {
        receive_fd_.store(-1, std::memory_order_release);
    }

    // Sideband eventfd so close() can wake a blocked select() promptly.
    int sevfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (sevfd < 0) {
        recordError(std::string("eventfd(shutdown) failed: ") + std::strerror(errno));
        CAN_Uninitialize(h);
        return false;
    }
    shutdown_eventfd_.store(sevfd, std::memory_order_release);
#endif

    // Release-store last so operational threads observing channel_handle_ != 0
    // are guaranteed to see the config_/info_/receive_fd_/receive_handle_/
    // shutdown_* writes that precede it.
    channel_handle_.store(h, std::memory_order_release);

    return true;
}

void PcanBackend::close() {
    // Ordering matches SocketCanBackend so a blocked receive() returns
    // promptly:
    //   1. Invalidate shutdown_* first — any worker that hasn't yet
    //      loaded it sees the sentinel and bails before touching the
    //      SDK handles.
    //   2. Signal the *snapshotted* shutdown primitive — wakes any
    //      worker already inside select() / WaitForMultipleObjects.
    //   3. Exchange channel_handle_ to 0 so racing new entrants get the
    //      closed-state fast-path.
    //   4. Tear down the SDK side.
    //   5. Release the shutdown primitive itself.
#ifdef _WIN32
    HANDLE shutdown_evt = static_cast<HANDLE>(
        shutdown_event_handle_.exchange(nullptr, std::memory_order_acq_rel));
    if (shutdown_evt) SetEvent(shutdown_evt);
#else
    int shutdown_fd = shutdown_eventfd_.exchange(-1, std::memory_order_acq_rel);
    if (shutdown_fd >= 0) {
        const uint64_t one = 1;
        ssize_t w = ::write(shutdown_fd, &one, sizeof(one));
        (void)w;
    }
#endif
    uint16_t h = channel_handle_.exchange(0, std::memory_order_acq_rel);
    receive_fd_.store(-1, std::memory_order_release);
    receive_handle_.store(nullptr, std::memory_order_release);
    if (h != 0) {
        CAN_Uninitialize(h);
    }
#ifdef _WIN32
    if (shutdown_evt) CloseHandle(shutdown_evt);
#else
    if (shutdown_fd >= 0) ::close(shutdown_fd);
#endif
}

bool PcanBackend::send(const Frame& frame) {
    const uint16_t h = channel_handle_.load(std::memory_order_acquire);
    if (h == 0) {
        recordError("send() on closed PCAN backend");
        return false;
    }

    TPCANMsg msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.ID = frame.id;
    msg.MSGTYPE = frame.is_extended_id ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD;
    if (frame.is_remote_frame) msg.MSGTYPE |= PCAN_MESSAGE_RTR;
    msg.LEN = frame.dlc > 8 ? 8 : frame.dlc;
    std::memcpy(msg.DATA, frame.data.data(), msg.LEN);

    TPCANStatus st = CAN_Write(h, &msg);
    if (st != PCAN_ERROR_OK) {
        recordPcanError("CAN_Write", st);
        return false;
    }
    return true;
}

bool PcanBackend::receive(Frame& frame, std::chrono::milliseconds timeout) {
    const uint16_t h = channel_handle_.load(std::memory_order_acquire);
    if (h == 0) return false;

    // Block on the platform-native event primitive multiplexed with our
    // shutdown sideband, so close() returns promptly without forcing the
    // worker to wait out its receive() timeout. No polling.
#ifdef _WIN32
    HANDLE handle = static_cast<HANDLE>(receive_handle_.load(std::memory_order_acquire));
    HANDLE shutdown_evt = static_cast<HANDLE>(
        shutdown_event_handle_.load(std::memory_order_acquire));
    if (handle && timeout.count() > 0) {
        HANDLE handles[2];
        DWORD num_handles = 1;
        handles[0] = handle;
        if (shutdown_evt) {
            handles[num_handles++] = shutdown_evt;
        }
        // Clamp to UINT32_MAX-1 so very-large finite timeouts stay finite
        // — mapping them to INFINITE turns a "wait 50 days" caller into
        // a never-returning wait.
        constexpr uint64_t kMaxMs = 0xFFFFFFFEULL;
        DWORD ms = static_cast<uint64_t>(timeout.count()) > kMaxMs
            ? static_cast<DWORD>(kMaxMs)
            : static_cast<DWORD>(timeout.count());
        DWORD r = WaitForMultipleObjects(num_handles, handles, FALSE, ms);
        if (r == WAIT_TIMEOUT) return false;
        if (r == WAIT_OBJECT_0 + 1) return false;  // close() signalled
        if (r != WAIT_OBJECT_0) {
            recordError("WaitForMultipleObjects failed");
            return false;
        }
    } else if (!handle) {
        // No event handle was retrieved at open(). The driver normally
        // provides one; falling through here means we'd have to poll,
        // which we explicitly refuse to do. Bail.
        recordError("PCAN_RECEIVE_EVENT unavailable; refusing to poll");
        return false;
    }
#else
    int rfd  = receive_fd_.load(std::memory_order_acquire);
    int sfd  = shutdown_eventfd_.load(std::memory_order_acquire);
    if (rfd >= 0 && timeout.count() > 0) {
        // EINTR-retry loop so a signal landing on this thread (Qt apps
        // commonly install SIGCHLD handlers from QProcess) doesn't get
        // misreported as a timeout. We track the deadline and recompute
        // the remaining timeout each iteration so total wait remains
        // bounded.
        auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            auto remain = std::chrono::duration_cast<std::chrono::microseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remain.count() <= 0) return false;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(rfd, &rfds);
            if (sfd >= 0) FD_SET(sfd, &rfds);
            const int nfds = (sfd > rfd ? sfd : rfd) + 1;
            timeval tv;
            tv.tv_sec  = remain.count() / 1'000'000;
            tv.tv_usec = remain.count() % 1'000'000;
            int sel = ::select(nfds, &rfds, nullptr, nullptr, &tv);
            if (sel > 0) {
                if (sfd >= 0 && FD_ISSET(sfd, &rfds)) return false;  // close() signalled
                break;
            }
            if (sel == 0) return false;
            if (errno == EINTR) continue;
            return false;
        }
    } else if (rfd < 0) {
        recordError("PCAN_RECEIVE_EVENT unavailable; refusing to poll");
        return false;
    }
#endif

    // The event fired (or signalled) — drain one frame. CAN_Read may
    // return QRCVEMPTY in the (rare) case where another consumer drained
    // the queue between event signal and our read; that's not a poll,
    // it's a single check, return false and let the worker loop's next
    // iteration wait on the event again.
    TPCANMsg msg;
    TPCANTimestamp ts;
    TPCANStatus st = CAN_Read(h, &msg, &ts);
    if (st == PCAN_ERROR_OK) {
        // Reset before populating so stale flags / data bytes from a prior
        // call don't leak through. Matches SocketCanBackend::receive().
        frame = Frame{};
        frame.id = msg.ID;
        const uint8_t copy_len = msg.LEN > 8 ? 8 : msg.LEN;
        frame.dlc = copy_len;
        std::memcpy(frame.data.data(), msg.DATA, copy_len);
        // TPCANTimestamp: total time in ms is (millis_overflow << 32) | millis,
        // then add micros for sub-ms resolution. The previous expression treated
        // each overflow as 10^9 µs instead of 2^32 ms — only correct while
        // millis_overflow == 0, i.e. the first ~49.7 days of driver uptime.
        const uint64_t total_ms = (static_cast<uint64_t>(ts.millis_overflow) << 32)
                                | static_cast<uint64_t>(ts.millis);
        frame.timestamp_us = total_ms * 1000ULL + static_cast<uint64_t>(ts.micros);
        frame.is_extended_id = (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0;
        frame.is_remote_frame = (msg.MSGTYPE & PCAN_MESSAGE_RTR) != 0;
        frame.is_error_frame = (msg.MSGTYPE & PCAN_MESSAGE_ERRFRAME) != 0;
        frame.is_fd_frame = (msg.MSGTYPE & PCAN_MESSAGE_FD) != 0;
        return true;
    }
    if (st == PCAN_ERROR_QRCVEMPTY) {
        return false;
    }
    if (st & PCAN_ERROR_QOVERRUN) {
        rx_overruns_.fetch_add(1);
    }
    recordPcanError("CAN_Read", st);
    return false;
}

int PcanBackend::receiveFd() const {
    return receive_fd_.load(std::memory_order_acquire);
}

ChannelStatus PcanBackend::status() const {
    ChannelStatus s;
    s.bus_load_percent = -1.0;
    s.rx_queue_overruns = rx_overruns_.load();

    const uint16_t h = channel_handle_.load(std::memory_order_acquire);
    if (h == 0) {
        s.bus_state = BusState::Unknown;
        return s;
    }

    TPCANStatus st = CAN_GetStatus(h);
    if (st == PCAN_ERROR_OK) {
        s.bus_state = BusState::ErrorActive;
    } else if (st & PCAN_ERROR_BUSPASSIVE) {
        s.bus_state = BusState::ErrorPassive;
    } else if (st & PCAN_ERROR_BUSWARNING) {
        s.bus_state = BusState::ErrorWarning;
    } else if (st & PCAN_ERROR_BUSOFF) {
        s.bus_state = BusState::BusOff;
    } else {
        s.bus_state = BusState::Unknown;
    }
    s.bus_errors = (st & PCAN_ERROR_ANYBUSERR) ? 1 : 0;

    return s;
}

AdapterInfo PcanBackend::info() const {
    // M-7: return the cached snapshot captured at open() time. Each
    // buildAdapterInfo() call invokes 5+ CAN_GetValue syscalls, which is
    // not what "info() is cheap" suggests in the abstract docs. The static
    // fields (device name, firmware/driver version, serial, part number)
    // don't change at runtime, so caching is safe; consumers that need
    // live status should call status() instead. Pre-open() calls get the
    // default-constructed info_, matching the contract that info() is
    // only well-defined after open() returns.
    return info_;
}

std::string PcanBackend::lastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void PcanBackend::recordError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = msg;
}

void PcanBackend::recordPcanError(const std::string& context, uint32_t pcan_status) {
    recordError(context + ": " + pcanErrorText(static_cast<TPCANStatus>(pcan_status)));
}

std::unique_ptr<ICanBackend> createPcanBackend() {
    return std::unique_ptr<ICanBackend>(new PcanBackend());
}

} // namespace can
