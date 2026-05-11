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

# Hide non-exported symbols on Linux so the export surface matches Windows.
set_target_properties(libcan PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

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
endif()
