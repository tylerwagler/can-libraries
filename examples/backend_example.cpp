/**
 * @file backend_example.cpp
 * @brief Demonstrates the libcan ICanBackend abstraction
 *
 * Lists compiled-in backends, enumerates adapters per backend, opens
 * the first available SocketCAN adapter, and exercises a quick TX +
 * stats cycle. Replaces the older can_example.cpp that targeted the
 * deleted can::Interface API.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "can/i_can_backend.h"
#include "can/can_types.h"

#include <chrono>
#include <iostream>
#include <thread>

static void printAdapter(const can::AdapterInfo& a) {
    std::cout << "  channel:          " << a.channel_id << "\n";
    if (!a.device_name.empty())      std::cout << "  device_name:      " << a.device_name << "\n";
    if (!a.firmware_version.empty()) std::cout << "  firmware_version: " << a.firmware_version << "\n";
    if (!a.serial_number.empty())    std::cout << "  serial_number:    " << a.serial_number << "\n";
    if (!a.driver_version.empty())   std::cout << "  driver_version:   " << a.driver_version << "\n";
    for (auto& [k, v] : a.extra) {
        std::cout << "  extra." << k << ": " << v << "\n";
    }
}

int main(int argc, char** argv) {
    std::cout << "=== libcan backend example ===\n\n";

    auto kinds = can::ICanBackend::availableBackends();
    std::cout << "Compiled-in backends:\n";
    for (auto k : kinds) std::cout << "  - " << can::backendKindToString(k) << "\n";
    if (kinds.empty()) {
        std::cerr << "No backends compiled in. Re-configure with at least one\n"
                     "CAN_BACKEND_* option enabled.\n";
        return 1;
    }
    std::cout << "\n";

    // Pick the first backend (typically SocketCAN on Linux).
    auto backend = can::ICanBackend::create(kinds.front());
    if (!backend) {
        std::cerr << "Failed to instantiate backend.\n";
        return 1;
    }

    std::cout << "Adapters visible to "
              << can::backendKindToString(backend->kind()) << ":\n";
    auto adapters = backend->enumerateAdapters();
    for (auto& a : adapters) {
        std::cout << "---\n";
        printAdapter(a);
    }
    if (adapters.empty()) {
        std::cout << "  (none — plug in a CAN adapter or `ip link add vcan0 type vcan`)\n";
        return 0;
    }

    // Open the channel name passed on the command line, or the first
    // enumerated adapter if none was given.
    can::ChannelConfig cfg;
    cfg.channel_id = (argc > 1) ? argv[1] : adapters.front().channel_id;
    cfg.bitrate = 500000;

    std::cout << "\nOpening " << cfg.channel_id << " at "
              << can::formatBitrate(cfg.bitrate) << "...\n";
    if (!backend->open(cfg)) {
        std::cerr << "open failed: " << backend->lastError() << "\n";
        return 1;
    }

    can::Frame tx;
    tx.id = 0x123;
    tx.dlc = 8;
    for (uint8_t i = 0; i < 8; ++i) tx.data[i] = i;
    if (!backend->send(tx)) {
        std::cerr << "send failed: " << backend->lastError() << "\n";
    } else {
        std::cout << "Sent frame 0x123 [8] 00 01 02 03 04 05 06 07\n";
    }

    std::cout << "Listening for 1 second...\n";
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    int rx_count = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        can::Frame rx;
        if (backend->receive(rx, std::chrono::milliseconds(100))) {
            std::cout << "  RX 0x" << std::hex << rx.id << std::dec
                      << " [" << static_cast<int>(rx.dlc) << "]\n";
            ++rx_count;
        }
    }
    std::cout << "Received " << rx_count << " frames.\n";

    auto status = backend->status();
    std::cout << "\nFinal status:\n"
              << "  bus_state:           " << can::busStateToString(status.bus_state) << "\n"
              << "  tx_error_counter:    " << status.tx_error_counter << "\n"
              << "  rx_error_counter:    " << status.rx_error_counter << "\n";

    backend->close();
    return 0;
}
