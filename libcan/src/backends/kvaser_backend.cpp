/**
 * @file kvaser_backend.cpp
 * @brief Kvaser canlib backend implementation
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "kvaser_backend.h"

#include <canlib.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace can {

namespace {

std::string canErrorText(canStatus st) {
    char buf[128] = {0};
    if (canGetErrorText(st, buf, sizeof(buf)) == canOK) {
        return std::string(buf);
    }
    std::ostringstream oss;
    oss << "canStatus " << static_cast<int>(st);
    return oss.str();
}

/// Read a string-typed canGetChannelData item.
std::string channelString(int channel, int item) {
    char buf[256] = {0};
    if (canGetChannelData(channel, item, buf, sizeof(buf)) == canOK) {
        return std::string(buf);
    }
    return {};
}

/// Map a generic bps value to a canlib BITRATE constant. Returns true
/// if a preset is available; for custom bitrates the caller would need
/// to compute BTR0/BTR1 directly (not yet exposed).
bool mapBitrate(uint32_t bps, long& out_bitrate) {
    switch (bps) {
        case 1000000: out_bitrate = canBITRATE_1M;   return true;
        case 500000:  out_bitrate = canBITRATE_500K; return true;
        case 250000:  out_bitrate = canBITRATE_250K; return true;
        case 125000:  out_bitrate = canBITRATE_125K; return true;
        case 100000:  out_bitrate = canBITRATE_100K; return true;
        case 83000:   out_bitrate = canBITRATE_83K;  return true;
        case 62000:   out_bitrate = canBITRATE_62K;  return true;
        case 50000:   out_bitrate = canBITRATE_50K;  return true;
        case 10000:   out_bitrate = canBITRATE_10K;  return true;
        default: return false;
    }
}

/// Initialize the canlib library exactly once per process. Safe to call
/// repeatedly.
void ensureLibInitialized() {
    static bool initialized = false;
    if (!initialized) {
        canInitializeLibrary();
        initialized = true;
    }
}

} // namespace

KvaserBackend::KvaserBackend() {
    ensureLibInitialized();
}

KvaserBackend::~KvaserBackend() {
    KvaserBackend::close();
}

BackendCapabilities KvaserBackend::capabilities() const {
    BackendCapabilities caps;
    // CAN-FD is not yet wired through this backend: open() doesn't pass
    // canOPEN_CAN_FD, send() doesn't set canFDMSG_FDF/BRS/ESI, and
    // canSetBusParamsFd isn't called for the data-phase bitrate. Advertise
    // false here so consumers don't try to send FD frames thinking they'll
    // round-trip. Re-enable once the FD path lands end-to-end.
    caps.supports_can_fd = false;
    caps.supports_listen_only = true;
    caps.supports_loopback = true;
    // Receive-own is not wired: enabling it on Kvaser needs canIoCtl with
    // the local-TX-echo ioctl AND a canMSG_TXACK check in receive() to
    // set frame.is_tx. The prior code mapped receive_own_messages to
    // canOPEN_ACCEPT_VIRTUAL, which is the flag for accessing *virtual*
    // channels and has nothing to do with TX echo. Advertise false until
    // the real path lands.
    caps.supports_receive_own = false;
    caps.supports_acceptance_filters = true;
    caps.exposes_error_counters = true;
    caps.exposes_bus_load = false;       // computed at higher layer
    caps.exposes_firmware_version = true;
    caps.exposes_serial_number = true;
    caps.exposes_receive_fd = false;     // canReadWait blocks natively
    return caps;
}

AdapterInfo KvaserBackend::buildAdapterInfo(int channel) {
    AdapterInfo a;
    a.backend = BackendKind::Kvaser;
    a.channel_index = static_cast<uint32_t>(channel);

    std::ostringstream chid;
    chid << "kvaser:" << channel;
    a.channel_id = chid.str();

    a.device_name = channelString(channel, canCHANNELDATA_DEVDESCR_ASCII);
    if (a.device_name.empty()) {
        a.device_name = channelString(channel, canCHANNELDATA_CHANNEL_NAME);
    }
    if (a.device_name.empty()) a.device_name = a.channel_id;

    // Firmware version: canCHANNELDATA_CARD_FIRMWARE_REV returns
    // four 16-bit values (build, release, minor, major) packed into
    // a 64-bit buffer. Format as M.m.r.b for display.
    uint64_t fw = 0;
    if (canGetChannelData(channel, canCHANNELDATA_CARD_FIRMWARE_REV, &fw, sizeof(fw)) == canOK) {
        uint16_t build   = static_cast<uint16_t>(fw & 0xFFFF);
        uint16_t release = static_cast<uint16_t>((fw >> 16) & 0xFFFF);
        uint16_t minor   = static_cast<uint16_t>((fw >> 32) & 0xFFFF);
        uint16_t major   = static_cast<uint16_t>((fw >> 48) & 0xFFFF);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", major, minor, release, build);
        a.firmware_version = buf;
    }

    uint64_t serial = 0;
    if (canGetChannelData(channel, canCHANNELDATA_CARD_SERIAL_NO, &serial, sizeof(serial)) == canOK
        && serial != 0) {
        a.serial_number = std::to_string(serial);
    }

    uint64_t ean = 0;
    if (canGetChannelData(channel, canCHANNELDATA_CARD_UPC_NO, &ean, sizeof(ean)) == canOK
        && ean != 0) {
        // EAN-13: 13 decimal digits. Stored packed as BCD in the high
        // bytes; format conservatively as raw decimal.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(ean));
        a.hardware_part_number = buf;
    }

    a.driver_version = channelString(channel, canCHANNELDATA_DLL_PRODUCT_VERSION);
    if (a.driver_version.empty()) {
        a.driver_version = channelString(channel, canCHANNELDATA_DRIVER_NAME);
    }

    uint32_t card_no = 0;
    if (canGetChannelData(channel, canCHANNELDATA_CARD_NUMBER, &card_no, sizeof(card_no)) == canOK) {
        a.extra["card_number"] = std::to_string(card_no);
    }
    uint32_t chan_on_card = 0;
    if (canGetChannelData(channel, canCHANNELDATA_CHAN_NO_ON_CARD, &chan_on_card, sizeof(chan_on_card)) == canOK) {
        a.extra["channel_on_card"] = std::to_string(chan_on_card);
    }

    return a;
}

std::vector<AdapterInfo> KvaserBackend::enumerateAdapters() {
    std::vector<AdapterInfo> result;

    int n = 0;
    if (canGetNumberOfChannels(&n) != canOK || n <= 0) {
        return result;
    }
    result.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        result.push_back(buildAdapterInfo(i));
    }
    return result;
}

bool KvaserBackend::open(const ChannelConfig& cfg) {
    if (handle_ >= 0) {
        recordError("Kvaser backend already open");
        return false;
    }

    // channel_id format: "kvaser:N" or just "N"
    int channel = -1;
    const std::string& id = cfg.channel_id;
    const char* num_start = id.c_str();
    if (id.rfind("kvaser:", 0) == 0) num_start = id.c_str() + 7;
    try { channel = std::stoi(num_start); }
    catch (...) {
        recordError("Invalid Kvaser channel_id (expected 'kvaser:N' or 'N'): " + id);
        return false;
    }

    if (cfg.data_bitrate > 0) {
        // CAN-FD open path isn't implemented yet (would need canOPEN_CAN_FD
        // and canSetBusParamsFd). Refuse rather than silently downgrade.
        recordError("CAN-FD open not yet implemented for Kvaser backend");
        return false;
    }

    if (cfg.receive_own_messages) {
        // Honest failure: the prior code OR'd in canOPEN_ACCEPT_VIRTUAL
        // here, which is the flag for opening virtual Kvaser channels —
        // not for echoing transmitted frames back to us. Wiring this
        // properly needs canIoCtl(handle, canIOCTL_SET_LOCAL_TXECHO/
        // TXACK, &on) and a canMSG_TXACK check in receive() to set
        // frame.is_tx. Refusing here is preferable to silently dropping
        // the flag on the floor.
        recordError("receive_own_messages not yet implemented for Kvaser backend");
        return false;
    }

    long bitrate;
    if (!mapBitrate(cfg.bitrate, bitrate)) {
        recordError("Unsupported bitrate for Kvaser preset: " + std::to_string(cfg.bitrate));
        return false;
    }

    int flags = canOPEN_EXCLUSIVE | canOPEN_REQUIRE_INIT_ACCESS;

    int h = canOpenChannel(channel, flags);
    if (h < 0) {
        recordCanError("canOpenChannel", h);
        return false;
    }

    canStatus st = canSetBusParams(h, bitrate, 0, 0, 0, 0, 0);
    if (st != canOK) {
        recordCanError("canSetBusParams", st);
        canClose(h);
        return false;
    }

    if (cfg.listen_only) {
        st = canSetBusOutputControl(h, canDRIVER_SILENT);
        if (st != canOK) {
            recordCanError("canSetBusOutputControl(SILENT)", st);
            canClose(h);
            return false;
        }
    } else {
        canSetBusOutputControl(h, canDRIVER_NORMAL);
    }

    st = canBusOn(h);
    if (st != canOK) {
        recordCanError("canBusOn", st);
        canClose(h);
        return false;
    }

    handle_ = h;
    channel_index_ = channel;
    config_ = cfg;
    info_ = buildAdapterInfo(channel);
    return true;
}

void KvaserBackend::close() {
    if (handle_ >= 0) {
        canBusOff(handle_);
        canClose(handle_);
        handle_ = -1;
    }
}

bool KvaserBackend::send(const Frame& frame) {
    if (handle_ < 0) {
        recordError("send() on closed Kvaser backend");
        return false;
    }
    if (frame.is_fd_frame) {
        // Reject loudly rather than silently truncating into a classic-CAN
        // canWrite. CAN-FD round-trip via this backend isn't wired yet —
        // see capabilities().supports_can_fd.
        recordError("CAN-FD send not yet supported by Kvaser backend");
        return false;
    }
    unsigned int flags = frame.is_extended_id ? canMSG_EXT : canMSG_STD;
    if (frame.is_remote_frame) flags |= canMSG_RTR;

    // Clamp to MAX_CAN_DLC: frame.dlc is public and a buggy caller could
    // hand us a value larger than the data array. canWrite reads `dlc`
    // bytes from the buffer.
    const uint8_t dlc = frame.dlc > MAX_CAN_DLC ? MAX_CAN_DLC : frame.dlc;
    canStatus st = canWrite(handle_, static_cast<long>(frame.id),
                            const_cast<uint8_t*>(frame.data.data()),
                            dlc, flags);
    if (st != canOK) {
        recordCanError("canWrite", st);
        return false;
    }
    return true;
}

bool KvaserBackend::receive(Frame& frame, std::chrono::milliseconds timeout) {
    if (handle_ < 0) return false;

    long id = 0;
    // Sized for CAN-FD even though we don't yet open channels in FD mode:
    // if a future canlib or a channel opened by another process delivers an
    // FD frame, canReadWait will write up to 64 bytes here. Sizing this at
    // 8 was a stack-overflow waiting to happen.
    unsigned char data[64] = {0};
    unsigned int dlc = 0;
    unsigned int flags = 0;
    unsigned long ts_ms = 0;

    // canReadWait blocks the caller in the kernel until a frame arrives
    // or the timeout expires — event-driven, not polling.
    canStatus st = canReadWait(handle_, &id, data, &dlc, &flags, &ts_ms,
                               static_cast<unsigned long>(timeout.count()));
    if (st == canERR_NOMSG) return false;
    if (st != canOK) {
        if (st == canERR_HARDWARE) rx_overruns_.fetch_add(1);
        recordCanError("canReadWait", st);
        return false;
    }

    // Reset before populating so stale flags / data bytes from a prior call
    // don't leak through (e.g. is_extended_id surviving onto a standard
    // frame). Matches SocketCanBackend::receive().
    frame = Frame{};
    frame.id = static_cast<uint32_t>(id);
    frame.dlc = static_cast<uint8_t>(dlc > MAX_CAN_FD_DLC ? MAX_CAN_FD_DLC : dlc);
    std::memcpy(frame.data.data(), data, frame.dlc);
    frame.timestamp_us = static_cast<uint64_t>(ts_ms) * 1000ULL;
    frame.is_extended_id = (flags & canMSG_EXT) != 0;
    frame.is_remote_frame = (flags & canMSG_RTR) != 0;
    frame.is_error_frame = (flags & canMSGERR_MASK) != 0;
    frame.is_fd_frame = (flags & canFDMSG_FDF) != 0;
    return true;
}

ChannelStatus KvaserBackend::status() const {
    ChannelStatus s;
    s.bus_load_percent = -1.0;
    s.rx_queue_overruns = rx_overruns_.load();

    if (handle_ < 0) {
        s.bus_state = BusState::Unknown;
        return s;
    }

    unsigned long flags = 0;
    if (canReadStatus(handle_, &flags) == canOK) {
        if (flags & canSTAT_BUS_OFF)              s.bus_state = BusState::BusOff;
        else if (flags & canSTAT_ERROR_PASSIVE)   s.bus_state = BusState::ErrorPassive;
        else if (flags & canSTAT_ERROR_WARNING)   s.bus_state = BusState::ErrorWarning;
        else if (flags & canSTAT_ERROR_ACTIVE)    s.bus_state = BusState::ErrorActive;
        else                                       s.bus_state = BusState::Unknown;
    }

    unsigned int tx_err = 0, rx_err = 0, ovrn = 0;
    if (canReadErrorCounters(handle_, &tx_err, &rx_err, &ovrn) == canOK) {
        s.tx_error_counter = tx_err;
        s.rx_error_counter = rx_err;
        s.bus_errors = ovrn;
    }
    return s;
}

AdapterInfo KvaserBackend::info() const {
    if (channel_index_ < 0) return info_;
    return buildAdapterInfo(channel_index_);
}

std::string KvaserBackend::lastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void KvaserBackend::recordError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = msg;
}

void KvaserBackend::recordCanError(const std::string& context, int can_status) {
    recordError(context + ": " + canErrorText(static_cast<canStatus>(can_status)));
}

std::unique_ptr<ICanBackend> createKvaserBackend() {
    return std::unique_ptr<ICanBackend>(new KvaserBackend());
}

} // namespace can
