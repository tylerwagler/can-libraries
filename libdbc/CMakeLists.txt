# DBC Parser Library CMakeLists.txt
# Builds libdbc - DBC file parsing library

cmake_minimum_required(VERSION 3.25)

# Library target
add_library(libdbc
    src/dbc_parser.cpp
)

# Alias for namespaced usage
add_library(CanLibraries::dbc ALIAS libdbc)

# Set include directories
target_include_directories(libdbc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Set compile features
target_compile_features(libdbc PUBLIC cxx_std_23)

# Set library properties
set_target_properties(libdbc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/dbc/dbc_parser.h
)

# Installation rules
if(CANLIB_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS libdbc
        EXPORT CanLibrariesTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dbc
    )
endif()

# Tests
if(BUILD_TESTING)
    add_executable(test_dbc_parser
        test/test_dbc_parser.cpp
    )
    target_link_libraries(test_dbc_parser PRIVATE libdbc)
    target_compile_features(test_dbc_parser PRIVATE cxx_std_23)
    
    add_test(NAME DbcParserTest COMMAND test_dbc_parser)
endif()
