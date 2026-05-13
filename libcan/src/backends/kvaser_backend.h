/**
 * @file kvaser_backend.h
 * @brief Kvaser canlib backend implementation
 *
 * Cross-platform CAN backend wrapping Kvaser's canlib API. Works with
 * any Kvaser USB / PCI / virtual interface that the installed canlib
 * supports.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#ifndef CAN_KVASER_BACKEND_H
#define CAN_KVASER_BACKEND_H

#include "can/i_can_backend.h"

#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>

namespace can {

class KvaserBackend : public ICanBackend {
public:
    KvaserBackend();
    ~KvaserBackend() override;

    BackendKind kind() const override { return BackendKind::Kvaser; }
    BackendCapabilities capabilities() const override;

    std::vector<AdapterInfo> enumerateAdapters() override;

    bool open(const ChannelConfig& cfg) override;
    void close() override;
    bool isOpen() const override { return handle_.load(std::memory_order_acquire) >= 0; }

    bool send(const Frame& frame) override;
    bool receive(Frame& frame, std::chrono::milliseconds timeout) override;

    ChannelStatus status() const override;
    AdapterInfo info() const override;

    std::string lastError() const override;

private:
    void recordError(const std::string& msg);
    void recordCanError(const std::string& context, int can_status);
    static AdapterInfo buildAdapterInfo(int channel);

    /// Kvaser canHandle. -1 means closed. Held atomic so send()/receive()/
    /// status() loads against the exchange-to-(-1) in close() are
    /// well-defined per the threading contract in i_can_backend.h.
    /// close() exchanges to -1 *before* canBusOff/canClose, so a later
    /// operational load sees the closed state and bails before reaching
    /// the SDK with a stale handle. Stored as int so the public header
    /// doesn't depend on canlib.h.
    std::atomic<int> handle_{-1};
    int channel_index_ = -1;  ///< Set in open(), read only by info() — guarded by the open/close lifecycle

    ChannelConfig config_;
    AdapterInfo info_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<uint64_t> rx_overruns_{0};
};

} // namespace can

#endif // CAN_KVASER_BACKEND_H
