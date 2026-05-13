/**
 * @file frame.h
 * @brief CAN frame data structure
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include "can/can_types.h"
#include "can/libcan_export.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace can {

/// Holds either a classic CAN frame (up to 8 data bytes) or a CAN-FD
/// frame (up to 64 data bytes). The `dlc` field carries the data length
/// as a byte count, not the on-wire 4-bit FD DLC code; use the helpers
/// in can_types.h (fdDlcToLength / lengthToFdDlc) when interfacing with
/// driver APIs that expose the raw 4-bit code.
struct LIBCAN_EXPORT Frame {
    uint32_t id = 0;                          ///< CAN identifier (11-bit or 29-bit)
    uint8_t  dlc = 0;                         ///< Data length as byte count (0..8 classic, 0..64 FD)
    std::array<uint8_t, MAX_CAN_FD_DLC> data{}; ///< Data bytes (zero-initialized)
    uint64_t timestamp_us = 0;                ///< Timestamp in microseconds (backend-defined epoch)
    bool is_error_frame = false;
    bool is_remote_frame = false;
    bool is_extended_id = false;
    bool is_fd_frame = false;
    bool is_tx = false;                       ///< True for frames echoed by the backend after we sent them
    bool is_brs = false;                      ///< CAN-FD bit-rate switch (data phase at higher bitrate)
    bool is_esi = false;                      ///< CAN-FD error state indicator (sender is error-passive)

    Frame() = default;

    /// Construct a frame with a CAN id and a payload. Caller's payload
    /// is copied; len is clamped to MAX_CAN_FD_DLC (64) bytes.
    ///
    /// If len > 8, the frame is automatically tagged is_fd_frame=true —
    /// otherwise the backend's send() would silently truncate the payload
    /// to 8 bytes (the classic-CAN max) since the frame would still look
    /// like a classic frame to the wire path. Callers that want a classic
    /// CAN frame with an FD-sized payload (which is invalid anyway) need
    /// to set is_fd_frame explicitly after construction.
    Frame(uint32_t can_id, const uint8_t* payload, uint8_t len)
        : id(can_id),
          dlc(len > MAX_CAN_FD_DLC ? static_cast<uint8_t>(MAX_CAN_FD_DLC) : len),
          is_extended_id(isExtId(can_id)),
          is_fd_frame(len > MAX_CAN_DLC)
    {
        if (payload && dlc > 0) {
            std::memcpy(data.data(), payload, dlc);
        }
    }

    /// Length in bytes. Identical to `dlc`; provided for readability
    /// at call sites that explicitly want the byte count rather than
    /// the DLC field name.
    uint8_t length() const { return dlc; }

    /// On-wire 4-bit FD DLC code corresponding to this frame's length.
    /// For classic CAN this equals `dlc`. For FD lengths above 8 the
    /// nearest-greater valid length is selected when `dlc` is not a
    /// valid FD length (e.g. 13 -> code 13 for 32 bytes).
    uint8_t fdDlcCode() const { return lengthToFdDlc(dlc); }

    bool isValid() const {
        return is_fd_frame ? isValidFdLength(dlc) : (dlc <= MAX_CAN_DLC);
    }
};

} // namespace can

#endif
