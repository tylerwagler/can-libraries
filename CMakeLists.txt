cmake_minimum_required(VERSION 3.25)
project(CanLibraries VERSION 2.0.0 LANGUAGES CXX)

# Detect whether we're the top-level build (a standalone CanLibraries build)
# or being consumed via add_subdirectory() from a downstream project. When
# consumed downstream, default examples/tests off so they don't get pulled
# into the parent's build target list, and avoid clobbering parent-level
# CMake settings (compile-commands export, output dirs) further down.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_CANLIBS_TOPLEVEL ON)
else()
    set(_CANLIBS_TOPLEVEL OFF)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# POSITION_INDEPENDENT_CODE moved onto the libcan target (see libcan/
# CMakeLists.txt) so it doesn't bleed into a parent project's choice
# when CanLibraries is consumed via add_subdirectory().

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
option(CAN_BACKEND_VECTOR "Build Vector XL backend (NOT YET IMPLEMENTED)" OFF)
# Vector XL Driver Library would land here, but the backend source
# (src/backends/vector_backend.cpp) and FindVectorXL.cmake module aren't
# in the tree yet. The option is kept visible so the planned shape is
# discoverable, but flipping it on fails at configure time with a clear
# message rather than dying later on a missing source file.
if(CAN_BACKEND_VECTOR)
    message(FATAL_ERROR
        "CAN_BACKEND_VECTOR is not yet implemented. There is no "
        "src/backends/vector_backend.cpp in the tree and no "
        "FindVectorXL.cmake module under cmake/. Disable this option "
        "(it is OFF by default) to proceed.")
endif()

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

