/**
 * @file test_dbc_parser.cpp
 * @brief Unit tests for DBC Parser class
 *
 * Tests the DBC parsing functionality including:
 * - Message parsing
 * - Signal parsing
 * - Value table parsing
 * - Frame decoding
 * - Frame encoding
 */

#include "dbc/dbc_parser.h"

#include <iostream>
#include <cassert>
#include <cstring>

using namespace dbc;

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

// Sample DBC content for testing
static const std::string SAMPLE_DBC = R"(
VERSION ""

NS_ : 
	NS_DESC_
	CM_
	BA_DEF_
	BA_
	VAL_
	CAT_DEF_
	CAT_
	FILTER
	BA_DEF_DEF_
	EV_DATA_
	ENVVAR_DATA_
	SGTYPE_
	SGTYPE_VAL_
	BA_DEF_SGTYPE_
	BA_SGTYPE_
	SIG_TYPE_REF_
	VOLBAT_EV_
	ENVVAR_
	DATATYPE_

BS_:

BU_: SMCU BCU

BO_ 256 SMCU_Stat: 8 SMCU
 SG_ SMCU_Stat : 0|8@1+ (1,0) [0|255] "" SMCU
 SG_ SMCU_State : 8|8@1+ (1,0) [0|255] "" SMCU

BO_ 257 Cell_Voltages: 8 SMCU
 SG_ Cell1_Voltage : 0|16@1+ (0.001,0) [0|65.535] "V" SMCU
 SG_ Cell2_Voltage : 16|16@1+ (0.001,0) [0|65.535] "V" SMCU

BO_ 258 Temperature_Data: 8 SMCU
 SG_ Module_Temp : 0|8@1+ (0.1,-40) [-40|21.5] "°C" SMCU
 SG_ Sensor_Status : 8|4@1+ (1,0) [0|15] "" SMCU
 VAL_ Sensor_Status 0 "OK" 1 "Warning" 2 "Error" 3 "Not Connected" ;
)";

// ============================================================================
// Message Parsing Tests
// ============================================================================

void test_parse_message_definition() {
    std::cout << "Testing message definition parsing..." << std::endl;
    
    Parser parser;
    bool result = parser.parseString(SAMPLE_DBC);
    
    TEST_ASSERT(result, "DBC parsed successfully");
    
    // Check message count
    auto messages = parser.getAllMessages();
    TEST_ASSERT(messages.size() >= 3, "At least 3 messages parsed");
    
    // Check specific message
    const Message* msg = parser.getMessageById(256);
    TEST_ASSERT(msg != nullptr, "Message 256 found");
    TEST_ASSERT(msg != nullptr && msg->name == "SMCU_Stat", "Message name correct");
    TEST_ASSERT(msg != nullptr && msg->length == 8, "Message length correct");
}

void test_message_by_name() {
    std::cout << "Testing message lookup by name..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    const Message* msg = parser.getMessageByName("Cell_Voltages");
    TEST_ASSERT(msg != nullptr, "Message 'Cell_Voltages' found by name");
    TEST_ASSERT(msg != nullptr && msg->id == 257, "Message ID correct");
}

void test_message_signals() {
    std::cout << "Testing message signal extraction..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    const Message* msg = parser.getMessageById(257);
    TEST_ASSERT(msg != nullptr, "Message 257 found");
    
    TEST_ASSERT(msg != nullptr && msg->hasSignal("Cell1_Voltage"), "Signal 'Cell1_Voltage' exists");
    TEST_ASSERT(msg != nullptr && msg->hasSignal("Cell2_Voltage"), "Signal 'Cell2_Voltage' exists");
    TEST_ASSERT(msg != nullptr && !msg->hasSignal("NonExistent"), "Non-existent signal not found");
}

// ============================================================================
// Signal Parsing Tests
// ============================================================================

void test_signal_properties() {
    std::cout << "Testing signal property parsing..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    const Signal* sig = parser.getSignal(257, "Cell1_Voltage");
    TEST_ASSERT(sig != nullptr, "Signal found");
    
    TEST_ASSERT(sig != nullptr && sig->name == "Cell1_Voltage", "Signal name correct");
    TEST_ASSERT(sig != nullptr && sig->start_bit == 0, "Start bit correct");
    TEST_ASSERT(sig != nullptr && sig->length == 16, "Signal length correct");
    TEST_ASSERT(sig != nullptr && sig->is_little_endian, "Byte order correct (Intel)");
    TEST_ASSERT(sig != nullptr && sig->factor == 0.001, "Factor correct");
    TEST_ASSERT(sig != nullptr && sig->offset == 0.0, "Offset correct");
    TEST_ASSERT(sig != nullptr && sig->unit == "V", "Unit correct");
}

void test_signed_signal() {
    std::cout << "Testing signed signal parsing..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    const Signal* sig = parser.getSignal(258, "Module_Temp");
    TEST_ASSERT(sig != nullptr, "Signal found");
    
    TEST_ASSERT(sig != nullptr && sig->is_signed, "Signal is signed");
    TEST_ASSERT(sig != nullptr && sig->factor == 0.1, "Factor correct");
    TEST_ASSERT(sig != nullptr && sig->offset == -40.0, "Offset correct");
    TEST_ASSERT(sig != nullptr && sig->unit == "°C", "Unit correct");
}

// ============================================================================
// Value Table Tests
// ============================================================================

