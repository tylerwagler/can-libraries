/**
 * @file test_backend.cpp
 * @brief Smoke tests for the ICanBackend abstraction
 *
 * Exercises the parts of the backend API that don't require hardware:
 * factory wiring, adapter enumeration, configuration validation. Real
 * open()/send()/receive() coverage lives in integration tests against
 * vcan0 (see backend_example).
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "can/i_can_backend.h"
#include "can/frame.h"
#include "can/can_types.h"

#include <cstdio>
#include <string>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (cond) { std::printf("  [PASS] %s\n", msg); ++g_passed; }      \
        else      { std::printf("  [FAIL] %s\n", msg); ++g_failed; }      \
    } while (0)

int main() {
    std::printf("=== libcan backend tests ===\n\n");

    std::printf("Frame defaults:\n");
    can::Frame f;
    CHECK(f.id == 0, "default id is 0");
    CHECK(f.dlc == 0, "default dlc is 0");
    CHECK(!f.is_extended_id, "default not extended");
    CHECK(f.isValid(), "default frame is valid");

    std::printf("\nFrame from payload:\n");
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    can::Frame g(0x123, payload, 4);
    CHECK(g.id == 0x123, "id set");
    CHECK(g.dlc == 4, "dlc set");
    CHECK(!g.is_extended_id, "11-bit id not flagged extended");
    CHECK(g.data[0] == 0xDE && g.data[3] == 0xEF, "payload copied");

    std::printf("\nExtended ID detection:\n");
    can::Frame ext(0x18FF50E5, payload, 4);
    CHECK(ext.is_extended_id, "29-bit id flagged extended");
    CHECK(can::isExtId(0x800), "0x800 needs extended format");
    CHECK(!can::isExtId(0x7FF), "0x7FF fits in standard");
    CHECK(can::isStdId(0x7FF), "0x7FF is a standard id");

    std::printf("\nCAN-FD frame fields:\n");
    can::Frame fd;
    CHECK(!fd.is_tx, "default is_tx false");
    CHECK(!fd.is_brs, "default is_brs false");
    CHECK(!fd.is_esi, "default is_esi false");
    CHECK(fd.data.size() == 64, "data array sized for 64-byte FD payload");
    CHECK(fd.timestamp_us == 0, "default timestamp_us zero");

    std::printf("\nFrame::length() and fdDlcCode():\n");
    can::Frame classic;
    classic.dlc = 8;
    CHECK(classic.length() == 8, "classic length == dlc");
    CHECK(classic.fdDlcCode() == 8, "classic dlc 8 -> fd code 8");

    can::Frame fd12;
    fd12.dlc = 12;
    fd12.is_fd_frame = true;
    CHECK(fd12.length() == 12, "FD 12 byte length");
    CHECK(fd12.fdDlcCode() == 9, "FD 12 byte -> code 9");

    can::Frame fd64;
    fd64.dlc = 64;
    fd64.is_fd_frame = true;
    CHECK(fd64.length() == 64, "FD 64 byte length");
    CHECK(fd64.fdDlcCode() == 15, "FD 64 byte -> code 15");

    std::printf("\nfdDlcToLength (round-trip on all 16 codes):\n");
    constexpr uint8_t expected[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    bool fd_decode_ok = true;
    for (uint8_t code = 0; code < 16; ++code) {
        if (can::fdDlcToLength(code) != expected[code]) {
            fd_decode_ok = false;
            std::printf("    code %u expected %u got %u\n",
                        code, expected[code], can::fdDlcToLength(code));
        }
    }
    CHECK(fd_decode_ok, "all 16 FD DLC codes decode to expected byte counts");
    CHECK(can::fdDlcToLength(16) == 0, "out-of-range FD code returns 0");

    std::printf("\nlengthToFdDlc (encode boundaries):\n");
    CHECK(can::lengthToFdDlc(0) == 0, "0 -> 0");
    CHECK(can::lengthToFdDlc(8) == 8, "8 -> 8");
    CHECK(can::lengthToFdDlc(9) == 9, "9 rounds up to code 9 (12 bytes)");
    CHECK(can::lengthToFdDlc(12) == 9, "12 -> 9");
    CHECK(can::lengthToFdDlc(13) == 10, "13 rounds up to code 10 (16 bytes)");
    CHECK(can::lengthToFdDlc(64) == 15, "64 -> 15");
    CHECK(can::lengthToFdDlc(100) == 15, ">64 saturates at 15");

    std::printf("\nisValidFdLength:\n");
    CHECK(can::isValidFdLength(8), "8 valid");
    CHECK(can::isValidFdLength(12), "12 valid");
    CHECK(!can::isValidFdLength(13), "13 not a valid FD length");
    CHECK(can::isValidFdLength(64), "64 valid");

    std::printf("\nisValid() FD-aware:\n");
    can::Frame f_classic_10;
    f_classic_10.dlc = 10;
    CHECK(!f_classic_10.isValid(), "classic frame with dlc=10 invalid");
    can::Frame f_fd_10;
    f_fd_10.dlc = 10;
    f_fd_10.is_fd_frame = true;
    CHECK(!f_fd_10.isValid(), "FD frame with dlc=10 invalid (not on the FD-allowed list)");
    can::Frame f_fd_12;
    f_fd_12.dlc = 12;
    f_fd_12.is_fd_frame = true;
    CHECK(f_fd_12.isValid(), "FD frame with dlc=12 valid");
    can::Frame f_fd_64;
    f_fd_64.dlc = 64;
    f_fd_64.is_fd_frame = true;
    CHECK(f_fd_64.isValid(), "FD frame with dlc=64 valid");

    std::printf("\nBitrate parsing:\n");
    CHECK(can::parseBitrate("500k") == 500'000, "parse 500k");
    CHECK(can::parseBitrate("1M") == 1'000'000, "parse 1M");
    CHECK(can::parseBitrate("250000") == 250'000, "parse plain decimal");
    CHECK(can::parseBitrate("") == 0, "parse empty");
    CHECK(can::parseBitrate("garbage") == 0, "parse garbage");

    std::printf("\nBitrate formatting:\n");
    CHECK(can::formatBitrate(1'000'000) == "1 Mbps", "format 1M");
    CHECK(can::formatBitrate(500'000) == "500 kbps", "format 500k");
    CHECK(can::formatBitrate(750) == "750 bps", "format <1k");

    std::printf("\nBackend factory:\n");
    auto kinds = can::ICanBackend::availableBackends();
    CHECK(!kinds.empty(), "at least one backend compiled in");
    for (auto k : kinds) {
        auto b = can::ICanBackend::create(k);
        std::string label = "factory creates " + can::backendKindToString(k);
        CHECK(b != nullptr, label.c_str());
        if (b) {
            CHECK(b->kind() == k, "backend reports its own kind");
        }
    }

    std::printf("\n========================================\n");
    std::printf("Test Results:\n");
    std::printf("  Passed: %d\n", g_passed);
    std::printf("  Failed: %d\n", g_failed);
    std::printf("========================================\n");
    return g_failed == 0 ? 0 : 1;
}
