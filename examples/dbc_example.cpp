/**
 * @file dbc_example.cpp
 * @brief Example demonstrating DBC parsing and frame decoding
 *
 * This example shows how to:
 * - Parse a DBC file
 * - Look up messages and signals
 * - Decode CAN frames using DBC definitions
 * - Encode signal values into CAN frames
 *
 * Compile: g++ -std=c++17 dbc_example.cpp -ldbc
 */

#include "dbc/dbc_parser.h"

#include <iostream>
#include <fstream>
#include <sstream>

using namespace dbc;

/**
 * @brief Print decoded message with all signal values
 */
void printDecodedMessage(const DecodedMessage& decoded) {
    std::cout << "Message: " << decoded.name << " (ID: 0x" << std::hex 
              << decoded.id << std::dec << ")" << std::endl;
    
    for (const auto& signal : decoded.signals) {
        std::cout << "  " << signal.name << " = " << signal.value;
        if (!signal.unit.empty()) {
            std::cout << " " << signal.unit;
        }
        if (!signal.value_name.empty()) {
            std::cout << " (" << signal.value_name << ")";
        }
        std::cout << std::dec << std::endl;
    }
}

/**
 * @brief Create a sample DBC content for demonstration
 */
std::string createSampleDBC() {
    return R"(
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
 SG_ Battery_Status : 0|4@1+ (1,0) [0|15] "" SMCU
 SG_ Error_Code : 4|4@1+ (1,0) [0|15] "" SMCU
 SG_ Temperature : 8|8@1+ (0.1,-40) [-40|21.5] "C" SMCU
 SG_ Voltage : 16|16@1+ (0.01,0) [0|655.35] "V" SMCU
 SG_ Current : 32|16@1+ (0.01,-327.68) [-327.68|327.67] "A" SMCU
 SG_ State_of_Charge : 48|8@1+ (1,0) [0|100] "%" SMCU

BO_ 512 Cell_Data: 8 SMCU
 SG_ Cell1_MV : 0|12@1+ (1,0) [0|4095] "mV" SMCU
 SG_ Cell2_MV : 12|12@1+ (1,0) [0|4095] "mV" SMCU
 SG_ Cell3_MV : 24|12@1+ (1,0) [0|4095] "mV" SMCU
 SG_ Cell4_MV : 36|12@1+ (1,0) [0|4095] "mV" SMCU

BU_VAL_ Sensor_Status 0 "OK" 1 "Warning" 2 "Error" 3 "Not Connected" ;

VAL_ Battery_Status 0 "Discharging" 1 "Charging" 2 "Idle" 3 "Fault" ;
)";
}

