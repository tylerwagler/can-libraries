/**
 * @file can_interface.cpp
 * @brief Linux CAN socket implementation
 *
 * Provides the implementation of the CAN Interface class for
 * Linux SocketCAN with thread-safe operations and callback support.
 *
 * @copyright Copyright (c) 2024 AVL Test Equipment
 * @license MIT License
 */

#include "can/can_interface.h"
#include "can/can_types.h"
#include "can/can_errors.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>

namespace can {

// ============================================================================
// Frame Implementation
// ============================================================================

Frame::Frame()
    : id(0)
    , dlc(0)
    , timestamp_us(0)
    , is_error_frame(false)
    , is_remote_frame(false)
    , is_extended_id(false)
    , is_fd_frame(false)
{
    std::memset(data, 0, sizeof(data));
}

Frame::Frame(uint32_t can_id, const uint8_t* data, uint8_t len)
    : id(can_id)
    , dlc(len > MAX_CAN_DLC ? MAX_CAN_DLC : len)
    , timestamp_us(0)
    , is_error_frame(false)
    , is_remote_frame(false)
    , is_extended_id(isExtId(can_id))
    , is_fd_frame(false)
{
    std::memset(this->data, 0, sizeof(this->data));
    if (data && len > 0) {
        std::memcpy(this->data, data, dlc);
    }
}

bool Frame::isValid() const {
    return dlc <= MAX_CAN_DLC;
}

// ============================================================================
// Interface Implementation
// ============================================================================

Interface::Interface()
    : socket_fd_(-1)
    , is_open_(false)
    , non_blocking_(false)
    , tx_frame_count_(0)
    , rx_frame_count_(0)
    , error_frame_count_(0)
    , receive_thread_running_(false)
    , receive_timeout_ms_(1000)
    , receive_remote_frames_(false)
    , receive_error_frames_(false)
{
}

Interface::~Interface() {
    close();
    stopReceiveThread();
}

Interface::Interface(Interface&& other) noexcept
    : socket_fd_(other.socket_fd_)
    , interface_name_(std::move(other.interface_name_))
    , is_open_(other.is_open_.load())
    , non_blocking_(other.non_blocking_.load())
    , frame_callback_(std::move(other.frame_callback_))
    , error_callback_(std::move(other.error_callback_))
    , state_callback_(std::move(other.state_callback_))
    , tx_frame_count_(0)
    , rx_frame_count_(0)
    , error_frame_count_(0)
    , receive_thread_running_(other.receive_thread_running_.load())
    , receive_timeout_ms_(other.receive_timeout_ms_)
    , receive_remote_frames_(other.receive_remote_frames_)
    , receive_error_frames_(other.receive_error_frames_)
{
    // Copy statistics with lock
    {
        std::lock_guard<std::mutex> lock(other.stats_mutex_);
        tx_frame_count_ = other.tx_frame_count_;
        rx_frame_count_ = other.rx_frame_count_;
        error_frame_count_ = other.error_frame_count_;
        tx_timestamps_ = other.tx_timestamps_;
        rx_timestamps_ = other.rx_timestamps_;
    }
    
    other.socket_fd_ = -1;
    other.is_open_ = false;
    other.receive_thread_running_ = false;
}

Interface& Interface::operator=(Interface&& other) noexcept {
    if (this != &other) {
        close();
        stopReceiveThread();

        socket_fd_ = other.socket_fd_;
        interface_name_ = std::move(other.interface_name_);
        is_open_ = other.is_open_.load();
        non_blocking_ = other.non_blocking_.load();
        frame_callback_ = std::move(other.frame_callback_);
        error_callback_ = std::move(other.error_callback_);
        state_callback_ = std::move(other.state_callback_);
        
        {
            std::lock_guard<std::mutex> lock(other.stats_mutex_);
            tx_frame_count_ = other.tx_frame_count_;
            rx_frame_count_ = other.rx_frame_count_;
            error_frame_count_ = other.error_frame_count_;
            tx_timestamps_ = other.tx_timestamps_;
            rx_timestamps_ = other.rx_timestamps_;
        }
        
        receive_thread_running_ = other.receive_thread_running_.load();
        receive_timeout_ms_ = other.receive_timeout_ms_;
        receive_remote_frames_ = other.receive_remote_frames_;
        receive_error_frames_ = other.receive_error_frames_;

        other.socket_fd_ = -1;
        other.is_open_ = false;
        other.receive_thread_running_ = false;
    }
    return *this;
}

bool Interface::open(const std::string& interface_name, uint32_t bitrate) {
    if (is_open_) {
        reportError("Interface already open");
        return false;
    }

    // Create socket
    socket_fd_ = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (socket_fd_ < 0) {
        reportError("Failed to create CAN socket: " + std::string(strerror(errno)));
        return false;
    }

    // Get interface index
    ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);

    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        reportError("Interface not found: " + interface_name);
        close();
        return false;
    }

    // Configure socket address
    sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    // Bind socket to interface
    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        reportError("Failed to bind socket to interface: " + std::string(strerror(errno)));
        close();
        return false;
    }

    // Configure raw socket filters
    if (!receive_remote_frames_) {
        struct can_filter filter;
        filter.can_id = 0;
        filter.can_mask = CAN_ERR_FLAG;
        setsockopt(socket_fd_, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter));
    }

    if (!receive_error_frames_) {
        int err = 0;
        setsockopt(socket_fd_, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err, sizeof(err));
    }

    interface_name_ = interface_name;
    is_open_ = true;

    // Report state change
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (state_callback_) {
            state_callback_(true);
        }
    }

    return true;
}

void Interface::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    bool was_open = is_open_.exchange(false);
    if (was_open) {
        // Report state change
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (state_callback_) {
            state_callback_(false);
        }
    }
}

