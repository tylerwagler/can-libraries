cmake_minimum_required(VERSION 3.25)

# Backend example — exercises ICanBackend.
add_executable(backend_example backend_example.cpp)
target_link_libraries(backend_example PRIVATE CanLibraries::can)

# DBC parser example — only build when the Vector_DBC submodule is being
# pulled in. With CANLIB_USE_VECTOR_DBC=OFF the CanLibraries::dbc target
# doesn't exist and the example otherwise dies at config time.
if(CANLIB_USE_VECTOR_DBC)
    add_executable(dbc_example dbc_example.cpp)
    target_link_libraries(dbc_example PRIVATE CanLibraries::dbc)
endif()