int main() {
    std::cout << "=== DBC Parser Example ===" << std::endl;
    std::cout << std::endl;

    // Create parser
    Parser parser;

    // Parse sample DBC content
    std::cout << "Parsing DBC content..." << std::endl;
    std::string dbc_content = createSampleDBC();
    
    if (!parser.parseString(dbc_content)) {
        std::cerr << "Failed to parse DBC content!" << std::endl;
        return 1;
    }
    std::cout << "DBC parsed successfully!" << std::endl;
    std::cout << std::endl;

    // List all messages
    std::cout << "Available messages:" << std::endl;
    auto messages = parser.getAllMessages();
    for (const auto& [id, msg] : messages) {
        std::cout << "  0x" << std::hex << id << std::dec << " - " << msg.name 
                  << " (" << msg.length << " bytes)" << std::endl;
    }
    std::cout << std::endl;

    // Get message by ID
    std::cout << "Looking up message by ID (256)..." << std::endl;
    const Message* msg = parser.getMessageById(256);
    if (msg) {
        std::cout << "  Found: " << msg->name << std::endl;
        std::cout << "  Transmitter: " << msg->transmitter << std::endl;
        std::cout << "  Signals: " << msg->signals.size() << std::endl;
        
        std::cout << "  Signal details:" << std::endl;
        for (const auto& [name, signal] : msg->signals) {
            std::cout << "    - " << name << ":" << std::endl;
            std::cout << "        Position: " << signal.start_bit << "|" << signal.length << std::endl;
            std::cout << "        Byte order: " << (signal.is_little_endian ? "Intel (LE)" : "Motorola (BE)") << std::endl;
            std::cout << "        Factor: " << signal.factor << ", Offset: " << signal.offset << std::endl;
            std::cout << "        Unit: " << signal.unit << std::endl;
        }
    }
    std::cout << std::endl;

    // Decode a sample frame
    std::cout << "Decoding sample CAN frame..." << std::endl;
    
    // Create a sample frame with known values
    // Battery_Status=1 (Charging), Error_Code=0, Temp=25.5C, Voltage=400.0V, Current=25.0A, SOC=80%
    uint8_t frame_data[8] = {
        0x12, // Bits 0-3: Status=1 (Charging), Bits 4-7: Error=0
        0x80, // Bits 8-15: Temperature = 655 -> 65.5 - 40 = 25.5C
        0x90, // Bits 16-23: Voltage low byte
        0x01, // Bits 16-23: Voltage high byte (0x0190 = 400 * 100 = 40000)
        0xF4, // Bits 24-31: Current low byte
        0x00, // Bits 24-31: Current high byte (0x00F4 = 25 * 100 = 2500)
        0x50, // Bits 32-39: SOC = 80
        0x00  // Padding
    };

    auto decoded = parser.decodeFrame(256, frame_data, 8);
    printDecodedMessage(decoded);
    std::cout << std::endl;

    // Decode another frame (Cell_Data)
    std::cout << "Decoding Cell_Data frame..." << std::endl;
    
    uint8_t cell_data[8] = {
        0x00, 0x10, // Cell1 = 0x1000 = 4096 mV
        0x40, 0x0F, // Cell2 = 0x0F40 = 3904 mV
        0x80, 0x0E, // Cell3 = 0x0E80 = 3712 mV
        0xC0, 0x0D  // Cell4 = 0x0DC0 = 3520 mV
    };

    auto cell_decoded = parser.decodeFrame(512, cell_data, 8);
    printDecodedMessage(cell_decoded);
    std::cout << std::endl;

    // Test value table lookup
    std::cout << "Value table lookups:" << std::endl;
    std::cout << "  Battery_Status 0 = " << parser.getSignalValueName(256, "Battery_Status", 0) << std::endl;
    std::cout << "  Battery_Status 1 = " << parser.getSignalValueName(256, "Battery_Status", 1) << std::endl;
    std::cout << "  Battery_Status 2 = " << parser.getSignalValueName(256, "Battery_Status", 2) << std::endl;
    std::cout << std::endl;

    // Encode a frame
    std::cout << "Encoding CAN frame from signal values..." << std::endl;
    
    std::map<std::string, double> signal_values;
    signal_values["Battery_Status"] = 1.0;    // Charging
    signal_values["Error_Code"] = 0.0;        // No error
    signal_values["Temperature"] = 30.5;      // 30.5 C
    signal_values["Voltage"] = 400.0;         // 400 V
    signal_values["Current"] = 50.0;          // 50 A
    signal_values["State_of_Charge"] = 85.0;  // 85%

    uint8_t encoded_data[8];
    size_t encoded_dlc;
    
    if (parser.encodeFrame(256, signal_values, encoded_data, encoded_dlc)) {
        std::cout << "  Frame encoded successfully!" << std::endl;
        std::cout << "  Data: ";
        for (size_t i = 0; i < encoded_dlc; ++i) {
            std::cout << "0x" << std::hex << static_cast<int>(encoded_data[i]) << " ";
        }
        std::cout << std::dec << std::endl;
        
        // Decode the encoded frame to verify
        std::cout << "  Verifying by decoding..." << std::endl;
        auto verify = parser.decodeFrame(256, encoded_data, encoded_dlc);
        printDecodedMessage(verify);
    } else {
        std::cerr << "  Failed to encode frame!" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "=== Example completed successfully! ===" << std::endl;

    return 0;
}
