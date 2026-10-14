# The bundled Lua 4 interpreter: the legacy scripting client, built as lua4.
# Added by the top-level CMakeLists, which supplies the C++ standard and the
# output directory -- this is a subdirectory of the femm-qt build, not a
# project of its own.
#
# liblua/ is the patched Lua 4.0, and the only copy of it in the tree. common/
# holds what the interpreter needs from femm-qt: utf8_io (a copy of common/,
# kept self-contained here exactly as tangle keeps its own) and a complex.cpp
# that is NOT the one in common/ -- this one carries the display precision the
# lua4 REPL prints with.
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/liblua")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(SOCKET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luasocket-1.0")

# utf8_io.cpp makes the interpreter's file I/O and command line Unicode-aware
# on Windows -- an umlaut in a script's path has to survive the ANSI argv the
# CRT hands a narrow main().
file(GLOB LUA_SOURCES "${LUA_DIR}/*.cpp")
add_library(lua4 STATIC ${LUA_SOURCES} "${COMMON_DIR}/utf8_io.cpp")
target_include_directories(lua4 PUBLIC "${LUA_DIR}" "${COMMON_DIR}")
target_compile_definitions(lua4 PRIVATE "I=CComplex(0,1)")
if(WIN32)
    # femm_utf8_args() calls CommandLineToArgvW (shell32).
    target_link_libraries(lua4 PUBLIC shell32)
endif()

# CComplex number type
add_library(complex STATIC "${COMMON_DIR}/complex.cpp")
target_include_directories(complex PUBLIC "${COMMON_DIR}")
target_compile_definitions(complex PRIVATE "I=CComplex(0,1)")

# lua.c and luasocket.c are C sources compiled as C++ (they pull in CComplex
# via lua.h, so they must see the C++ CComplex type). Marking them LANGUAGE CXX
# makes CMake invoke the C++ compiler, but Clang then warns that implicitly
# "treating 'c' input as 'c++'" is deprecated. Pass -x c++ to state the input
# language explicitly and silence that warning. The flag is understood by GCC
# and Clang alike; skip it on MSVC, which is not used to build femm-qt.
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
# No femm_add_version_resource here: lua4 is the bundled Lua 4.0 interpreter
# (TeCGraf, PUC-Rio), not part of the FEMM product, and FEMM's version and
# copyright strings would misattribute it.

# The client-side halves of the Lua interface -- the binding femm.lua and the
# launcher -- staged beside the interpreter that runs them. lua4 alone is not
# usable: femm-lua reads femm.lua from its own directory, and refuses to start
# without it.
#
# All four build scripts already copy these -- win_build.bat:123,
# linux_build.sh:91, freebsd_build.sh:119, macos_build.sh:215. What this adds is
# that a plain `ninja`, which is how the tree is usually built during
# development, is enough on its own. That is the case that went stale: bin/
# femm.lua was two months behind because nobody had run a build script in
# between. The copies in those scripts then find the file already current.
#
# Its own ALL target, not a POST_BUILD on lua: hung off the executable the copy
# would only run when the C++ relinks, so editing femm.lua -- which never
# triggers a relink, being data -- would leave a stale copy staged and the next
# run would exercise the old binding. That is exactly how bin/femm.lua came to
# be two months behind its source. A custom target is always considered out of
# date, and copy_if_different makes that free when nothing changed. Same
# reasoning, and the same shape, as libfemm's pybridge_python target.
#
# The launcher is platform-specific: .bat is what femm-qt.iss ships on Windows,
# .sh is what the Unix installers ship. copy_if_different preserves the mode,
# so femm-lua.sh keeps the executable bit it carries in git.
if(WIN32)
    set(FEMMLUA_LAUNCHER "femm-lua.bat")
else()
    set(FEMMLUA_LAUNCHER "femm-lua.sh")
endif()

add_custom_target(luafemm_client ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/luafemm/femm.lua"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/luafemm/${FEMMLUA_LAUNCHER}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMENT "Staging the Lua client files (femm.lua, ${FEMMLUA_LAUNCHER}) into bin/")
add_dependencies(lua luafemm_client)
