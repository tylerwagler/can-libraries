# FindKvaser.cmake
#
# Locates the Kvaser canlib SDK.
#
# Linux: typical install via kvlibsdk drops headers in /usr/include and
#   libcanlib.so in /usr/lib (or /usr/lib64).
# Windows: from the SDK installer, default at
#   C:/Program Files (x86)/Kvaser/Canlib/, with headers under INC/ and
#   import library under Lib/x64 (or Lib/x86).
#
# Variables on success:
#   Kvaser_FOUND
#   Kvaser_INCLUDE_DIR
#   Kvaser_LIBRARY
#
# Imported target:
#   Kvaser::canlib
#
# Hint variable:
#   KVASER_ROOT — root directory of an SDK install

set(_kvaser_search_paths "")
if(DEFINED KVASER_ROOT)
    list(APPEND _kvaser_search_paths "${KVASER_ROOT}")
endif()

if(WIN32)
    list(APPEND _kvaser_search_paths
        "C:/Program Files (x86)/Kvaser/Canlib"
        "C:/Program Files/Kvaser/Canlib"
    )
endif()

find_path(Kvaser_INCLUDE_DIR
    NAMES canlib.h
    HINTS ${_kvaser_search_paths}
    PATH_SUFFIXES INC include
)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_kvaser_lib_subdir "Lib/x64" "Lib/MS")
        set(_kvaser_lib_names canlib32 canlib)
    else()
        set(_kvaser_lib_subdir "Lib/x86" "Lib/Win32" "Lib/MS")
        set(_kvaser_lib_names canlib32 canlib)
    endif()
    find_library(Kvaser_LIBRARY
        NAMES ${_kvaser_lib_names}
        HINTS ${_kvaser_search_paths}
        PATH_SUFFIXES ${_kvaser_lib_subdir}
    )
else()
    find_library(Kvaser_LIBRARY
        NAMES canlib
        HINTS ${_kvaser_search_paths}
        PATH_SUFFIXES lib lib64
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Kvaser
    REQUIRED_VARS Kvaser_LIBRARY Kvaser_INCLUDE_DIR
)

if(Kvaser_FOUND AND NOT TARGET Kvaser::canlib)
    add_library(Kvaser::canlib UNKNOWN IMPORTED)
    set_target_properties(Kvaser::canlib PROPERTIES
        IMPORTED_LOCATION "${Kvaser_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Kvaser_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Kvaser_INCLUDE_DIR Kvaser_LIBRARY)
