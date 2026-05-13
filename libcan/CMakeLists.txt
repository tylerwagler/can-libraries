# CAN Socket Library CMakeLists.txt
# Builds libcan - cross-platform CAN library with pluggable backends

cmake_minimum_required(VERSION 3.25)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(LIBCAN_SOURCES
    src/i_can_backend.cpp
)

# Per-backend sources are appended below based on CAN_BACKEND_* options.
if(CAN_BACKEND_SOCKETCAN)
    list(APPEND LIBCAN_SOURCES src/backends/socketcan_backend.cpp)
endif()
if(CAN_BACKEND_PCAN)
    list(APPEND LIBCAN_SOURCES src/backends/pcan_backend.cpp)
endif()
if(CAN_BACKEND_KVASER)
    list(APPEND LIBCAN_SOURCES src/backends/kvaser_backend.cpp)
endif()
if(CAN_BACKEND_VECTOR)
    list(APPEND LIBCAN_SOURCES src/backends/vector_backend.cpp)
endif()

# ----------------------------------------------------------------------------
# Library target
# ----------------------------------------------------------------------------
add_library(libcan ${LIBCAN_SOURCES})
add_library(CanLibraries::can ALIAS libcan)

# Generate libcan_export.h with the LIBCAN_EXPORT macro that resolves to
# __declspec(dllexport)/dllimport on Windows and __attribute__((visibility))
# on Linux. This is what makes the library work correctly as both a .so
# and a .dll.
include(GenerateExportHeader)
generate_export_header(libcan
    BASE_NAME LIBCAN
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/can/libcan_export.h
)

# Generate version.h from version.h.in so LIBCAN_VERSION_* and
# can::libraryVersion() stay locked to the project() VERSION above.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/can/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/can/version.h
    @ONLY
)

# Hide non-exported symbols on Linux so the export surface matches Windows.
# EXPORT_NAME=can makes the installed target `CanLibraries::can` directly
# (the build-tree alias is already `CanLibraries::can`); without this, the
# installed name would be `CanLibraries::libcan` and CanLibrariesConfig
# would need to manually recreate the alias post-find_package.
# OUTPUT_NAME=can fixes the on-disk file name: the target is internally
# called `libcan`, and without OUTPUT_NAME CMake would prefix `lib` again
# and ship `liblibcan.so` (which downstream `-llibcan` would link, but is
# not what anyone expects from `find_package(CanLibraries)` + the `::can`
# alias). With this, we ship `libcan.so` and `-lcan` works.
# SOVERSION tracks the project's major version so the SONAME accurately
# reflects ABI breakage. A future bump to 3.x will mint a new soname and
# old binaries will refuse to load against it (instead of silently picking
# up an incompatible build).
# POSITION_INDEPENDENT_CODE on the target (rather than as a global) keeps
# the property scoped to libcan, so it works correctly even when this
# project is consumed via add_subdirectory() from a parent that has its
# own PIC choice.
set_target_properties(libcan PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME can
    OUTPUT_NAME can
)

# When built as a static library, the generated libcan_export.h's
# LIBCAN_EXPORT macro must be a no-op for consumers — otherwise on
# Windows it would expand to __declspec(dllimport) when downstream code
# includes the public headers, breaking static linkage. Setting
# LIBCAN_STATIC_DEFINE as a PUBLIC compile definition makes the macro
# resolve to empty for both the library's own TUs and consumers.
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(libcan PUBLIC LIBCAN_STATIC_DEFINE)
endif()

# Compile definitions per enabled backend.
if(CAN_BACKEND_SOCKETCAN)
    target_compile_definitions(libcan PRIVATE CAN_BACKEND_SOCKETCAN=1)
endif()
if(CAN_BACKEND_PCAN)
    target_compile_definitions(libcan PRIVATE CAN_BACKEND_PCAN=1)
endif()
if(CAN_BACKEND_KVASER)
    target_compile_definitions(libcan PRIVATE CAN_BACKEND_KVASER=1)
endif()
if(CAN_BACKEND_VECTOR)
    target_compile_definitions(libcan PRIVATE CAN_BACKEND_VECTOR=1)
endif()

# ----------------------------------------------------------------------------
# Includes
# ----------------------------------------------------------------------------
target_include_directories(libcan
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ----------------------------------------------------------------------------
# Link dependencies
# ----------------------------------------------------------------------------
find_package(Threads REQUIRED)
target_link_libraries(libcan PUBLIC Threads::Threads)

if(CAN_BACKEND_PCAN)
    target_link_libraries(libcan PRIVATE PCANBasic::PCANBasic)
endif()
if(CAN_BACKEND_KVASER)
    target_link_libraries(libcan PRIVATE Kvaser::canlib)
endif()
if(CAN_BACKEND_VECTOR)
    target_link_libraries(libcan PRIVATE VectorXL::VectorXL)
endif()

# Internal libcan code uses C++23 features (digit separators, designated initializers, etc.).
# Public headers stay at C++14 minimum so consumers can choose their own standard — propagating
# cxx_std_23 PUBLIC pulled <stdfloat>'s std::float64_t into older consumers (openSYDE) that
# expect their own global float64_t typedef from stwtypes.hpp.
target_compile_features(libcan
    PRIVATE cxx_std_23
    INTERFACE cxx_std_14
)

# ----------------------------------------------------------------------------
# Installation
# ----------------------------------------------------------------------------
if(CANLIB_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS libcan
        EXPORT CanLibrariesTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES
        include/can/frame.h
        include/can/can_types.h
        include/can/i_can_backend.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/can/libcan_export.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/can/version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/can
    )
endif()

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(BUILD_TESTING)
    add_executable(test_backend test/test_backend.cpp)
    target_link_libraries(test_backend PRIVATE libcan)
    target_compile_features(test_backend PRIVATE cxx_std_23)

    add_test(NAME BackendTest COMMAND test_backend)

    # Live SocketCAN integration test — skipped at runtime if vcan0 is
    # not present (exit 77, the CTest skip convention).
    if(CAN_BACKEND_SOCKETCAN)
        add_executable(test_socketcan_live test/test_socketcan_live.cpp)
        target_link_libraries(test_socketcan_live PRIVATE libcan)
        target_compile_features(test_socketcan_live PRIVATE cxx_std_23)

        add_test(NAME SocketCanLiveTest COMMAND test_socketcan_live)
        set_tests_properties(SocketCanLiveTest PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
