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
    bool isOpen() const override { return socket_fd_ >= 0; }

    bool send(const Frame& frame) override;
    bool receive(Frame& frame, std::chrono::milliseconds timeout) override;
    int receiveFd() const override { return socket_fd_; }

    ChannelStatus status() const override;
    AdapterInfo info() const override;

    std::string lastError() const override;

private:
    void recordError(const std::string& msg);
    void updateStateFromErrorFrame(uint32_t can_id, const uint8_t* data, uint8_t dlc);

    int socket_fd_ = -1;
    ChannelConfig config_;
    AdapterInfo info_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

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
