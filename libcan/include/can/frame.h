/**
 * @file frame.h
 * @brief CAN frame data structure
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license MIT License
 */

#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include "can/can_types.h"
#include "can/libcan_export.h"

#include <cstdint>
#include <cstring>

namespace can {

/// CAN frame compatible with Linux SocketCAN's struct can_frame.
/// Holds either a classic CAN frame (up to 8 data bytes) or the metadata
/// for a CAN-FD frame; FD payloads larger than 8 bytes are not yet
/// represented here (template apps using CAN-FD should extend this).
struct LIBCAN_EXPORT Frame {
    uint32_t id = 0;                  ///< CAN identifier (11-bit or 29-bit)
    uint8_t  dlc = 0;                 ///< Data length code (0..8 classic CAN)
    uint8_t  data[8] = {};            ///< Data bytes
    uint64_t timestamp_us = 0;        ///< Timestamp in microseconds (backend-defined epoch)
    bool is_error_frame = false;
    bool is_remote_frame = false;
    bool is_extended_id = false;
    bool is_fd_frame = false;

    Frame() = default;

    /// Construct a frame with a CAN id and a payload. Caller's payload
    /// is copied; len is clamped to 8 bytes.
    Frame(uint32_t can_id, const uint8_t* payload, uint8_t len)
        : id(can_id),
          dlc(len > MAX_CAN_DLC ? static_cast<uint8_t>(MAX_CAN_DLC) : len),
          is_extended_id(isExtId(can_id))
    {
        if (payload && dlc > 0) {
            std::memcpy(data, payload, dlc);
        }
    }

    bool isValid() const { return dlc <= MAX_CAN_DLC; }
};

} // namespace can

#endif
