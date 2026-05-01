# CAN Libraries

Production-ready C++17 libraries for CAN communication and DBC parsing, designed for use in both AVL Battery GUI and Ai2_SiC projects.

## Features

- **Pure C++17** - No external dependencies except standard library and Linux CAN headers
- **Thread-safe** - All shared state properly synchronized with mutexes
- **Callback-based** - Event-driven architecture for received messages
- **Well-documented** - Doxygen-style comments for all public APIs
- **Cross-project compatible** - Works with both AVL (SMCU_FES0.dbc) and Ai2_SiC (inverter DBCs)

## Project Structure

```
can-libraries/
├── CMakeLists.txt                    # Root CMake configuration
├── cmake/
│   └── CanLibrariesConfig.cmake.in   # CMake package config template
├── libcan/                           # CAN Socket Library
│   ├── CMakeLists.txt
│   ├── include/can/
│   │   ├── can_interface.h           # Main CAN interface class
│   │   ├── can_types.h               # Common types and constants
│   │   └── can_errors.h              # Error handling utilities
│   ├── src/
│   │   └── can_interface.cpp         # Implementation
│   └── test/
│       └── test_can_interface.cpp    # Unit tests
├── libdbc/                           # DBC Parser Library
│   ├── CMakeLists.txt
│   ├── include/dbc/
│   │   └── dbc_parser.h              # DBC parser class
│   ├── src/
│   │   └── dbc_parser.cpp            # Implementation
│   └── test/
│       └── test_dbc_parser.cpp       # Unit tests
├── examples/
│   ├── CMakeLists.txt
│   ├── can_example.cpp               # CAN interface example
│   ├── dbc_example.cpp               # DBC parsing example
│   └── combined_example.cpp          # Combined usage example
├── test/
│   └── CMakeLists.txt                # Test configuration
└── README.md                         # This file
```

## Building

### Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+)
- Linux kernel with SocketCAN support
- pthread library

### Build Commands

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests
make test

# Install (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | ON | Build shared libraries |
| `BUILD_TESTING` | ON | Build test executables |
| `CANLIB_INSTALL` | ON | Install libraries and headers |
| `USE_GTEST` | OFF | Use Google Test framework |

## Usage

### CAN Interface

```cpp
#include "can/can_interface.h"

using namespace can;

// Create interface
Interface can;

// Set up callbacks
can.setFrameCallback([](const Frame& frame) {
    std::cout << "Received: ID=0x" << std::hex << frame.id << std::endl;
});

can.setErrorCallback([](const std::string& error) {
    std::cerr << "Error: " << error << std::endl;
});

// Open interface (requires sudo or proper permissions)
if (can.open("can0", 500000)) {  // 500 kbps
    // Send a frame
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    Frame frame(0x123, data, 8);
    can.send(frame);
    
    // Start background receive thread
    can.startReceiveThread();
    
    // ... use interface ...
    
    // Cleanup
    can.stopReceiveThread();
    can.close();
}
```

### DBC Parser

```cpp
#include "dbc/dbc_parser.h"

using namespace dbc;

// Create parser
Parser parser;

// Parse DBC file
if (!parser.parseFile("SMCU_FES0.dbc")) {
    std::cerr << "Failed to parse DBC!" << std::endl;
    return;
}

// Get message by ID
const Message* msg = parser.getMessageById(256);
if (msg) {
    std::cout << "Message: " << msg->name << std::endl;
    
    // Decode a CAN frame
    uint8_t data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto decoded = parser.decodeFrame(256, data, 8);
    
    for (const auto& signal : decoded.signals) {
        std::cout << signal.name << " = " << signal.value;
        if (!signal.unit.empty()) {
            std::cout << " " << signal.unit;
        }
        if (!signal.value_name.empty()) {
            std::cout << " (" << signal.value_name << ")";
        }
        std::cout << std::endl;
    }
}
```

### Combined Usage

```cpp
#include "can/can_interface.h"
#include "dbc/dbc_parser.h"

// Load DBC
auto parser = std::make_shared<dbc::Parser>();
parser->parseFile("SMCU_FES0.dbc");

// Create CAN interface with DBC decoding
can::Interface can;
can.setFrameCallback([parser](const can::Frame& frame) {
    auto decoded = parser->decodeFrame(frame.id, frame.data, frame.dlc);
    
    if (decoded.valid) {
        std::cout << decoded.name << ":" << std::endl;
        for (const auto& sig : decoded.signals) {
            std::cout << "  " << sig.name << " = " << sig.value 
                      << " " << sig.unit << std::endl;
        }
    }
});

can.open("can0", 500000);
can.startReceiveThread();
```

## Integration with AVL GUI

### CMake Integration

```cmake
# In AVL GUI CMakeLists.txt
add_subdirectory(../can-libraries can-libraries-build)

target_link_libraries(avl_gui
    PRIVATE
        CanLibraries::can
        CanLibraries::dbc
)

target_include_directories(avl_gui
    PRIVATE
        ${CMAKE_SOURCE_DIR}/../can-libraries/libcan/include
        ${CMAKE_SOURCE_DIR}/../can-libraries/libdbc/include
)
```

### Usage in AVL Project