bool Interface::isOpen() const {
    return is_open_.load();
}

bool Interface::send(const Frame& frame) {
    if (!is_open_) {
        reportError("Interface not open");
        return false;
    }

    if (!frame.isValid()) {
        reportError("Invalid frame DLC");
        return false;
    }

    // Build CAN frame
    struct can_frame can_frame;
    std::memset(&can_frame, 0, sizeof(can_frame));

    can_frame.can_id = frame.id;
    if (frame.is_extended_id) {
        can_frame.can_id |= CAN_EFF_FLAG;
    }
    if (frame.is_error_frame) {
        can_frame.can_id |= CAN_ERR_FLAG;
    }
    if (frame.is_remote_frame) {
        can_frame.can_id |= CAN_RTR_FLAG;
    }

    can_frame.can_dlc = frame.dlc;
    std::memcpy(can_frame.data, frame.data, frame.dlc);

    // Send frame
    ssize_t sent = write(socket_fd_, &can_frame, sizeof(can_frame));
    if (sent < 0) {
        reportError("Failed to send frame: " + std::string(strerror(errno)));
        return false;
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        tx_frame_count_++;
        tx_timestamps_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    return true;
}

bool Interface::receive(Frame& frame, bool blocking) {
    if (!is_open_) {
        reportError("Interface not open");
        return false;
    }

    // Prepare for select if blocking
    fd_set read_fds;
    struct timeval timeout;

    if (blocking || !non_blocking_) {
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);

        timeout.tv_sec = receive_timeout_ms_ / 1000;
        timeout.tv_usec = (receive_timeout_ms_ % 1000) * 1000;

        int result = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        if (result <= 0) {
            return false; // Timeout or error
        }
    }

    // Read frame
    struct can_frame can_frame;
    ssize_t bytes = read(socket_fd_, &can_frame, sizeof(can_frame));

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // No data available
        }
        reportError("Failed to receive frame: " + std::string(strerror(errno)));
        return false;
    }

    // Populate frame structure
    frame.id = can_frame.can_id & (CAN_EFF_FLAG ? CAN_EXT_ID_MASK : CAN_STD_ID_MASK);
    frame.dlc = can_frame.can_dlc;
    frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.is_error_frame = (can_frame.can_id & CAN_ERR_FLAG) != 0;
    frame.is_remote_frame = (can_frame.can_id & CAN_RTR_FLAG) != 0;
    frame.is_extended_id = (can_frame.can_id & CAN_EFF_FLAG) != 0;
    frame.is_fd_frame = false; // Not supported in basic SocketCAN

    std::memcpy(frame.data, can_frame.data, frame.dlc);

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        rx_frame_count_++;
        rx_timestamps_.push_back(frame.timestamp_us);
    }

    return true;
}

void Interface::setFrameCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = std::move(callback);
}

void Interface::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

void Interface::setStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    state_callback_ = std::move(callback);
}

uint64_t Interface::getTxFrameCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return tx_frame_count_;
}

uint64_t Interface::getRxFrameCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return rx_frame_count_;
}

uint64_t Interface::getErrorFrameCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return error_frame_count_;
}

double Interface::getBusLoadPercent() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Count frames in the last second
    size_t tx_count = 0;
    for (auto it = tx_timestamps_.rbegin(); it != tx_timestamps_.rend(); ++it) {
        if (now - *it > BUS_LOAD_WINDOW_US) {
            break;
        }
        tx_count++;
    }

    // Estimate bus load based on frame count
    // Average CAN frame is ~50 bits, at 500kbps = 8.33 microseconds per frame
    // 1 second = 1,000,000 microseconds
    // Max frames per second at 500kbps ≈ 12,000 frames
    const double max_frames_per_second = 12000.0;
    
    if (tx_count == 0) {
        return 0.0;
    }

    double load = (static_cast<double>(tx_count) / max_frames_per_second) * 100.0;
    return std::min(load, 100.0);
}

void Interface::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    tx_frame_count_ = 0;
    rx_frame_count_ = 0;
    error_frame_count_ = 0;
    tx_timestamps_.clear();
    rx_timestamps_.clear();
}

void Interface::setNonBlocking(bool enable) {
    non_blocking_ = enable;
    
    if (socket_fd_ >= 0) {
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (enable) {
            fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        } else {
            fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
}

void Interface::enableRemoteFrames(bool enable) {
    receive_remote_frames_ = enable;
    // Note: Requires reopening socket to apply filter changes
}

void Interface::enableErrorFrames(bool enable) {
    receive_error_frames_ = enable;
    // Note: Requires reopening socket to apply filter changes
}

void Interface::setReceiveTimeout(int timeout_ms) {
    receive_timeout_ms_ = timeout_ms;
}

const std::string& Interface::getInterfaceName() const {
    return interface_name_;
}

int Interface::getSocketFd() const {
    return socket_fd_;
}

bool Interface::startReceiveThread() {
    if (receive_thread_running_.load()) {
        return false;
    }

    receive_thread_running_ = true;
    receive_thread_ = std::make_unique<std::thread>(&Interface::receiveLoop, this);
    
    return true;
}

void Interface::stopReceiveThread() {
    receive_thread_running_ = false;

    if (receive_thread_ && receive_thread_->joinable()) {
        receive_thread_->join();
        receive_thread_.reset();
    }
}

bool Interface::isReceiveThreadRunning() const {
    return receive_thread_running_.load();
}

std::string Interface::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void Interface::receiveLoop() {
    while (receive_thread_running_.load()) {
        Frame frame;
        
        if (receive(frame, true)) {
            // Invoke callback
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (frame_callback_) {
                frame_callback_(frame);
            }
        }
    }
}

void Interface::reportError(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

} // namespace can
