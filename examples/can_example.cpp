/**
 * @file can_example.cpp
 * @brief Simple example demonstrating CAN Interface usage
 *
 * This example shows how to:
 * - Open a CAN interface
 * - Set up callbacks for received frames
 * - Send and receive CAN frames
 * - Monitor statistics
 *
 * Compile: g++ -std=c++17 can_example.cpp -lcan -lpthread
 */

#include "can/can_interface.h"
#include "can/can_types.h"

#include <iostream>
#include <chrono>
#include <thread>

using namespace can;

// Global flag for graceful shutdown
static std::atomic<bool> running{true};

/**
 * @brief Callback for received CAN frames
 */
void onFrameReceived(const Frame& frame) {
    std::cout << "Received: ID=0x" << std::hex << frame.id 
              << std::dec << " DLC=" << static_cast<int>(frame.dlc)
              << " Data=";
    
    for (size_t i = 0; i < frame.dlc; ++i) {
        std::cout << std::hex << static_cast<int>(frame.data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

/**
 * @brief Callback for error events
 */
void onError(const std::string& error) {
    std::cerr << "Error: " << error << std::endl;
}

/**
 * @brief Callback for connection state changes
 */
void onStateChanged(bool connected) {
    std::cout << "Connection state: " << (connected ? "CONNECTED" : "DISCONNECTED") << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== CAN Interface Example ===" << std::endl;
    std::cout << std::endl;

    // Parse command line arguments
    std::string interface_name = "vcan0";  // Default to virtual CAN
    uint32_t bitrate = 500000;             // 500 kbps default

    if (argc > 1) {
        interface_name = argv[1];
    }
    if (argc > 2) {
        bitrate = std::stoul(argv[2]);
    }

    std::cout << "Interface: " << interface_name << std::endl;
    std::cout << "Bitrate: " << formatBitrate(bitrate) << std::endl;
    std::cout << std::endl;

    // Create CAN interface
    Interface can;

    // Set up callbacks
    can.setFrameCallback(onFrameReceived);
    can.setErrorCallback(onError);
    can.setStateCallback(onStateChanged);

    // Open the interface
    std::cout << "Opening interface..." << std::endl;
    if (!can.open(interface_name, bitrate)) {
        std::cerr << "Failed to open interface: " << interface_name << std::endl;
        std::cerr << "Last error: " << can.getLastError() << std::endl;
        std::cerr << std::endl;
        std::cerr << "Note: You may need to create a virtual CAN interface first:" << std::endl;
        std::cerr << "  sudo ip link add dev vcan0 type vcan" << std::endl;
        std::cerr << "  sudo ip link set up vcan0" << std::endl;
        return 1;
    }

    std::cout << "Interface opened successfully!" << std::endl;
    std::cout << std::endl;

    // Start background receive thread
    can.startReceiveThread();

    // Send some test frames
    std::cout << "Sending test frames..." << std::endl;
    for (int i = 0; i < 5 && running; ++i) {
        uint8_t data[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
        Frame frame(0x123, data, 8);
        frame.is_extended_id = false;

        if (can.send(frame)) {
            std::cout << "Sent frame " << i + 1 << " with ID 0x123" << std::endl;
        } else {
            std::cerr << "Failed to send frame " << i + 1 << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << std::endl;
    std::cout << "Monitoring statistics..." << std::endl;

    // Monitor statistics for a few seconds
    for (int i = 0; i < 5 && running; ++i) {
        std::cout << "TX Frames: " << can.getTxFrameCount()
                  << " | RX Frames: " << can.getRxFrameCount()
                  << " | Errors: " << can.getErrorFrameCount()
                  << " | Bus Load: " << can.getBusLoadPercent() << "%"
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;

    can.stopReceiveThread();
    can.close();

    std::cout << "Final statistics:" << std::endl;
    std::cout << "  TX Frames: " << can.getTxFrameCount() << std::endl;
    std::cout << "  RX Frames: " << can.getRxFrameCount() << std::endl;
    std::cout << "  Error Frames: " << can.getErrorFrameCount() << std::endl;

    std::cout << std::endl;
    std::cout << "Example completed successfully!" << std::endl;

    return 0;
}
