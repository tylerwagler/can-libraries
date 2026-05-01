/**
 * @file can_interface.h
 * @brief Linux CAN socket interface for real-time communication
 *
 * This header defines the CAN Interface class for Linux SocketCAN.
 * Provides thread-safe, event-driven CAN communication with callback support.
 *
 * @copyright Copyright (c) 2024 AVL Test Equipment
 * @license MIT License
 */

#ifndef CAN_INTERFACE_H
#define CAN_INTERFACE_H

#include "can/libcan_export.h"

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>

namespace can {

/**
 * @brief CAN frame structure matching Linux SocketCAN format
 *
 * Represents a complete CAN frame including ID, data, and metadata.
 * Compatible with struct can_frame from <linux/can.h>
 */
struct LIBCAN_EXPORT Frame {
    uint32_t id;                  ///< CAN identifier (11-bit or 29-bit)
    uint8_t dlc;                  ///< Data Length Code (0-8 for classic CAN, up to 64 for CAN FD)
    uint8_t data[8];              ///< Data bytes (max 8 for classic CAN)
    uint64_t timestamp_us;        ///< Timestamp in microseconds
    bool is_error_frame;          ///< True if this is an error frame
    bool is_remote_frame;         ///< True if this is a remote frame
    bool is_extended_id;          ///< True if 29-bit extended identifier
    bool is_fd_frame;             ///< True if CAN FD frame

    /**
     * @brief Default constructor - creates empty frame
     */
    Frame();

    /**
     * @brief Construct frame with ID and data
     * @param can_id CAN identifier
     * @param data Pointer to data bytes
     * @param len Data length (max 8 for classic CAN)
     */
    Frame(uint32_t can_id, const uint8_t* data, uint8_t len);

    /**
     * @brief Get data length in bytes
     * @return Number of data bytes
     */
    size_t getDataLength() const { return dlc; }

    /**
     * @brief Check if frame has valid data
     * @return true if dlc is between 0 and 8
     */
    bool isValid() const;
};

/**
 * @brief CAN Interface class for Linux SocketCAN
 *
 * Provides a high-level, thread-safe interface to Linux CAN sockets.
 * Supports callback-based event handling, statistics tracking, and
 * configurable filtering.
 *
 * @example
 * can::Interface can;
 * can.setFrameCallback([](const can::Frame& frame) {
 *     std::cout << "Received frame 0x" << std::hex << frame.id << std::endl;
 * });
 *
 * if (can.open("vcan0", 500000)) {
 *     can::Frame frame(0x123, data, 8);
 *     can.send(frame);
 * }
 */
class LIBCAN_EXPORT Interface {
public:
    /**
     * @brief Callback type for received frames
     * @param frame The received CAN frame
     */
    using FrameCallback = std::function<void(const Frame&)>;

    /**
     * @brief Callback type for error events
     * @param error Error description string
     */
    using ErrorCallback = std::function<void(const std::string&)>;

    /**
     * @brief Callback type for state changes
     * @param is_connected True if interface is now connected
     */
    using StateCallback = std::function<void(bool is_connected)>;

    /**
     * @brief Constructor
     */
    Interface();

    /**
     * @brief Destructor - automatically closes socket if open
     */
    ~Interface();

    // Non-copyable
    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;

    // Movable
    Interface(Interface&& other) noexcept;
    Interface& operator=(Interface&& other) noexcept;

    /**
     * @brief Open CAN interface
     * @param interface_name Name of CAN interface (e.g., "can0", "vcan0")
     * @param bitrate Bitrate in bits per second (e.g., 500000 for 500kbps)
     * @return true if successful, false on error
     *
     * Opens the specified CAN interface with the given bitrate.
     * The interface must exist and be up (ip link set can0 up).
     */
    bool open(const std::string& interface_name, uint32_t bitrate = 500000);

    /**
     * @brief Close the CAN interface
     *
     * Closes the socket and stops any background processing.
     * Callbacks will not be invoked after closing.
     */
    void close();

    /**
     * @brief Check if interface is currently open
     * @return true if interface is open and ready
     */
    bool isOpen() const;

    /**
     * @brief Send a CAN frame
     * @param frame Frame to send
     * @return true if frame was sent successfully
     *
     * Sends the frame to the CAN bus. Blocks until the frame
     * is transmitted or an error occurs.
     */
    bool send(const Frame& frame);

