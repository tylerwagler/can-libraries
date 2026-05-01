/**
 * @file i_can_backend.cpp
 * @brief Backend factory and helpers
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "can/i_can_backend.h"

namespace can {

std::string backendKindToString(BackendKind kind) {
    switch (kind) {
        case BackendKind::SocketCan: return "SocketCAN";
        case BackendKind::PcanBasic: return "PCANBasic";
        case BackendKind::Kvaser:    return "Kvaser";
        case BackendKind::VectorXL:  return "VectorXL";
    }
    return "Unknown";
}

std::string busStateToString(BusState state) {
    switch (state) {
        case BusState::Unknown:       return "unknown";
        case BusState::ErrorActive:   return "error-active";
        case BusState::ErrorWarning:  return "error-warning";
        case BusState::ErrorPassive:  return "error-passive";
        case BusState::BusOff:        return "bus-off";
    }
    return "unknown";
}

// Forward declarations of backend factory functions. Each backend's
// CMake-gated source defines its own create_*() function; if the backend
// isn't compiled in, the corresponding stub below returns nullptr.
#ifdef CAN_BACKEND_SOCKETCAN
std::unique_ptr<ICanBackend> createSocketCanBackend();
#else
static std::unique_ptr<ICanBackend> createSocketCanBackend() { return nullptr; }
#endif

#ifdef CAN_BACKEND_PCAN
std::unique_ptr<ICanBackend> createPcanBackend();
#else
static std::unique_ptr<ICanBackend> createPcanBackend() { return nullptr; }
#endif

#ifdef CAN_BACKEND_KVASER
std::unique_ptr<ICanBackend> createKvaserBackend();
#else
static std::unique_ptr<ICanBackend> createKvaserBackend() { return nullptr; }
#endif

#ifdef CAN_BACKEND_VECTOR
std::unique_ptr<ICanBackend> createVectorBackend();
#else
static std::unique_ptr<ICanBackend> createVectorBackend() { return nullptr; }
#endif

std::unique_ptr<ICanBackend> ICanBackend::create(BackendKind kind) {
    switch (kind) {
        case BackendKind::SocketCan: return createSocketCanBackend();
        case BackendKind::PcanBasic: return createPcanBackend();
        case BackendKind::Kvaser:    return createKvaserBackend();
        case BackendKind::VectorXL:  return createVectorBackend();
    }
    return nullptr;
}

std::vector<BackendKind> ICanBackend::availableBackends() {
    std::vector<BackendKind> kinds;
#ifdef CAN_BACKEND_SOCKETCAN
    kinds.push_back(BackendKind::SocketCan);
#endif
#ifdef CAN_BACKEND_PCAN
    kinds.push_back(BackendKind::PcanBasic);
#endif
#ifdef CAN_BACKEND_KVASER
    kinds.push_back(BackendKind::Kvaser);
#endif
#ifdef CAN_BACKEND_VECTOR
    kinds.push_back(BackendKind::VectorXL);
#endif
    return kinds;
}

} // namespace can
