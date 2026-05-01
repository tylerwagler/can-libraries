/**
 * @file can_types.h
 * @brief Common types and constants for CAN communication
 *
 * Defines standard types, constants, and helper functions for CAN
 * frame handling compatible with Linux SocketCAN.
 *
 * @copyright Copyright (c) 2024 AVL Test Equipment
 * @license MIT License
 */

#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <cstdint>
#include <string>

namespace can {

/**
 * @brief Maximum data length for classic CAN frames
 */
static constexpr size_t MAX_CAN_DLC = 8;

/**
 * @brief Maximum data length for CAN FD frames
 */
static constexpr size_t MAX_CAN_FD_DLC = 64;

/**
 * @brief CAN identifier mask for standard (11-bit) IDs
 */
static constexpr uint32_t CAN_STD_ID_MASK = 0x7FF;

/**
 * @brief CAN identifier mask for extended (29-bit) IDs
 */
static constexpr uint32_t CAN_EXT_ID_MASK = 0x1FFFFFFF;

/**
 * @brief Error codes for CAN operations
 */
enum class ErrorCode {
    SUCCESS = 0,
    INVALID_INTERFACE,
    INTERFACE_NOT_UP,
    SOCKET_ERROR,
    INVALID_BITRATE,
    TIMEOUT,
    TRANSMIT_ERROR,
    RECEIVE_ERROR,
    INVALID_FRAME,
    NOT_OPEN,
    ALREADY_OPEN,
    THREAD_ERROR,
    UNKNOWN_ERROR
};

/**
 * @brief Convert error code to string
 * @param code Error code to convert
 * @return Human-readable error string
 */
inline std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:          return "Success";
        case ErrorCode::INVALID_INTERFACE: return "Invalid interface name";
        case ErrorCode::INTERFACE_NOT_UP:  return "Interface is not up";
        case ErrorCode::SOCKET_ERROR:      return "Socket creation failed";
        case ErrorCode::INVALID_BITRATE:   return "Invalid bitrate value";
        case ErrorCode::TIMEOUT:           return "Operation timed out";
        case ErrorCode::TRANSMIT_ERROR:    return "Transmission error";
        case ErrorCode::RECEIVE_ERROR:     return "Reception error";
        case ErrorCode::INVALID_FRAME:     return "Invalid frame format";
        case ErrorCode::NOT_OPEN:          return "Interface not open";
        case ErrorCode::ALREADY_OPEN:      return "Interface already open";
        case ErrorCode::THREAD_ERROR:      return "Thread creation failed";
        case ErrorCode::UNKNOWN_ERROR:     return "Unknown error";
        default:                           return "Unknown error";
    }
}

/**
 * @brief Check if DLC is valid for classic CAN
 * @param dlc Data Length Code
 * @return true if valid (0-8)
 */
inline bool isValidDlc(uint8_t dlc) {
    return dlc <= MAX_CAN_DLC;
}

/**
 * @brief Check if identifier is valid for standard CAN
 * @param id CAN identifier
 * @return true if 11-bit ID is valid
 */
inline bool isStdId(uint32_t id) {
    return (id & ~CAN_STD_ID_MASK) == 0;
}

/**
 * @brief Check if identifier requires extended format
 * @param id CAN identifier
 * @return true if 29-bit extended ID is needed
 */
inline bool isExtId(uint32_t id) {
    return (id & ~CAN_EXT_ID_MASK) != 0;
}

/**
 * @brief Get bitrate in bits per second
 * @param bitrate String representation (e.g., "500k", "1M")
 * @return Bitrate value or 0 on error
 *
 * Supports formats: "125k", "250k", "500k", "1M", "1000000"
 */
uint32_t parseBitrate(const std::string& bitrate);

/**
 * @brief Format bitrate as human-readable string
 * @param bitrate Bitrate in bits per second
 * @return Formatted string (e.g., "500 kbps", "1 Mbps")
 */
std::string formatBitrate(uint32_t bitrate);

} // namespace can

#endif // CAN_TYPES_H
