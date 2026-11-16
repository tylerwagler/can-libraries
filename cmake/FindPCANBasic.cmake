# FindPCANBasic.cmake
#
# Locates the PEAK-System PCANBasic API.
#
# On Linux, expects the headers and library from peak-linux-driver /
# libpcanbasic (typical install locations: /usr/include and /usr/lib).
#
# On Windows, expects the SDK from PEAK's installer, normally at
# C:/Program Files/PEAK-System/PCAN-Basic API/Include and .../x64 (or x86).
#
# On macOS, PEAK ships no driver; the supported path is UV Software's
# PCBUSB library ("macOS Library for PCAN-USB Interfaces", www.mac-can.com,
# freeware). Its header and library are named PCBUSB.h / libPCBUSB, and its
# install.sh puts them in /usr/local/include and /usr/local/lib. The API is
# function-compatible with PCANBasic (header synced to PEAK 4.9.0) and its
# PCAN_RECEIVE_EVENT yields a select()-able fd like libpcanbasic's, so
# consumers link the same PCANBasic::PCANBasic imported target.
#
# Variables set on success:
#   PCANBasic_FOUND
#   PCANBasic_INCLUDE_DIR
#   PCANBasic_LIBRARY
#
# Imported target:
#   PCANBasic::PCANBasic — link this from your target.
#
# Hint variables (set before find_package() to override search):
#   PCANBASIC_ROOT — root directory of an SDK install

set(_pcan_search_paths "")
if(DEFINED PCANBASIC_ROOT)
    list(APPEND _pcan_search_paths "${PCANBASIC_ROOT}")
endif()

if(APPLE)
    # PCBUSB's install.sh default prefix, plus /opt/homebrew in case a
    # Homebrew formula ever packages it.
    list(APPEND _pcan_search_paths /usr/local /opt/homebrew)

    find_path(PCANBasic_INCLUDE_DIR
        NAMES PCBUSB.h
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES include
    )

    find_library(PCANBasic_LIBRARY
        NAMES PCBUSB
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES lib
    )
elseif(WIN32)
    list(APPEND _pcan_search_paths
        "C:/Program Files/PEAK-System/PCAN-Basic API"
        "C:/Program Files (x86)/PEAK-System/PCAN-Basic API"
    )

    find_path(PCANBasic_INCLUDE_DIR
        NAMES PCANBasic.h
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES Include include
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_pcan_lib_subdir "x64/VC_LIB" "x64")
    else()
        set(_pcan_lib_subdir "x86/VC_LIB" "x86" "Win32")
    endif()
    find_library(PCANBasic_LIBRARY
        NAMES PCANBasic
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES ${_pcan_lib_subdir}
    )
else()
    find_path(PCANBasic_INCLUDE_DIR
        NAMES PCANBasic.h
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES Include include
    )

    find_library(PCANBasic_LIBRARY
        NAMES pcanbasic libpcanbasic
        HINTS ${_pcan_search_paths}
        PATH_SUFFIXES lib lib64
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCANBasic
    REQUIRED_VARS PCANBasic_LIBRARY PCANBasic_INCLUDE_DIR
)

if(PCANBasic_FOUND AND NOT TARGET PCANBasic::PCANBasic)
    add_library(PCANBasic::PCANBasic UNKNOWN IMPORTED)
    set_target_properties(PCANBasic::PCANBasic PROPERTIES
        IMPORTED_LOCATION "${PCANBasic_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PCANBasic_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(PCANBasic_INCLUDE_DIR PCANBasic_LIBRARY)
