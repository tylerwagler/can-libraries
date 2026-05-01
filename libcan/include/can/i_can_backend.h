/**
 * @file i_can_backend.h
 * @brief Abstract CAN backend interface
 *
 * Defines the ICanBackend interface that all CAN driver backends implement
 * (SocketCAN, PCANBasic, Kvaser canlib, Vector XL Driver Library).
 *
 * The backend layer is the lowest-level abstraction in libcan. Higher-level
 * code (can::Interface, Qt CanIoWorker) holds a unique_ptr<ICanBackend> and
 * is otherwise unaware of which driver is in use.
 *
 * @copyright Copyright (c) 2026 Crane / Elytron Communications
 * @license MIT License
 */

#ifndef CAN_I_CAN_BACKEND_H
#define CAN_I_CAN_BACKEND_H

#include "can/can_interface.h"
#include "can/libcan_export.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace can {

/// Identifies which driver implements a backend.
enum class BackendKind {
    SocketCan,    ///< Linux SocketCAN
    PcanBasic,    ///< PEAK-System PCANBasic API
    Kvaser,       ///< Kvaser canlib
    VectorXL,     ///< Vector XL Driver Library (Windows-only)
};

LIBCAN_EXPORT std::string backendKindToString(BackendKind kind);

/// Bus state per the CAN error confinement state machine.
enum class BusState {
    Unknown,
    ErrorActive,
    ErrorWarning,
    ErrorPassive,
    BusOff,
};

LIBCAN_EXPORT std::string busStateToString(BusState state);

/// Static description of a backend's capabilities. Returned by ICanBackend::capabilities().
struct BackendCapabilities {
    bool supports_can_fd = false;
    bool supports_listen_only = false;
    bool supports_loopback = false;
    bool supports_receive_own = false;
    bool supports_acceptance_filters = false;
    bool exposes_error_counters = false;
    bool exposes_bus_load = false;
    bool exposes_firmware_version = false;
    bool exposes_serial_number = false;
    bool exposes_receive_fd = false;     ///< receiveFd() returns a usable fd (Linux)
};

/// Identifying / diagnostic info for a single CAN adapter or channel.
///
/// Populated both by enumerateAdapters() (pre-open, may have empty channel_id
/// fields filled in) and by info() on an open backend (all fields populated).
struct AdapterInfo {
    BackendKind backend = BackendKind::SocketCan;
    std::string device_name;        ///< "PCAN-USB Pro FD", "Kvaser Leaf Light v2", "can0", ...
    std::string channel_id;         ///< Backend-specific channel identifier, e.g. "PCAN_USBBUS1", "can0"
    std::string serial_number;      ///< Adapter serial (empty if backend doesn't expose)
    std::string firmware_version;
    std::string driver_version;
    std::string hardware_part_number;
    uint32_t channel_index = 0;     ///< Backend-specific numeric channel/handle
    /// Backend-specific extras. Use for things that don't fit common fields
    /// (Kvaser EAN, PCAN bitrate-FD config, Vector hwIndex, etc.).
    std::map<std::string, std::string> extra;
};

/// Live status snapshot of an open channel. Cheap to retrieve repeatedly.
struct ChannelStatus {
    BusState bus_state = BusState::Unknown;
    uint32_t tx_error_counter = 0;
    uint32_t rx_error_counter = 0;
    uint64_t rx_queue_overruns = 0;
    uint64_t bus_errors = 0;
    /// 0.0 - 100.0. Negative if the backend does not measure bus load.
    double bus_load_percent = -1.0;
};

/// Configuration for opening a CAN channel.
struct ChannelConfig {
    std::string channel_id;          ///< Backend-specific (e.g. "can0", "PCAN_USBBUS1")
    uint32_t bitrate = 500000;       ///< Arbitration bitrate (bps)
    uint32_t data_bitrate = 0;       ///< CAN-FD data phase bitrate; 0 = no FD
    double sample_point = 0.75;      ///< 0.0..1.0; ignored by backends that don't expose it
    bool listen_only = false;
    bool loopback = false;
    bool receive_own_messages = false;
    bool receive_error_frames = false;
};

/// Abstract CAN backend.
///
/// Lifecycle:
///   1. construct (cheap; no driver state held)
///   2. enumerateAdapters() — optional, lists available channels
///   3. open(cfg) — initialize hardware
///   4. send/receive/status — operational
///   5. close() or destruct
///
/// Thread safety:
///   - send() must be safe to call from any thread (vendor APIs are thread-safe).
///   - receive() should only be called from one thread at a time.
///   - status(), info(), lastError() are thread-safe.
class LIBCAN_EXPORT ICanBackend {
public:
    virtual ~ICanBackend() = default;

    virtual BackendKind kind() const = 0;
    virtual BackendCapabilities capabilities() const = 0;

    /// List adapters this backend can see. Returns an empty vector if the
    /// driver isn't installed or no hardware is attached. Safe to call
    /// without open().
    virtual std::vector<AdapterInfo> enumerateAdapters() = 0;

    virtual bool open(const ChannelConfig& cfg) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual bool send(const Frame& frame) = 0;

    /// Block up to `timeout` for a frame. Returns true if a frame was
    /// received, false on timeout or error.
    virtual bool receive(Frame& frame, std::chrono::milliseconds timeout) = 0;

    /// Linux-only: file descriptor that becomes readable when frames arrive.
    /// Returns -1 if unsupported (Windows backends, or backend not open).
    /// Useful for select()/poll() and Qt's QSocketNotifier.
    virtual int receiveFd() const { return -1; }

    virtual ChannelStatus status() const = 0;
    virtual AdapterInfo info() const = 0;

    virtual std::string lastError() const = 0;

    /// Construct a backend instance for the given kind. Returns nullptr if
    /// the requested backend was not compiled in.
    static std::unique_ptr<ICanBackend> create(BackendKind kind);

    /// Backends that were compiled in. Useful for populating UI dropdowns.
    static std::vector<BackendKind> availableBackends();

protected:
    ICanBackend() = default;
    ICanBackend(const ICanBackend&) = delete;
    ICanBackend& operator=(const ICanBackend&) = delete;
};

} // namespace can

#endif // CAN_I_CAN_BACKEND_H
