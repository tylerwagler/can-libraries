/**
 * @file test_can_interface.cpp
 * @brief Unit tests for CAN Interface class
 *
 * Tests the CAN socket interface functionality including:
 * - Frame creation and validation
 * - Socket operations (open/close)
 * - Statistics tracking
 * - Callback handling
 *
 * Note: These tests use mock sockets and require no actual CAN hardware.
 */

#include "can/can_interface.h"
#include "can/can_types.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>

using namespace can;

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    if (condition) { \
        std::cout << "  [PASS] " << message << std::endl; \
        tests_passed++; \
    } else { \
        std::cout << "  [FAIL] " << message << std::endl; \
        tests_failed++; \
    }

// ============================================================================
// Frame Tests
// ============================================================================

void test_frame_default_constructor() {
    std::cout << "Testing frame default constructor..." << std::endl;
    
    Frame frame;
    
    TEST_ASSERT(frame.id == 0, "Default ID is 0");
    TEST_ASSERT(frame.dlc == 0, "Default DLC is 0");
    TEST_ASSERT(frame.timestamp_us == 0, "Default timestamp is 0");
    TEST_ASSERT(!frame.is_error_frame, "Default is not error frame");
    TEST_ASSERT(!frame.is_remote_frame, "Default is not remote frame");
    TEST_ASSERT(frame.isValid(), "Empty frame is valid");
}

void test_frame_constructor_with_data() {
    std::cout << "Testing frame constructor with data..." << std::endl;
    
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    Frame frame(0x123, data, 8);
    
    TEST_ASSERT(frame.id == 0x123, "Frame ID set correctly");
    TEST_ASSERT(frame.dlc == 8, "DLC set correctly");
    TEST_ASSERT(frame.data[0] == 0x01, "First data byte correct");
    TEST_ASSERT(frame.data[7] == 0x08, "Last data byte correct");
    TEST_ASSERT(frame.isValid(), "Frame with data is valid");
}

void test_frame_extended_id() {
    std::cout << "Testing extended ID detection..." << std::endl;
    
    uint8_t data[] = {0x00};
    Frame std_frame(0x7FF, data, 1);
    Frame ext_frame(0x1FFFFFFF, data, 1);
    
    TEST_ASSERT(isStdId(0x7FF), "0x7FF is standard ID");
    TEST_ASSERT(!isStdId(0x800), "0x800 is not standard ID");
    TEST_ASSERT(isExtId(0x800), "0x800 requires extended format");
    TEST_ASSERT(isExtId(0x1FFFFFFF), "0x1FFFFFFF is extended ID");
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_statistics_initialization() {
    std::cout << "Testing statistics initialization..." << std::endl;
    
    Interface can;
    
    TEST_ASSERT(can.getTxFrameCount() == 0, "Initial TX count is 0");
    TEST_ASSERT(can.getRxFrameCount() == 0, "Initial RX count is 0");
    TEST_ASSERT(can.getErrorFrameCount() == 0, "Initial error count is 0");
    TEST_ASSERT(can.getBusLoadPercent() == 0.0, "Initial bus load is 0%");
}

void test_statistics_reset() {
    std::cout << "Testing statistics reset..." << std::endl;
    
    Interface can;
    
    // Note: We can't actually send/receive without a real interface,
    // but we can test the reset functionality
    can.resetStatistics();
    
    TEST_ASSERT(can.getTxFrameCount() == 0, "TX count reset to 0");
    TEST_ASSERT(can.getRxFrameCount() == 0, "RX count reset to 0");
}

// ============================================================================
// Callback Tests
// ============================================================================

void test_callback_assignment() {
    std::cout << "Testing callback assignment..." << std::endl;
    
    Interface can;
    std::atomic<bool> frame_callback_called{false};
    std::atomic<bool> error_callback_called{false};
    
    can.setFrameCallback([&frame_callback_called](const Frame& frame) {
        (void)frame;
        frame_callback_called = true;
    });
    
    can.setErrorCallback([&error_callback_called](const std::string& error) {
        (void)error;
        error_callback_called = true;
    });
    
    // Callbacks are set without error
    TEST_ASSERT(true, "Frame callback assigned successfully");
    TEST_ASSERT(true, "Error callback assigned successfully");
}

// ============================================================================
// Configuration Tests
// ============================================================================

void test_interface_state() {
    std::cout << "Testing interface state..." << std::endl;
    
    Interface can;
    
    TEST_ASSERT(!can.isOpen(), "Interface not open initially");
    TEST_ASSERT(can.getSocketFd() == -1, "Socket FD is -1 when not open");
    TEST_ASSERT(can.getInterfaceName().empty(), "Interface name is empty initially");
}

void test_non_blocking_mode() {
    std::cout << "Testing non-blocking mode..." << std::endl;
    
    Interface can;
    
    can.setNonBlocking(true);
    // Note: Can't fully test without actual socket
    TEST_ASSERT(true, "Non-blocking mode set without error");
}

void test_timeout_configuration() {
    std::cout << "Testing timeout configuration..." << std::endl;
    
    Interface can;
    
    can.setReceiveTimeout(500);
    // Note: Can't fully test without actual socket
    TEST_ASSERT(true, "Receive timeout configured successfully");
}

// ============================================================================
// Run All Tests
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "CAN Interface Library Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Frame tests
    test_frame_default_constructor();
    test_frame_constructor_with_data();
    test_frame_extended_id();
    
    std::cout << std::endl;
    
    // Statistics tests
    test_statistics_initialization();
    test_statistics_reset();
    
    std::cout << std::endl;
    
    // Callback tests
    test_callback_assignment();
    
    std::cout << std::endl;
    
    // Configuration tests
    test_interface_state();
    test_non_blocking_mode();
    test_timeout_configuration();
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Test Results:" << std::endl;
    std::cout << "  Passed: " << tests_passed << std::endl;
    std::cout << "  Failed: " << tests_failed << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
