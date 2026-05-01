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
 * @license MIT License
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
