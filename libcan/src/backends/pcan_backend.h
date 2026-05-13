/**
 * @file pcan_backend.h
 * @brief PEAK-System PCANBasic backend implementation
 *
 * Cross-platform CAN backend wrapping PEAK-System's PCANBasic API. Works
 * with PCAN-USB, PCAN-USB FD, PCAN-USB Pro, PCAN-PCI, PCAN-LAN, etc.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#ifndef CAN_PCAN_BACKEND_H
#define CAN_PCAN_BACKEND_H

#include "can/i_can_backend.h"

#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>

namespace can {

class PcanBackend : public ICanBackend {
public:
    PcanBackend();
    ~PcanBackend() override;

    BackendKind kind() const override { return BackendKind::PcanBasic; }
    BackendCapabilities capabilities() const override;

    std::vector<AdapterInfo> enumerateAdapters() override;

    bool open(const ChannelConfig& cfg) override;
    void close() override;
    bool isOpen() const override { return channel_handle_.load(std::memory_order_acquire) != 0; }

    bool send(const Frame& frame) override;
    bool receive(Frame& frame, std::chrono::milliseconds timeout) override;
    int receiveFd() const override;

    ChannelStatus status() const override;
    AdapterInfo info() const override;

    std::string lastError() const override;

private:
    void recordError(const std::string& msg);
    void recordPcanError(const std::string& context, uint32_t pcan_status);
    void populateAdapterInfo();
    static bool mapBitrate(uint32_t bps, uint16_t& out_btr0btr1);
    static AdapterInfo buildAdapterInfo(uint16_t handle);

    /// PCAN channel handle (TPCANHandle). 0 means closed. Held atomic so
    /// send()/receive()/status() loads against the exchange-to-0 in close()
    /// are well-defined per the threading contract in i_can_backend.h.
    /// close() exchanges to 0 *before* calling CAN_Uninitialize so a later
    /// operational load sees the closed state and bails before reaching the
    /// SDK with a stale handle. Stored as plain integer so the public
    /// header doesn't need PCANBasic.h.
    std::atomic<uint16_t> channel_handle_{0};
    std::atomic<int>      receive_fd_{-1};      ///< Linux only; -1 on Windows or if not retrieved
    std::atomic<void*>    receive_handle_{nullptr}; ///< Windows only; HANDLE for WaitForMultipleObjects

    /// Sideband wakeup channel selected on by receive() alongside the
    /// PCAN receive event. close() signals it so a blocked worker
    /// returns promptly instead of waiting out its full timeout, matching
    /// SocketCanBackend's behavior. On Linux it's an eventfd; on Windows
    /// it's a manual-reset(*) Win32 event handle. (*) auto-reset would
    /// work too but manual-reset means re-entry in close() observes the
    /// signaled state — extra defensive against tear-down races.
    std::atomic<int>   shutdown_eventfd_{-1};      ///< Linux only
    std::atomic<void*> shutdown_event_handle_{nullptr}; ///< Windows only

    ChannelConfig config_;
    AdapterInfo info_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<uint64_t> rx_overruns_{0};
};

} // namespace can

#endif // CAN_PCAN_BACKEND_H
