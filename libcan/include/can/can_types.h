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
#include <limits>
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

/// True if a byte-count length is in range for a CAN-FD frame.
/// CAN-FD permits 0..8, 12, 16, 20, 24, 32, 48, 64; other values
/// above 8 are not encodable in the on-wire 4-bit DLC field.
inline bool isValidFdLength(uint8_t length) {
    if (length <= 8) return true;
    switch (length) {
        case 12: case 16: case 20: case 24:
        case 32: case 48: case 64:
            return true;
        default:
            return false;
    }
}

/// Decode the on-wire 4-bit CAN-FD DLC into a byte-count length.
/// For codes 0..8 the length equals the code; codes 9..15 expand to
/// 12, 16, 20, 24, 32, 48, 64. Codes >15 return 0.
inline uint8_t fdDlcToLength(uint8_t fd_dlc) {
    static constexpr uint8_t kLut[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return fd_dlc < 16 ? kLut[fd_dlc] : 0;
}

/// Encode a CAN-FD byte-count length as the on-wire 4-bit DLC code.
/// Invalid lengths (9..11, 13..15, 17..19, etc.) round up to the
/// next valid length. Lengths above 64 saturate to 64.
inline uint8_t lengthToFdDlc(uint8_t length) {
    if (length <= 8) return length;
    if (length <= 12) return 9;
    if (length <= 16) return 10;
    if (length <= 20) return 11;
    if (length <= 24) return 12;
    if (length <= 32) return 13;
    if (length <= 48) return 14;
    return 15;
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
/// "1M", or a plain decimal integer. Returns 0 if unparseable, negative,
/// or out of range for uint32_t — callers should treat 0 as "invalid"
/// (no real CAN bus runs at 0 bps).
inline uint32_t parseBitrate(const std::string& bitrate) {
    if (bitrate.empty()) return 0;
    char* end = nullptr;
    double v = std::strtod(bitrate.c_str(), &end);
    if (end == bitrate.c_str()) return 0;
    while (*end == ' ') ++end;
    if (*end == 'k' || *end == 'K') v *= 1000.0;
    else if (*end == 'M' || *end == 'm') v *= 1'000'000.0;
    // Clamp before cast: casting an out-of-range double to uint32_t is
    // undefined behavior pre-C++20 (implementation-defined since) and
    // can wrap, saturate, or produce garbage depending on the compiler.
    constexpr double kMax = static_cast<double>(std::numeric_limits<uint32_t>::max());
    if (v < 0.0 || v > kMax) return 0;
    return static_cast<uint32_t>(v);
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