void test_value_table_parsing() {
    std::cout << "Testing value table parsing..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    std::string value_name = parser.getSignalValueName(258, "Sensor_Status", 0);
    TEST_ASSERT(value_name == "OK", "Value 0 maps to 'OK'");
    
    value_name = parser.getSignalValueName(258, "Sensor_Status", 1);
    TEST_ASSERT(value_name == "Warning", "Value 1 maps to 'Warning'");
    
    value_name = parser.getSignalValueName(258, "Sensor_Status", 2);
    TEST_ASSERT(value_name == "Error", "Value 2 maps to 'Error'");
    
    value_name = parser.getSignalValueName(258, "Sensor_Status", 3);
    TEST_ASSERT(value_name == "Not Connected", "Value 3 maps to 'Not Connected'");
}

// ============================================================================
// Frame Decoding Tests
// ============================================================================

void test_decode_unsigned_signal() {
    std::cout << "Testing unsigned signal decoding..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    // Create frame with Cell1_Voltage = 3.456V
    // 3.456 / 0.001 = 3456 = 0x0D80
    uint8_t data[8] = {0x80, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    auto decoded = parser.decodeFrame(257, data, 8);
    
    TEST_ASSERT(decoded.valid, "Frame decoded successfully");
    TEST_ASSERT(decoded.name == "Cell_Voltages", "Message name correct");
    
    // Find Cell1_Voltage in decoded signals
    bool found = false;
    double cell1_voltage = 0;
    for (const auto& sig : decoded.signals) {
        if (sig.name == "Cell1_Voltage") {
            found = true;
            cell1_voltage = sig.value;
            break;
        }
    }
    
    TEST_ASSERT(found, "Cell1_Voltage found in decoded message");
    TEST_ASSERT(cell1_voltage > 3.45 && cell1_voltage < 3.46, "Voltage value correct (3.456V)");
}

void test_decode_signed_signal() {
    std::cout << "Testing signed signal decoding..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    // Create frame with Module_Temp = 25.5°C
    // (25.5 - (-40)) / 0.1 = 655 = 0x028F
    uint8_t data[8] = {0x8F, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    auto decoded = parser.decodeFrame(258, data, 8);
    
    TEST_ASSERT(decoded.valid, "Frame decoded successfully");
    
    // Find Module_Temp in decoded signals
    bool found = false;
    double temp = 0;
    for (const auto& sig : decoded.signals) {
        if (sig.name == "Module_Temp") {
            found = true;
            temp = sig.value;
            break;
        }
    }
    
    TEST_ASSERT(found, "Module_Temp found in decoded message");
    TEST_ASSERT(temp > 25.4 && temp < 25.6, "Temperature value correct (25.5°C)");
}

void test_decode_with_value_table() {
    std::cout << "Testing decoding with value table..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    // Create frame with Sensor_Status = 2 (Error)
    uint8_t data[8] = {0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    auto decoded = parser.decodeFrame(258, data, 8);
    
    // Find Sensor_Status in decoded signals
    for (const auto& sig : decoded.signals) {
        if (sig.name == "Sensor_Status") {
            TEST_ASSERT(sig.value == 2.0, "Raw value is 2");
            TEST_ASSERT(sig.value_name == "Error", "Value name is 'Error'");
            break;
        }
    }
}

// ============================================================================
// Frame Encoding Tests
// ============================================================================

void test_encode_frame() {
    std::cout << "Testing frame encoding..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    uint8_t data[8];
    size_t dlc;
    
    std::map<std::string, double> signal_values;
    signal_values["Cell1_Voltage"] = 3.456;
    signal_values["Cell2_Voltage"] = 3.789;
    
    bool result = parser.encodeFrame(257, signal_values, data, dlc);
    
    TEST_ASSERT(result, "Frame encoded successfully");
    TEST_ASSERT(dlc == 8, "DLC is 8");
    
    // Verify Cell1_Voltage encoding (3.456V = 0x0D80)
    TEST_ASSERT(data[0] == 0x80, "Cell1_Voltage byte 0 correct");
    TEST_ASSERT(data[1] == 0x0D, "Cell1_Voltage byte 1 correct");
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_empty_dbc() {
    std::cout << "Testing empty DBC handling..." << std::endl;
    
    Parser parser;
    bool result = parser.parseString("");
    
    TEST_ASSERT(!result || parser.getAllMessages().empty(), "Empty DBC handled correctly");
}

void test_unknown_message_id() {
    std::cout << "Testing unknown message ID handling..." << std::endl;
    
    Parser parser;
    parser.parseString(SAMPLE_DBC);
    
    const Message* msg = parser.getMessageById(9999);
    TEST_ASSERT(msg == nullptr, "Unknown message returns nullptr");
}

void test_malformed_dbc() {
    std::cout << "Testing malformed DBC handling..." << std::endl;
    
    Parser parser;
    std::string malformed = "BO_ invalid";
    
    // Should not crash on malformed input
    bool result = parser.parseString(malformed);
    TEST_ASSERT(true, "Malformed DBC handled without crash");
}

// ============================================================================
// Run All Tests
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "DBC Parser Library Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Message parsing tests
    test_parse_message_definition();
    test_message_by_name();
    test_message_signals();
    
    std::cout << std::endl;
    
    // Signal parsing tests
    test_signal_properties();
    test_signed_signal();
    
    std::cout << std::endl;
    
    // Value table tests
    test_value_table_parsing();
    
    std::cout << std::endl;
    
    // Frame decoding tests
    test_decode_unsigned_signal();
    test_decode_signed_signal();
    test_decode_with_value_table();
    
    std::cout << std::endl;
    
    // Frame encoding tests
    test_encode_frame();
    
    std::cout << std::endl;
    
    // Edge case tests
    test_empty_dbc();
    test_unknown_message_id();
    test_malformed_dbc();
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Test Results:" << std::endl;
    std::cout << "  Passed: " << tests_passed << std::endl;
    std::cout << "  Failed: " << tests_failed << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
