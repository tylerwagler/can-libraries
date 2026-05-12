/**
 * @file socketcan_backend.h
 * @brief Linux SocketCAN backend implementation
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#ifndef CAN_SOCKETCAN_BACKEND_H
#define CAN_SOCKETCAN_BACKEND_H

#include "can/i_can_backend.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace can {

class SocketCanBackend : public ICanBackend {
public:
    SocketCanBackend();
    ~SocketCanBackend() override;

    BackendKind kind() const override { return BackendKind::SocketCan; }
    BackendCapabilities capabilities() const override;

    std::vector<AdapterInfo> enumerateAdapters() override;

    bool open(const ChannelConfig& cfg) override;
    void close() override;
    bool isOpen() const override { return socket_fd_.load(std::memory_order_acquire) >= 0; }

    bool send(const Frame& frame) override;
    bool receive(Frame& frame, std::chrono::milliseconds timeout) override;
    int receiveFd() const override { return socket_fd_.load(std::memory_order_acquire); }

    ChannelStatus status() const override;
    AdapterInfo info() const override;

    std::string lastError() const override;

private:
    void recordError(const std::string& msg);
    void updateStateFromErrorFrame(uint32_t can_id, const uint8_t* data, uint8_t dlc);

    /// Tracks a frame we just sent so the matching kernel echo (delivered
    /// when CAN_RAW_RECV_OWN_MSGS is enabled) can be tagged with is_tx=true
    /// downstream. Entries expire after a short window to bound memory.
    struct RecentTx {
        uint32_t id;
        uint8_t  dlc;
        bool     is_fd;
        uint64_t payload_hash;
        uint64_t deadline_us;
    };

    void pushRecentTx(const Frame& frame);
    bool consumeRecentTx(const Frame& frame);

    /// Held atomic so concurrent loads from send/receive against the
    /// store in close() are well-defined. close() exchanges this to -1
    /// *before* invoking the syscall on the fd, so a later loader sees
    /// the closed state. The kernel-reuse TOCTOU on a fd already loaded
    /// before close() ran is still undefined per i_can_backend.h.
    std::atomic<int> socket_fd_{-1};
    /// Sideband eventfd added to receive()'s select(2) fdset. close()
    /// writes to it to wake a worker blocked in select() promptly —
    /// shutdown(2) is a no-op on AF_CAN SOCK_RAW sockets, so we need
    /// an explicit wakeup channel. Atomic so receive() and close() can
    /// observe each other's writes without UB. -1 when not open.
    std::atomic<int> shutdown_eventfd_{-1};
    bool fd_enabled_ = false;
    ChannelConfig config_;
    AdapterInfo info_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    mutable std::mutex recent_tx_mutex_;
    std::deque<RecentTx> recent_tx_;

    // Live counters / state. Updated on RX of error frames.
    mutable std::mutex status_mutex_;
    BusState bus_state_ = BusState::Unknown;
    std::atomic<uint32_t> tx_err_counter_{0};
    std::atomic<uint32_t> rx_err_counter_{0};
    std::atomic<uint64_t> bus_errors_{0};
    std::atomic<uint64_t> rx_overruns_{0};
};

} // namespace can

#endif // CAN_SOCKETCAN_BACKEND_H
