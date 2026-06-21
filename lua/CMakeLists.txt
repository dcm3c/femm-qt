cmake_minimum_required(VERSION 3.16)
project(lua4cli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone: the patched Lua 4 and CComplex sources are vendored locally here
# (copied from femm-qt/solvers/{liblua,common}) so lua4cli builds with no
# dependency on the femm-qt tree.
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/liblua")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(SOCKET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luasocket-1.0")

# Lua 4 library (vendored copy of the femm-qt lua4 sources, now local)
file(GLOB LUA_SOURCES "${LUA_DIR}/*.cpp")
add_library(lua4 STATIC ${LUA_SOURCES})
target_include_directories(lua4 PUBLIC "${LUA_DIR}" "${COMMON_DIR}")
target_compile_definitions(lua4 PRIVATE "I=CComplex(0,1)")

# CComplex number type
add_library(complex STATIC "${COMMON_DIR}/complex.cpp")
target_include_directories(complex PUBLIC "${COMMON_DIR}")
target_compile_definitions(complex PRIVATE "I=CComplex(0,1)")

# lua.c and luasocket.c are C sources compiled as C++ (they pull in CComplex
# via lua.h, so they must see the C++ CComplex type). Marking them LANGUAGE CXX
# makes CMake invoke the C++ compiler, but Clang then warns that implicitly
# "treating 'c' input as 'c++'" is deprecated. Pass -x c++ to state the input
# language explicitly and silence that warning. The flag is understood by GCC
# and Clang alike; skip it on MSVC, which never builds this subproject.
if(NOT MSVC)
    set(LUA_CXX_INPUT_FLAG "-x;c++")
endif()

# LuaSocket 1.0 (TCP/IP for Lua 4)
set_source_files_properties("${SOCKET_DIR}/luasocket.c" PROPERTIES
    LANGUAGE CXX COMPILE_OPTIONS "${LUA_CXX_INPUT_FLAG}")
add_library(luasocket STATIC "${SOCKET_DIR}/luasocket.c")
target_include_directories(luasocket PUBLIC "${SOCKET_DIR}" "${LUA_DIR}" "${COMMON_DIR}")
if(WIN32)
    target_link_libraries(luasocket PUBLIC ws2_32)
endif()

# CLI interpreter — compile lua.c as C++ since it pulls in CComplex via lua.h
set_source_files_properties(lua.c PROPERTIES
    LANGUAGE CXX COMPILE_OPTIONS "${LUA_CXX_INPUT_FLAG}")
add_executable(lua lua.c)
target_link_libraries(lua PRIVATE lua4 complex luasocket)
target_include_directories(lua PRIVATE "${LUA_DIR}" "${COMMON_DIR}" "${SOCKET_DIR}")

# On-disk command name is "lua4". The CMake target stays "lua" to avoid
# colliding with the static-library target "lua4" (different artifact types,
# so the .exe and the .a/.lib never clash on disk).
set_target_properties(lua PROPERTIES OUTPUT_NAME lua4)
