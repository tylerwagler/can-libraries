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
    bool isOpen() const override { return channel_handle_ != 0; }

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

    /// PCAN channel handle (TPCANHandle). 0 means closed.
    /// Stored as plain integer so the public header doesn't need PCANBasic.h.
    uint16_t channel_handle_ = 0;
    int receive_fd_ = -1;          ///< Linux only; -1 on Windows or if not retrieved
    void* receive_handle_ = nullptr; ///< Windows only; HANDLE for WaitForSingleObject

    ChannelConfig config_;
    AdapterInfo info_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<uint64_t> rx_overruns_{0};
};

} // namespace can

#endif // CAN_PCAN_BACKEND_H
