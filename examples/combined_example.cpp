/**
 * @file combined_example.cpp
 * @brief Combined example showing CAN interface with DBC decoding
 *
 * This example demonstrates how to use both libraries together:
 * - Receive CAN frames
 * - Decode them using DBC definitions
 * - Process decoded signal values
 *
 * Compile: g++ -std=c++17 combined_example.cpp -lcan -ldbc -lpthread
 */

#include "can/can_interface.h"
#include "dbc/dbc_parser.h"

#include <iostream>
#include <memory>
#include <atomic>

using namespace can;
using namespace dbc;

// Global flag for graceful shutdown
static std::atomic<bool> running{true};

/**
 * @brief Combined handler that receives CAN frames and decodes them
 */
class FrameHandler {
public:
    explicit FrameHandler(std::shared_ptr<Parser> dbc_parser)
        : parser_(std::move(dbc_parser)) {}

    void onFrameReceived(const Frame& frame) {
        // Decode the frame using DBC
        auto decoded = parser_->decodeFrame(frame.id, frame.data, frame.dlc,
                                            frame.timestamp_us);

        if (decoded.valid) {
            std::cout << "[" << std::dec << frame.timestamp_us << "] "
                      << decoded.name << " (0x" << std::hex << decoded.id << std::dec << "):";

            for (const auto& signal : decoded.signals) {
                std::cout << " " << signal.name << "=" << signal.value;
                if (!signal.unit.empty()) {
                    std::cout << signal.unit;
                }
                if (!signal.value_name.empty()) {
                    std::cout << "(" << signal.value_name << ")";
                }
            }
            std::cout << std::endl;
        } else {
            std::cout << "[" << std::dec << frame.timestamp_us << "] "
                      << "Unknown message 0x" << std::hex << frame.id << std::dec << std::endl;
        }
    }

private:
    std::shared_ptr<Parser> parser_;
};

int main() {
    std::cout << "=== Combined CAN + DBC Example ===" << std::endl;
    std::cout << std::endl;

    // Step 1: Load DBC file
    std::cout << "Loading DBC definitions..." << std::endl;
    
    auto parser = std::make_shared<Parser>();
    
    // Parse sample DBC content (in real usage, load from file)
    std::string dbc_content = R"(
VERSION ""
NS_ : BS_: BU_: SMCU BCU

BO_ 256 SMCU_Stat: 8 SMCU
 SG_ Battery_Status : 0|4@1+ (1,0) [0|15] "" SMCU
 SG_ Error_Code : 4|4@1+ (1,0) [0|15] "" SMCU
 SG_ Temperature : 8|8@1+ (0.1,-40) [-40|21.5] "C" SMCU
 SG_ Voltage : 16|16@1+ (0.01,0) [0|655.35] "V" SMCU
 SG_ Current : 32|16@1+ (0.01,-327.68) [-327.68|327.67] "A" SMCU
 SG_ State_of_Charge : 48|8@1+ (1,0) [0|100] "%" SMCU

VAL_ Battery_Status 0 "Discharging" 1 "Charging" 2 "Idle" 3 "Fault" ;
)";

    if (!parser->parseString(dbc_content)) {
        std::cerr << "Failed to parse DBC!" << std::endl;
        return 1;
    }

    std::cout << "Loaded " << parser->getAllMessages().size() << " messages" << std::endl;
    std::cout << std::endl;

    // Step 2: Create frame handler
    FrameHandler handler(parser);

    // Step 3: Create and configure CAN interface
    Interface can;
    
    // Set up frame callback
    can.setFrameCallback([&handler](const Frame& frame) {
        handler.onFrameReceived(frame);
    });

    can.setErrorCallback([](const std::string& error) {
        std::cerr << "CAN Error: " << error << std::endl;
    });

    // Step 4: Open interface (using vcan0 for testing)
    std::cout << "Opening CAN interface (vcan0)..." << std::endl;
    
    if (!can.open("vcan0", 500000)) {
        std::cerr << "Failed to open vcan0. Creating virtual interface..." << std::endl;
        std::cerr << "Run: sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0" << std::endl;
        std::cerr << std::endl;
        
        // For demonstration, show what would happen with data
        std::cout << "=== Simulated Frame Processing ===" << std::endl;
        
        uint8_t sample_data[8] = {0x12, 0x80, 0x90, 0x01, 0xF4, 0x00, 0x50, 0x00};
        auto decoded = parser->decodeFrame(256, sample_data, 8);
        
        if (decoded.valid) {
            std::cout << "Decoded " << decoded.name << ":" << std::endl;
            for (const auto& sig : decoded.signals) {
                std::cout << "  " << sig.name << " = " << sig.value;
                if (!sig.unit.empty()) std::cout << " " << sig.unit;
                if (!sig.value_name.empty()) std::cout << " (" << sig.value_name << ")";
                std::cout << std::endl;
            }
        }
        
        return 0;
    }

    std::cout << "Interface opened!" << std::endl;
    std::cout << std::endl;

    // Step 5: Start receive thread
    can.startReceiveThread();

    // Step 6: Send some test frames
    std::cout << "Sending test frames..." << std::endl;
    
    // Frame 1: Battery charging
    uint8_t data1[8] = {0x12, 0x80, 0x90, 0x01, 0xF4, 0x00, 0x50, 0x00};
    Frame frame1(256, data1, 8);
    can.send(frame1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Frame 2: Different status
    uint8_t data2[8] = {0x22, 0x00, 0x64, 0x01, 0x00, 0x00, 0x45, 0x00};
    Frame frame2(256, data2, 8);
    can.send(frame2);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Step 7: Cleanup
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;
    
    can.stopReceiveThread();
    can.close();

    std::cout << "Final statistics:" << std::endl;
    std::cout << "  TX Frames: " << can.getTxFrameCount() << std::endl;
    std::cout << "  RX Frames: " << can.getRxFrameCount() << std::endl;

    std::cout << std::endl;
    std::cout << "=== Example completed successfully! ===" << std::endl;

    return 0;
}
