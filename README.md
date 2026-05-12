# CAN Libraries

C++23 libraries for CAN communication, used by Crane's Qt application
template (`Qt_Template`) and intended for direct use by any non-Qt
consumer that needs a vendor-agnostic CAN abstraction.

Components:

- **libcan** — `can::ICanBackend`, an event-driven, vendor-neutral CAN
  driver interface. Implementations:
  - `SocketCanBackend` (Linux) — wraps the kernel SocketCAN stack and
    enriches `AdapterInfo` with sysfs / SIOCETHTOOL data so the Linux
    driver yields the same diagnostic surface the vendor SDKs expose
    (firmware version, vendor, model, USB topology, error counters)
  - `PcanBackend` (Linux + Windows) — PEAK-System PCANBasic API
  - `KvaserBackend` (Linux + Windows) — Kvaser canlib
  - `VectorBackend` — placeholder; lands when Vector hardware is on hand

- **Vendored Tobias Lorenz libraries** (GPL-3.0, git submodules under
  `third_party/`):
  - **Vector_DBC** — DBC file parser. Re-exported as `CanLibraries::dbc`.
  - **Vector_BLF** — Vector binary log format. Re-exported as `CanLibraries::blf`.
  - **Vector_ASC** — Vector ASCII log format. Re-exported as `CanLibraries::asc`.

  Initialize after cloning: `git submodule update --init --recursive`.

## Layout

```
can-libraries/
├── CMakeLists.txt
├── cmake/
│   ├── CanLibrariesConfig.cmake.in
│   ├── FindPCANBasic.cmake
│   └── FindKvaser.cmake
├── libcan/
│   ├── include/can/
│   │   ├── frame.h            # can::Frame
│   │   ├── can_types.h        # constants, isExtId, parseBitrate, etc.
│   │   └── i_can_backend.h    # ICanBackend, AdapterInfo, ChannelStatus
│   ├── src/
│   │   ├── i_can_backend.cpp  # factory
│   │   └── backends/
│   │       ├── socketcan_backend.{h,cpp}
│   │       ├── pcan_backend.{h,cpp}
│   │       └── kvaser_backend.{h,cpp}
│   └── test/test_backend.cpp
├── third_party/
│   ├── Vector_DBC/    (submodule — bitbucket.org/tobylorenz/vector_dbc)
│   ├── Vector_BLF/    (submodule — bitbucket.org/tobylorenz/vector_blf)
│   └── Vector_ASC/    (submodule — bitbucket.org/tobylorenz/vector_asc)
└── examples/
    ├── backend_example.cpp
    └── dbc_example.cpp
```

## Building

Requirements:
- CMake ≥ 3.25, a C++23 compiler, pthreads
- `flex` + `bison ≥ 3.3` (Vector_DBC and Vector_ASC use generated parsers)
- `zlib` (Vector_BLF compresses log streams)
- Linux kernel headers (SocketCAN backend)
- Optional vendor SDKs (PCAN, Kvaser, Vector XL — see backend options below)

```sh
git clone <repo>
cd can-libraries
git submodule update --init --recursive   # pulls in Vector_DBC/BLF/ASC
mkdir build && cd build
cmake ..              # SocketCAN-only on Linux by default
cmake --build .
ctest                 # unit tests
./bin/backend_example # exercises the abstraction
./bin/dbc_example my.dbc  # exercises Vector_DBC
```

### Backend options

Each vendor backend is opt-in via a CMake option. If the option is on
but the vendor SDK isn't installed, configure fails with a clear error
from the corresponding `Find*.cmake` module.

| Option                  | Default                | SDK required               |
|-------------------------|------------------------|----------------------------|
| `CAN_BACKEND_SOCKETCAN` | ON on Linux            | Linux kernel headers       |
| `CAN_BACKEND_PCAN`      | OFF                    | PEAK-System PCANBasic      |
| `CAN_BACKEND_KVASER`    | OFF                    | Kvaser canlib (kvlibsdk)   |
| `CAN_BACKEND_VECTOR`    | OFF (not implemented)  | n/a — fails configure if ON |

Override SDK search paths with `-DPCANBASIC_ROOT=…` or `-DKVASER_ROOT=…`.

`CAN_BACKEND_PCAN` and `CAN_BACKEND_KVASER` currently advertise CAN-FD as
**not supported** via `BackendCapabilities::supports_can_fd`; the open /
send paths reject FD configurations and frames respectively. CAN-FD is
end-to-end working on `CAN_BACKEND_SOCKETCAN` only. The Vector backend
is a planned placeholder — flipping the option on produces a fatal-error
at configure time.

## Using libcan

```cpp
#include "can/i_can_backend.h"

auto backend = can::ICanBackend::create(can::BackendKind::SocketCan);
for (const auto& a : backend->enumerateAdapters()) {
    // a.device_name, a.firmware_version, a.serial_number, a.extra...
}

can::ChannelConfig cfg;
cfg.channel_id = "can0";
cfg.bitrate    = 500'000;
backend->open(cfg);

can::Frame f(0x123, payload, 8);
backend->send(f);

can::Frame rx;
if (backend->receive(rx, std::chrono::milliseconds(100))) { /* handle */ }

auto status = backend->status();   // bus_state, error counters
backend->close();
```

`receive()` blocks in the kernel (event-driven via `select()` /
`WaitForSingleObject`) until a frame arrives or the timeout expires —
no polling. `send()` is documented thread-safe on every backend, so
multiple threads may transmit concurrently.

For Qt apps, use the `qt_template` library on top of libcan rather than
calling `ICanBackend` directly — it provides a worker-thread-based
`CanManager` with periodic-TX scheduling and a status / signal API
suitable for GUI work.

## Cross-platform symbol visibility

Public API uses `LIBCAN_EXPORT`, generated by CMake's
`GenerateExportHeader`. Symbols are hidden by default on both Linux
and Windows so the export surface is consistent across platforms.

## License

GPL-3.0-or-later. See `LICENSE`.

This library is published under the GNU General Public License v3 to align
with the licensing of upstream tooling we depend on (e.g. Tobias Lorenz's
`Vector_DBC` for DBC parsing, used by openSYDE). Apps that link against
`libcan` must also be GPL-compatible.
