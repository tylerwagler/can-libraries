/**
 * @file can_types.h
 * @brief Common constants and helpers for CAN identifier / bitrate handling
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace can {

/// Maximum data length for a classic CAN frame.
static constexpr size_t MAX_CAN_DLC = 8;

/// Maximum data length for a CAN-FD frame.
static constexpr size_t MAX_CAN_FD_DLC = 64;

/// Mask for an 11-bit standard CAN identifier.
static constexpr uint32_t CAN_STD_ID_MASK = 0x7FF;

/// Mask for a 29-bit extended CAN identifier.
static constexpr uint32_t CAN_EXT_ID_MASK = 0x1FFFFFFF;

/// True if dlc is in range for a classic CAN frame (0..8).
inline bool isValidDlc(uint8_t dlc) {
    return dlc <= MAX_CAN_DLC;
}

/// True if id fits in an 11-bit standard CAN identifier.
inline bool isStdId(uint32_t id) {
    return (id & ~CAN_STD_ID_MASK) == 0;
}

/// True if id requires the 29-bit extended frame format (i.e. is larger
/// than the 11-bit standard range).
inline bool isExtId(uint32_t id) {
    return id > CAN_STD_ID_MASK;
}

/// Parse a bitrate string into bits per second. Accepts "125k", "500K",
/// "1M", or a plain decimal integer. Returns 0 if unparseable.
inline uint32_t parseBitrate(const std::string& bitrate) {
    if (bitrate.empty()) return 0;
    char* end = nullptr;
    double v = std::strtod(bitrate.c_str(), &end);
    if (end == bitrate.c_str()) return 0;
    while (*end == ' ') ++end;
    if (*end == 'k' || *end == 'K') v *= 1000.0;
    else if (*end == 'M' || *end == 'm') v *= 1'000'000.0;
    return v < 0 ? 0 : static_cast<uint32_t>(v);
}

/// Format a bitrate as a human-readable string ("500 kbps", "1 Mbps").
inline std::string formatBitrate(uint32_t bitrate) {
    if (bitrate >= 1'000'000) {
        if (bitrate % 1'000'000 == 0) {
            return std::to_string(bitrate / 1'000'000) + " Mbps";
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3g Mbps", bitrate / 1'000'000.0);
        return buf;
    }
    if (bitrate >= 1000) {
        return std::to_string(bitrate / 1000) + " kbps";
    }
    return std::to_string(bitrate) + " bps";
}

} // namespace can

#endif
