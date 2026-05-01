cmake_minimum_required(VERSION 3.25)
project(CanLibraries VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Detect whether we're the top-level build (a standalone CanLibraries build)
# or being consumed via add_subdirectory() from a downstream project. When
# consumed downstream, default examples/tests off so they don't get pulled
# into the parent's build target list.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_CANLIBS_TOPLEVEL ON)
else()
    set(_CANLIBS_TOPLEVEL OFF)
endif()

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_TESTING "Build tests" ${_CANLIBS_TOPLEVEL})
option(CANLIB_INSTALL "Install libraries and headers" ${_CANLIBS_TOPLEVEL})
option(CANLIB_BUILD_EXAMPLES "Build can-libraries example apps" ${_CANLIBS_TOPLEVEL})

# CAN backend options. Defaults: enable SocketCAN on Linux, attempt to
# auto-enable PCAN if its SDK is found, leave Kvaser/Vector off until the
# user opts in.
if(UNIX AND NOT APPLE)
    option(CAN_BACKEND_SOCKETCAN "Build SocketCAN backend (Linux)" ON)
else()
    option(CAN_BACKEND_SOCKETCAN "Build SocketCAN backend (Linux)" OFF)
endif()
option(CAN_BACKEND_PCAN   "Build PCANBasic backend" OFF)
option(CAN_BACKEND_KVASER "Build Kvaser canlib backend" OFF)
option(CAN_BACKEND_VECTOR "Build Vector XL backend (Windows + supported Linux hardware)" OFF)
# Vector's XL Driver Library has both Windows and Linux releases. The Linux
# library covers the modern lineup (VN1600 family, VN5000 family, etc.) but
# not all Vector hardware. The user must download the Linux SDK separately
# from Vector — find_package(VectorXL) below will fail with a clear error
# if the SDK isn't present.

# Add cmake/ to the module path so Find* modules are discoverable.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Resolve vendor SDKs for any backends the user opted into. We do this at
# top level so the SDK-found state is available everywhere downstream.
if(CAN_BACKEND_PCAN)
    find_package(PCANBasic REQUIRED)
endif()
if(CAN_BACKEND_KVASER)
    find_package(Kvaser REQUIRED)
endif()
if(CAN_BACKEND_VECTOR)
    find_package(VectorXL REQUIRED)
endif()

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Testing — call enable_testing() BEFORE add_subdirectory so tests added
# in subdirs register with ctest.
if(BUILD_TESTING)
    enable_testing()
endif()

# Add subdirectories
add_subdirectory(libcan)
add_subdirectory(libdbc)

# Examples — opt out when consumed downstream.
if(CANLIB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation configuration
if(CANLIB_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Configure package config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CanLibrariesConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CanLibraries
    )

    # Write version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install CMake config files
    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CanLibraries
    )

    # Export targets
    install(
        EXPORT CanLibrariesTargets
        NAMESPACE CanLibraries::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CanLibraries
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "CanLibraries Configuration Summary")
message(STATUS "==================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "Build testing: ${BUILD_TESTING}")
message(STATUS "Install: ${CANLIB_INSTALL}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Backends:")
message(STATUS "  SocketCAN: ${CAN_BACKEND_SOCKETCAN}")
message(STATUS "  PCANBasic: ${CAN_BACKEND_PCAN}")
message(STATUS "  Kvaser:    ${CAN_BACKEND_KVASER}")
message(STATUS "  VectorXL:  ${CAN_BACKEND_VECTOR}")
message(STATUS "")