```cpp
// Replace existing CAN handling with can-libraries
#include "can/can_interface.h"
#include "dbc/dbc_parser.h"

class AVLCanManager {
public:
    AVLCanManager() {
        // Load SMCU DBC
        dbc_parser_.parseFile("/path/to/SMCU_FES0.dbc");
        
        // Set up CAN interface
        can_interface_.setFrameCallback([this](const can::Frame& frame) {
            this->onFrameReceived(frame);
        });
        
        can_interface_.open("can0", 500000);
        can_interface_.startReceiveThread();
    }

private:
    void onFrameReceived(const can::Frame& frame) {
        // Decode using DBC
        auto decoded = dbc_parser_.decodeFrame(frame.id, frame.data, frame.dlc);
        
        // Update UI with decoded values
        for (const auto& sig : decoded.signals) {
            updateSignalValue(sig.name, sig.value);
        }
    }

    can::Interface can_interface_;
    dbc::Parser dbc_parser_;
};
```

## Integration with Ai2_SiC Project

### CMake Integration

```cmake
# In Ai2_SiC CMakeLists.txt
find_package(CanLibraries REQUIRED)

target_link_libraries(sic_controller
    PRIVATE
        CanLibraries::can
        CanLibraries::dbc
)
```

### Usage in Ai2_SiC Project

```cpp
#include "can/can_interface.h"
#include "dbc/dbc_parser.h"

class SiCInverterController {
public:
    SiCInverterController() {
        // Load inverter DBC
        dbc_parser_.parseFile("inverter.dbc");
        
        can_interface_.setFrameCallback([this](const can::Frame& frame) {
            this->handleInverterMessage(frame);
        });
        
        can_interface_.open("can1", 1000000);  // 1 Mbps for inverter
    }

private:
    void handleInverterMessage(const can::Frame& frame) {
        auto decoded = dbc_parser_.decodeFrame(frame.id, frame.data, frame.dlc);
        
        // Process inverter-specific signals
        for (const auto& sig : decoded.signals) {
            if (sig.name == "DC_Bus_Voltage") {
                updateDCBusVoltage(sig.value);
            } else if (sig.name == "Motor_Torque") {
                updateMotorTorque(sig.value);
            }
        }
    }

    can::Interface can_interface_;
    dbc::Parser dbc_parser_;
};
```

## API Reference

### CAN Interface

#### Frame Structure

```cpp
struct Frame {
    uint32_t id;              // CAN identifier
    uint8_t dlc;              // Data Length Code (0-8)
    uint8_t data[8];          // Data bytes
    uint64_t timestamp_us;    // Timestamp in microseconds
    bool is_error_frame;      // Error frame flag
    bool is_remote_frame;     // Remote frame flag
    bool is_extended_id;      // 29-bit extended ID
    bool is_fd_frame;         // CAN FD flag
};
```

#### Interface Methods

| Method | Description |
|--------|-------------|
| `open(interface, bitrate)` | Open CAN interface |
| `close()` | Close interface |
| `isOpen()` | Check connection status |
| `send(frame)` | Send CAN frame |
| `receive(frame, blocking)` | Receive CAN frame |
| `setFrameCallback(callback)` | Set frame received callback |
| `setErrorCallback(callback)` | Set error callback |
| `getTxFrameCount()` | Get transmitted frame count |
| `getRxFrameCount()` | Get received frame count |
| `getBusLoadPercent()` | Get bus load percentage |

### DBC Parser

#### Signal Structure

```cpp
struct Signal {
    std::string name;           // Signal name
    size_t start_bit;           // Starting bit position
    size_t length;              // Signal length in bits
    bool is_little_endian;      // Intel vs Motorola byte order
    bool is_signed;             // Signed/unsigned
    double factor;              // Scaling factor
    double offset;              // Offset value
    double min_value;           // Minimum physical value
    double max_value;           // Maximum physical value
    std::string unit;           // Unit string
    std::map<int, std::string> value_table;  // Value table
};
```

#### Parser Methods

| Method | Description |
|--------|-------------|
| `parseFile(filepath)` | Parse DBC file |
| `parseString(content)` | Parse DBC from string |
| `getMessageById(id)` | Get message by CAN ID |
| `getMessageByName(name)` | Get message by name |
| `decodeFrame(id, data, dlc)` | Decode CAN frame |
| `encodeFrame(id, values, data, dlc)` | Encode signal values |
| `getSignalValueName(msg_id, sig_name, value)` | Get value table name |

## Testing

### Running Tests

```bash
cd build
make test_can_interface
make test_dbc_parser

# Or use ctest
ctest --output-on-failure
```

### Test Coverage

- **CAN Interface Tests**
  - Frame creation and validation
  - Statistics tracking
  - Callback handling
  - Configuration options

- **DBC Parser Tests**
  - Message parsing
  - Signal parsing (Intel/Motorola byte order)
  - Value table parsing
  - Frame decoding/encoding
  - Edge cases

## Troubleshooting

### Common Issues

1. **Cannot open CAN interface**
   ```bash
   # Create virtual interface for testing
   sudo ip link add dev vcan0 type vcan
   sudo ip link set up vcan0
   ```

2. **Permission denied**
   ```bash
   # Add user to can group or use setcap
   sudo usermod -aG can $USER
   # Or
   sudo setcap cap_net_raw+eip $(which your_app)
   ```

3. **DBC parsing fails**
   - Check DBC file format (should be standard DBC format)
   - Verify file encoding (UTF-8 without BOM)
   - Check for syntax errors in DBC file

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests: `make test`
5. Submit a pull request

## License

MIT License - See LICENSE file for details

## Credits

Developed for AVL Test Equipment battery testing systems.
