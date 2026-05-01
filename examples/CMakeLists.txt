# Examples CMakeLists.txt
# Builds example programs demonstrating library usage

cmake_minimum_required(VERSION 3.16)

# CAN Interface Example
add_executable(can_example
    can_example.cpp
)

target_link_libraries(can_example
    PRIVATE
        CanLibraries::can
)

# DBC Parser Example
add_executable(dbcc_example
    dbc_example.cpp
)

target_link_libraries(dbcc_example
    PRIVATE
        CanLibraries::dbc
)

# Combined Example (CAN + DBC)
add_executable(combined_example
    combined_example.cpp
)

target_link_libraries(combined_example
    PRIVATE
        CanLibraries::can
        CanLibraries::dbc
)