    /**
     * @brief Receive a CAN frame
     * @param frame Reference to store received frame
     * @param blocking If true, block until frame received
     * @return true if frame was received, false on timeout/error
     *
     * When blocking is false, returns immediately if no frame
     * is available.
     */
    bool receive(Frame& frame, bool blocking = false);

    /**
     * @brief Set callback for received frames
     * @param callback Function to call when frame received
     *
     * The callback will be invoked from the receive thread
     * for each received frame.
     */
    void setFrameCallback(FrameCallback callback);

    /**
     * @brief Set callback for error events
     * @param callback Function to call on errors
     */
    void setErrorCallback(ErrorCallback callback);

    /**
     * @brief Set callback for connection state changes
     * @param callback Function to call on state change
     */
    void setStateCallback(StateCallback callback);

    /**
     * @brief Get transmitted frame count (thread-safe)
     * @return Number of frames transmitted
     */
    uint64_t getTxFrameCount() const;

    /**
     * @brief Get received frame count (thread-safe)
     * @return Number of frames received
     */
    uint64_t getRxFrameCount() const;

    /**
     * @brief Get error frame count (thread-safe)
     * @return Number of error frames
     */
    uint64_t getErrorFrameCount() const;

    /**
     * @brief Get bus load percentage (thread-safe)
     * @return Bus load as percentage (0.0 - 100.0)
     *
     * Calculates bus load based on frames transmitted
     * in the last second window.
     */
    double getBusLoadPercent() const;

    /**
     * @brief Reset all statistics
     *
     * Clears frame counts and resets bus load calculation.
     */
    void resetStatistics();

    /**
     * @brief Enable or disable non-blocking I/O
     * @param enable True to enable non-blocking mode
     *
     * In non-blocking mode, receive() returns immediately
     * if no frames are available.
     */
    void setNonBlocking(bool enable);

    /**
     * @brief Enable or disable remote frame reception
     * @param enable True to receive remote frames
     */
    void enableRemoteFrames(bool enable);

    /**
     * @brief Enable or disable error frame reception
     * @param enable True to receive error frames
     */
    void enableErrorFrames(bool enable);

    /**
     * @brief Set receive timeout in milliseconds
     * @param timeout_ms Timeout value (0 = infinite blocking)
     */
    void setReceiveTimeout(int timeout_ms);

    /**
     * @brief Get interface name
     * @return Name of the CAN interface
     */
    const std::string& getInterfaceName() const;

    /**
     * @brief Get socket file descriptor
     * @return Socket FD or -1 if not open
     *
     * Useful for integration with external event loops.
     */
    int getSocketFd() const;

    /**
     * @brief Start background receive thread
     *
     * Starts a dedicated thread that continuously receives
     * frames and invokes the frame callback.
     *
     * @return true if thread started successfully
     */
    bool startReceiveThread();

    /**
     * @brief Stop the background receive thread
     *
     * Waits for the thread to finish processing.
     */
    void stopReceiveThread();

    /**
     * @brief Check if receive thread is running
     * @return true if receive thread is active
     */
    bool isReceiveThreadRunning() const;

    /**
     * @brief Get last error message
     * @return Last error string or empty if no error
     */
    std::string getLastError() const;

private:
    // Socket and interface state
    int socket_fd_;
    std::string interface_name_;
    std::atomic<bool> is_open_;
    std::atomic<bool> non_blocking_;

    // Callbacks
    std::mutex callback_mutex_;
    FrameCallback frame_callback_;
    ErrorCallback error_callback_;
    StateCallback state_callback_;

    // Statistics
    mutable std::mutex stats_mutex_;
    uint64_t tx_frame_count_;
    uint64_t rx_frame_count_;
    uint64_t error_frame_count_;
    std::vector<uint64_t> tx_timestamps_;
    std::vector<uint64_t> rx_timestamps_;

    // Receive thread
    std::atomic<bool> receive_thread_running_;
    std::unique_ptr<std::thread> receive_thread_;

    // Configuration
    int receive_timeout_ms_;
    bool receive_remote_frames_;
    bool receive_error_frames_;

    // Error handling
    mutable std::mutex error_mutex_;
    std::string last_error_;

    // Constants
    static constexpr int BUS_LOAD_WINDOW_US = 1000000; // 1 second

    // Internal methods
    bool configureSocket();
    void receiveLoop();
    void updateBusLoad();
    void reportError(const std::string& error);
};

} // namespace can

#endif // CAN_INTERFACE_H