# Export compile commands for IDE support, and pin output layout — only
# at top level so we don't override a parent project's choices when
# consumed via add_subdirectory().
if(_CANLIBS_TOPLEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Testing — call enable_testing() BEFORE add_subdirectory so tests added
# in subdirs register with ctest.
if(BUILD_TESTING)
    enable_testing()
endif()

# Add subdirectories
add_subdirectory(libcan)

# ----------------------------------------------------------------------------
# Vendored Vector_DBC / Vector_BLF / Vector_ASC submodules
# ----------------------------------------------------------------------------
# Tobias Lorenz's libraries (GPL-3.0). We pin via git submodules at
# third_party/. Force their internal options off so they don't drag in
# Doxygen/static-analysis tooling we don't want, and stop their tests
# from registering with our ctest.
set(OPTION_RUN_DOXYGEN     OFF CACHE BOOL "" FORCE)
set(OPTION_RUN_CCCC        OFF CACHE BOOL "" FORCE)
set(OPTION_RUN_CPPCHECK    OFF CACHE BOOL "" FORCE)
set(OPTION_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(OPTION_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(OPTION_USE_GCOV        OFF CACHE BOOL "" FORCE)
set(OPTION_USE_GPROF       OFF CACHE BOOL "" FORCE)
set(OPTION_ADD_LCOV        OFF CACHE BOOL "" FORCE)

# Each Toby submodule's CMakeLists.txt writes `set(CMAKE_BUILD_TYPE
# Release)` at the top. Today that's a non-cache local-scope assignment
# so our cached value survives unscathed, but if a future upstream
# release strengthens that to ... CACHE STRING ... FORCE) it would
# clobber the user's choice. Snapshot before the add_subdirectory()
# calls and restore after — defensive, no cost when untriggered.
option(CANLIB_USE_VECTOR_DBC "Build/expose third_party Vector_DBC" ON)
option(CANLIB_USE_VECTOR_BLF "Build/expose third_party Vector_BLF" ON)
option(CANLIB_USE_VECTOR_ASC "Build/expose third_party Vector_ASC" ON)

set(_canlib_saved_build_type "${CMAKE_BUILD_TYPE}")

# Helper: each Toby submodule's CMakeLists references ${CMAKE_SOURCE_DIR}/src
# expecting to be built standalone. When consumed via add_subdirectory that
# resolves to *our* source tree. Patch the include dirs after the fact so
# consumers see the right paths. SYSTEM suppresses pedantic warnings from
# their headers leaking into our builds. INSTALL_INTERFACE adds the
# include base directory of the installed package so downstream
# find_package() consumers can resolve <Vector/DBC.h>, <Vector/BLF.h>,
# <Vector/ASC.h> after we install the headers.
function(_canlib_fix_toby_includes target subdir)
    include(GNUInstallDirs)
    target_include_directories(${target} SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/${subdir}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/third_party/${subdir}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
endfunction()

# When this project is installable (standalone build), pull the Vector
# submodules into 'all' so `cmake --install` finds them built. When
# consumed via add_subdirectory, the parent typically doesn't install,
# and we keep EXCLUDE_FROM_ALL so unused submodules aren't compiled.
if(CANLIB_INSTALL)
    set(_canlib_subdir_extras "")
else()
    set(_canlib_subdir_extras "EXCLUDE_FROM_ALL")
endif()

if(CANLIB_USE_VECTOR_DBC)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vector_DBC/CMakeLists.txt)
        message(FATAL_ERROR "Vector_DBC submodule not initialized. Run: "
            "git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party/Vector_DBC ${_canlib_subdir_extras})
    _canlib_fix_toby_includes(Vector_DBC Vector_DBC)
    # The Vector_DBC flex/bison-generated Scanner.cpp triggers a
    # -Wimplicit-fallthrough on modern GCC. The SYSTEM include in
    # _canlib_fix_toby_includes suppresses warnings on consumers of the
    # *headers*, but not on Vector_DBC's own compilation, so silence it
    # at the source.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(Vector_DBC PRIVATE -Wno-implicit-fallthrough)
    endif()
    # Re-export with our namespace prefix so consumers refer to a stable
    # alias instead of the upstream's bare target name. EXPORT_NAME=dbc
    # makes the installed target `CanLibraries::dbc` directly.
    set_target_properties(Vector_DBC PROPERTIES EXPORT_NAME dbc)
    add_library(CanLibraries::dbc ALIAS Vector_DBC)
endif()

if(CANLIB_USE_VECTOR_BLF)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vector_BLF/CMakeLists.txt)
        message(FATAL_ERROR "Vector_BLF submodule not initialized.")
    endif()
    add_subdirectory(third_party/Vector_BLF ${_canlib_subdir_extras})
    _canlib_fix_toby_includes(Vector_BLF Vector_BLF)
    # Vector_BLF v2.4.2 sources use <cstdint> typedefs (uint8_t, etc.)
    # without explicitly including the header. Modern libstdc++ no
    # longer pulls <cstdint> in transitively, so the build fails.
    # Force-include it on every TU as a workaround until upstream
    # ships the fix. -include is a GCC/Clang flag.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(Vector_BLF PRIVATE -include cstdint)
        target_compile_options(Vector_BLF PUBLIC  -include cstdint)
    endif()
    set_target_properties(Vector_BLF PROPERTIES
        CXX_STANDARD 17
        EXPORT_NAME blf
    )
    add_library(CanLibraries::blf ALIAS Vector_BLF)
endif()

if(CANLIB_USE_VECTOR_ASC)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vector_ASC/CMakeLists.txt)
        message(FATAL_ERROR "Vector_ASC submodule not initialized.")
    endif()
    add_subdirectory(third_party/Vector_ASC ${_canlib_subdir_extras})
    _canlib_fix_toby_includes(Vector_ASC Vector_ASC)
    set_target_properties(Vector_ASC PROPERTIES EXPORT_NAME asc)
    add_library(CanLibraries::asc ALIAS Vector_ASC)
endif()

# Suppress Qt's AUTOMOC/AUTOUIC/AUTORCC on the Vector submodule targets.
# A Qt consumer that does `set(CMAKE_AUTOMOC ON)` before
# `add_subdirectory(can-libraries)` would otherwise have AUTOMOC sweep
# the Vector_DBC / Vector_ASC flex/bison-generated parser sources and
# emit a CMP0071 warning ("excluding GENERATED source files..."). These
# submodules are pure C++ parsers, not Qt code, so opting out is the
# right answer — no Q_OBJECT, no .ui, no .qrc to find.
foreach(_canlib_toby IN ITEMS Vector_DBC Vector_BLF Vector_ASC)
    if(TARGET ${_canlib_toby})
        set_target_properties(${_canlib_toby} PROPERTIES
            AUTOMOC OFF
            AUTOUIC OFF
            AUTORCC OFF
        )
    endif()
endforeach()
unset(_canlib_toby)

# Attach the Vector submodules to our export set so consumers of an
# installed CanLibraries can use CanLibraries::dbc / ::blf / ::asc
# straight from find_package(CanLibraries).
#
# Each submodule's CMakeLists sets INTERFACE_SOURCES to its public .h
# files as raw absolute source-dir paths. install(EXPORT) rejects that
# (paths in exported properties must be wrapped in $<BUILD_INTERFACE:>
# / $<INSTALL_INTERFACE:>). The submodule's own install(FILES) reads
# INTERFACE_SOURCES at install time to know which headers to install —
# which would fail similarly. We replace both: clear INTERFACE_SOURCES
# (so the export is portable), and install headers explicitly via
# install(DIRECTORY ...).
if(CANLIB_INSTALL)
    include(GNUInstallDirs)

    foreach(_canlib_toby IN ITEMS Vector_DBC Vector_BLF Vector_ASC)
        if(TARGET ${_canlib_toby})
            set_target_properties(${_canlib_toby} PROPERTIES INTERFACE_SOURCES "")
        endif()
    endforeach()
    unset(_canlib_toby)

    if(CANLIB_USE_VECTOR_DBC)
        install(TARGETS Vector_DBC
            EXPORT CanLibrariesTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY third_party/Vector_DBC/src/Vector/DBC/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/DBC
            FILES_MATCHING PATTERN "*.h"
        )
        install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/Vector_DBC/src/Vector/DBC/vector_dbc_export.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/DBC
        )
        # Top-level umbrella header (third_party/Vector_DBC/src/Vector/DBC.h)
        install(FILES third_party/Vector_DBC/src/Vector/DBC.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector
        )
    endif()
    if(CANLIB_USE_VECTOR_BLF)
        install(TARGETS Vector_BLF
            EXPORT CanLibrariesTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY third_party/Vector_BLF/src/Vector/BLF/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/BLF
            FILES_MATCHING PATTERN "*.h"
        )
        install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/Vector_BLF/src/Vector/BLF/vector_blf_export.h
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/Vector_BLF/src/Vector/BLF/config.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/BLF
        )
        install(FILES third_party/Vector_BLF/src/Vector/BLF.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector
        )
    endif()
    if(CANLIB_USE_VECTOR_ASC)
        install(TARGETS Vector_ASC
            EXPORT CanLibrariesTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY third_party/Vector_ASC/src/Vector/ASC/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/ASC
            FILES_MATCHING PATTERN "*.h"
        )
        install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/Vector_ASC/src/Vector/ASC/vector_asc_export.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector/ASC
        )
        install(FILES third_party/Vector_ASC/src/Vector/ASC.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vector
        )
    endif()
endif()
unset(_canlib_subdir_extras)

# Restore CMAKE_BUILD_TYPE if any submodule's CMakeLists clobbered it.
# This is a no-op today (their assignment is non-FORCE local-scope) but
# guards against a future upstream change.
if(NOT "${_canlib_saved_build_type}" STREQUAL "${CMAKE_BUILD_TYPE}")
    set(CMAKE_BUILD_TYPE "${_canlib_saved_build_type}"
        CACHE STRING "" FORCE)
endif()
unset(_canlib_saved_build_type)

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

    # Install CMake config files. The Find* modules ship alongside so
    # that a static-libcan consumer's find_dependency(PCANBasic|Kvaser)
    # call (made from CanLibrariesConfig.cmake when the corresponding
    # backend was compiled in) can resolve.
    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/CanLibrariesConfigVersion.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPCANBasic.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindKvaser.cmake
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
