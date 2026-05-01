cmake_minimum_required(VERSION 3.25)

# Backend example — exercises ICanBackend.
add_executable(backend_example backend_example.cpp)
target_link_libraries(backend_example PRIVATE CanLibraries::can)

# DBC parser example — exercises libdbc independently of libcan.
add_executable(dbc_example dbc_example.cpp)
target_link_libraries(dbc_example PRIVATE CanLibraries::dbc)
