# CanLibraries Integration Guide

This document explains how to integrate the CAN and DBC libraries into your projects.

## Library Overview

**CanLibraries** provides two shared, cross-project libraries:

1. **libcan** - Linux SocketCAN interface with thread-safe I/O
2. **libdbc** - DBC file parser for decoding CAN messages

Both libraries are:
- Pure C++17 (no Qt or external dependencies)
- Thread-safe
- Callback-based architecture
- Compatible with both AVL Battery GUI and Ai2_SiC projects

## Build Instructions

### Building the Libraries

```bash
cd /home/tyler/Projects/Crane/can-libraries
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Installing System-Wide (Optional)

```bash
sudo make install
```

This installs to:
- Libraries: `/usr/local/lib/`
- Headers: `/usr/local/include/can/` and `/usr/local/include/dbc/`
- CMake config: `/usr/local/lib/cmake/CanLibraries/`

## Integration with AVL Battery GUI

### Option 1: Subdirectory (Recommended for Development)

Edit `/home/tyler/Projects/Crane/AVL/GUI/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(AVL_Battery_GUI)

# Add can-libraries as subdirectory
add_subdirectory(../../../can-libraries can-libraries-build)

# Find Qt
find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

# Add executable
add_executable(avl_gui
    src/main.cpp
    src/ui/main_window.cpp
    # ... other sources
)

# Link libraries
target_link_libraries(avl_gui PRIVATE
    Qt5::Widgets
    Qt5::Network
    CanLibraries::can
    CanLibraries::dbc
)

# Include directories (if not using subdirectory)
# target_include_directories(avl_gui PRIVATE
#     ${CMAKE_SOURCE_DIR}/../../../can-libraries/libcan/include
#     ${CMAKE_SOURCE_DIR}/../../../can-libraries/libdbc/include
# )
```

### Option 2: find_package (For Installed Libraries)

```cmake
find_package(CanLibraries REQUIRED)

target_link_libraries(avl_gui PRIVATE
    Qt5::Widgets
    CanLibraries::can
    CanLibraries::dbc
)
```

### Migrating Existing Code

Replace internal CAN handling:

**Old (in can_manager.cpp):**
```cpp
#include <linux/can.h>
#include <linux/can/raw.h>
// ... manual socket handling
```

**New:**
```cpp
#include <can/can_interface.h>

class CanManager : public QObject {
    can::Interface can_interface_;
    
    void onFrameReceived(const can::Frame& frame) {
        // Process frame
    }
    
public:
    bool initialize(const QString& interface) {
        can_interface_.setFrameCallback([this](const can::Frame& frame) {
            onFrameReceived(frame);
        });
        return can_interface_.open(interface.toStdString(), 500000);
    }
};
```

Replace DBC parsing:

**Old:**
```cpp
// Manual DBC parsing
```

**New:**
```cpp
#include <dbc/dbc_parser.h>

class CanManager : public QObject {
    dbc::Parser dbc_parser_;
    
public:
    bool loadDbc(const QString& path) {
        return dbc_parser_.parseFile(path.toStdString());
    }
    
    void decodeFrame(uint32_t id, const uint8_t* data, size_t dlc) {
        auto decoded = dbc_parser_.decodeFrame(id, data, dlc);
        // Process decoded signals
    }
};
```

## Integration with Ai2_SiC

### Option 1: Subdirectory

Edit `/home/tyler/Projects/Crane/Ai2_SiC/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MclarenInverterViewer)

# Add can-libraries
add_subdirectory(../../../can-libraries can-libraries-build)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(mclaren_inverter_viewer
    src/main.cpp
    src/can/can_manager.cpp
    # ... other sources
)

target_link_libraries(mclaren_inverter_viewer PRIVATE
    Qt5::Widgets
    CanLibraries::can
    CanLibraries::dbc
)
```

### Option 2: find_package

```cmake
find_package(CanLibraries REQUIRED)

target_link_libraries(mclaren_inverter_viewer PRIVATE
    Qt5::Widgets
    CanLibraries::can
    CanLibraries::dbc
)
```

## Usage Examples

### Basic CAN Communication

```cpp
#include <can/can_interface.h>
#include <iostream>

int main() {
    can::Interface can;
    
    // Set callback for received frames
    can.setFrameCallback([](const can::Frame& frame) {
        std::cout << "Received CAN ID 0x" << std::hex << frame.id << std::dec << std::endl;
    });
    
    // Open interface
    if (!can.open("can0", 500000)) {
        std::cerr << "Failed to open CAN interface" << std::endl;
        return 1;
    }
    
    // Send a frame
    can::Frame tx;
    tx.id = 0x123;
    tx.dlc = 8;
    tx.data[0] = 0x01;
    tx.data[1] = 0x02;
    // ... fill rest of data
    tx.is_error_frame = false;
    tx.is_remote_frame = false;
    
    if (!can.send(tx)) {
        std::cerr << "Failed to send frame" << std::endl;
    }
    
    // Wait for messages (blocking receive)
    can::Frame rx;
    while (can.receive(rx, true)) {
        // Process received frame
    }
    
    can.close();
    return 0;
}
```

### DBC Parsing and Decoding

```cpp
#include <dbc/dbc_parser.h>
#include <iostream>

