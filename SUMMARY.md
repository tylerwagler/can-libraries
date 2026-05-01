# CAN Libraries - Implementation Summary

## Overview

This document summarizes the complete CAN and DBC library implementation created at `/home/tyler/Projects/Crane/can-libraries/`.

## Files Created

### Root Configuration
| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake configuration with build options and installation |
| `cmake/CanLibrariesConfig.cmake.in` | CMake package configuration template for find_package() |
| `README.md` | Comprehensive documentation with examples |

### libcan (CAN Socket Library)

#### Headers
| File | Description |
|------|-------------|
| `libcan/include/can/can_interface.h` | Main Interface class with callback support |
| `libcan/include/can/can_types.h` | Common types, constants, and helper functions |
| `libcan/include/can/can_errors.h` | Exception classes and error handling utilities |

#### Implementation
| File | Description |
|------|-------------|
| `libcan/src/can_interface.cpp` | Full Linux SocketCAN implementation |

#### Tests
| File | Description |
|------|-------------|
| `libcan/test/test_can_interface.cpp` | Unit tests for CAN interface |

#### Build Configuration
| File | Description |
|------|-------------|
| `libcan/CMakeLists.txt` | Library build and installation rules |

### libdbc (DBC Parser Library)

#### Headers
| File | Description |
|------|-------------|
| `libdbc/include/dbc/dbc_parser.h` | Parser class with Message/Signal structures |

#### Implementation
| File | Description |
|------|-------------|
| `libdbc/src/dbc_parser.cpp` | Complete DBC parser with encoding/decoding |

#### Tests
| File | Description |
|------|-------------|
| `libdbc/test/test_dbc_parser.cpp` | Unit tests for DBC parsing |

#### Build Configuration
| File | Description |
|------|-------------|
| `libdbc/CMakeLists.txt` | Library build and installation rules |

### Examples

| File | Description |
|------|-------------|
| `examples/can_example.cpp` | Simple CAN interface usage example |
| `examples/dbc_example.cpp` | DBC parsing and decoding example |
| `examples/combined_example.cpp` | Combined CAN + DBC usage example |
| `examples/CMakeLists.txt` | Example build configuration |

### Tests

| File | Description |
|------|-------------|
| `test/CMakeLists.txt` | Test suite configuration |

## Key Features

### CAN Interface (libcan)

1. **Thread-safe operations**
   - All statistics protected by mutex
   - Callbacks protected by mutex
   - Atomic flags for state management

2. **Event-driven architecture**
   - Frame callback for received messages
   - Error callback for error events
   - State callback for connection changes

3. **Statistics tracking**
   - TX/RX frame counts
   - Error frame count
   - Bus load percentage calculation

4. **Configuration options**
   - Non-blocking I/O
   - Receive timeout
   - Remote frame filtering
   - Error frame filtering

5. **Background receive thread**
   - Automatic frame reception
   - Callback invocation
   - Clean start/stop

### DBC Parser (libdbc)

1. **Complete DBC parsing**
   - Message definitions (BO_)
   - Signal definitions (SG_)
   - Value tables (VAL_)
   - Attributes (BA_)

2. **Signal encoding/decoding**
   - Intel (little-endian) byte order
   - Motorola (big-endian) byte order
   - Signed/unsigned handling
   - Factor and offset application

3. **Value table support**
   - Enumerated value names
   - Lookup by integer value

4. **Frame operations**
   - Decode raw CAN frames to physical values
   - Encode physical values to CAN frames

## Integration Instructions

### For AVL GUI Project

1. **Add as subdirectory:**
   ```cmake
   add_subdirectory(../can-libraries can-libraries-build)
   
   target_link_libraries(avl_gui
       PRIVATE
           CanLibraries::can
           CanLibraries::dbc
   )
   ```

2. **Replace existing CAN handling:**
   ```cpp
   // Old: Custom CAN implementation
   // New:
   can::Interface can;
   can.open("can0", 500000);
   can.setFrameCallback([this](const can::Frame& frame) {
       auto decoded = dbc_parser_.decodeFrame(frame.id, frame.data, frame.dlc);
       // Update UI with decoded values
   });
   ```

3. **Load SMCU DBC:**
   ```cpp
   dbc::Parser parser;
   parser.parseFile("/path/to/SMCU_FES0.dbc");
   ```

### For Ai2_SiC Project

1. **Find package (if installed):**
   ```cmake
   find_package(CanLibraries REQUIRED)
   
   target_link_libraries(sic_controller
       PRIVATE
           CanLibraries::can
           CanLibraries::dbc
   )
   ```

2. **Use with inverter DBC:**
   ```cpp
   dbc::Parser parser;
   parser.parseFile("inverter.dbc");
   
   can::Interface can;
   can.open("can1", 1000000);  // 1 Mbps for inverter
   ```

## Build Commands

```bash
# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Install
sudo make install
```

## Testing

### Unit Tests
- `test_can_interface`: Tests frame creation, statistics, callbacks
- `test_dbc_parser`: Tests message/signal parsing, decoding, encoding

### Example Programs
- `can_example`: Basic CAN interface usage
- `dbc_example`: DBC parsing demonstration
- `combined_example`: Full integration example

## API Reference

### CAN Interface Main Methods

```cpp
bool open(const std::string& interface_name, uint32_t bitrate);
void close();
bool send(const Frame& frame);
bool receive(Frame& frame, bool blocking = false);
void setFrameCallback(FrameCallback callback);
void setErrorCallback(ErrorCallback callback);
uint64_t getTxFrameCount() const;
uint64_t getRxFrameCount() const;
double getBusLoadPercent() const;
```

### DBC Parser Main Methods

```cpp
bool parseFile(const std::string& filepath);
const Message* getMessageById(uint32_t id) const;
const Message* getMessageByName(const std::string& name) const;
DecodedMessage decodeFrame(uint32_t id, const uint8_t* data, size_t dlc) const;
bool encodeFrame(uint32_t id, const std::map<std::string, double>& values,
                 uint8_t* data, size_t& dlc) const;
std::string getSignalValueName(uint32_t msg_id, const std::string& signal_name,
                               int value) const;
```

## Design Decisions

1. **Pure C++17** - No Qt or external dependencies for maximum portability
2. **Callback-based** - Event-driven design fits well with GUI applications
3. **Thread-safe** - All shared state protected for multi-threaded use
4. **Standard SocketCAN** - Uses Linux kernel CAN support directly
5. **Modular design** - Separate CAN and DBC libraries for flexibility

## Future Enhancements

1. CAN FD support (beyond 8 bytes)
2. Additional DBC features (environment variables, node definitions)
3. Multiple interface support
4. Message filtering
5. Signal grouping

## License

MIT License - See LICENSE file for details