int main() {
    dbc::Parser parser;
    
    // Parse DBC file
    if (!parser.parseFile("/path/to/SMCU_FES0.dbc")) {
        std::cerr << "Failed to parse DBC file" << std::endl;
        return 1;
    }
    
    // Get message by ID
    const auto* msg = parser.getMessageById(256); // SMCU_00_DATA1
    if (msg) {
        std::cout << "Message: " << msg->name << std::endl;
        std::cout << "Signals: " << msg->signals.size() << std::endl;
        
        // Print signal names
        for (const auto& [name, signal] : msg->signals) {
            std::cout << "  - " << name << " (bit " << signal.start_bit 
                      << ", len " << signal.length << ")" << std::endl;
        }
    }
    
    // Decode a CAN frame
    can::Frame frame;
    frame.id = 256;
    frame.dlc = 8;
    // ... fill frame data
    
    auto decoded = parser.decodeFrame(frame.id, frame.data, frame.dlc);
    
    // Access decoded signals
    for (const auto& sig : decoded.signals) {
        std::cout << sig.name << " = " << sig.value;
        if (!sig.unit.empty()) {
            std::cout << " " << sig.unit;
        }
        if (!sig.value_name.empty()) {
            std::cout << " (" << sig.value_name << ")";
        }
        std::cout << std::endl;
    }
    
    return 0;
}
```

### Combined Usage (Real-World Example)

```cpp
#include <can/can_interface.h>
#include <dbc/dbc_parser.h>
#include <iostream>
#include <thread>

int main() {
    // Initialize DBC parser
    dbc::Parser dbc;
    if (!dbc.parseFile("SMCU_FES0.dbc")) {
        std::cerr << "Failed to load DBC" << std::endl;
        return 1;
    }
    
    // Initialize CAN interface
    can::Interface can;
    can.setFrameCallback([&dbc](const can::Frame& frame) {
        // Decode using DBC
        auto decoded = dbc.decodeFrame(frame.id, frame.data, frame.dlc);
        
        if (decoded.valid) {
            std::cout << "Decoded: " << decoded.name << std::endl;
            
            // Process specific signals
            for (const auto& sig : decoded.signals) {
                if (sig.name.find("Cell_Voltage") != std::string::npos) {
                    std::cout << "  " << sig.name << ": " 
                              << sig.value << " " << sig.unit << std::endl;
                }
            }
        }
    });
    
    // Open CAN interface
    if (!can.open("can0", 500000)) {
        std::cerr << "Failed to open CAN" << std::endl;
        return 1;
    }
    
    std::cout << "Listening for CAN messages (Ctrl+C to stop)..." << std::endl;
    
    // Run receive loop
    can::Frame frame;
    while (true) {
        try {
            if (!can.receive(frame, true)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    
    return 0;
}
```

## API Reference

### can::Interface

**Methods:**
- `bool open(const std::string& interface, uint32_t bitrate)` - Open CAN interface
- `void close()` - Close interface
- `bool send(const Frame& frame)` - Transmit frame
- `bool receive(Frame& frame, bool blocking)` - Receive frame
- `void setFrameCallback(FrameCallback cb)` - Set receive callback
- `void setErrorCallback(ErrorCallback cb)` - Set error callback
- `uint64_t getTxFrameCount() const` - Get transmitted frame count
- `uint64_t getRxFrameCount() const` - Get received frame count
- `double getBusLoadPercent() const` - Get bus load percentage

### dbc::Parser

**Methods:**
- `bool parseFile(const std::string& path)` - Parse DBC file
- `bool parseString(const std::string& content)` - Parse DBC from string
- `const Message* getMessageById(uint32_t id) const` - Get message by ID
- `const Message* getMessageByName(const std::string& name) const` - Get message by name
- `DecodedMessage decodeFrame(uint32_t id, const uint8_t* data, size_t dlc) const` - Decode frame
- `bool encodeFrame(uint32_t id, const std::map<std::string, double>& values, uint8_t* data, size_t& dlc) const` - Encode frame

## Troubleshooting

### Common Issues

**Issue:** "Cannot find can/can_interface.h"
- **Solution:** Ensure CMakeLists.txt includes the can-libraries subdirectory or uses find_package correctly

**Issue:** "undefined reference to can::Interface::open"
- **Solution:** Link against CanLibraries::can in target_link_libraries

**Issue:** DBC parsing fails
- **Solution:** Check file path is correct and DBC file format is valid

**Issue:** No frames received
- **Solution:** Ensure CAN interface is up (`ip link set can0 up`), check permissions, verify cable connection

## Development

### Running Tests

```bash
cd build
make test_can_interface
make test_dbc_parser
./bin/test_can_interface
./bin/test_dbc_parser
```

### Building Examples

```bash
make can_example
make dbcc_example
./bin/can_example
./bin/dbcc_example
```

## License

MIT License - See LICENSE file for details.

## Contributing

Contributions welcome! Please submit pull requests to the main repository.
