# Layer 2: IPC server + headless binary (no Qt dependency)

add_library(femm_ipc STATIC
    tcp_server.cpp
)
target_include_directories(femm_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(femm_ipc PUBLIC libfemm)
if(WIN32)
    target_link_libraries(femm_ipc PUBLIC ws2_32)
endif()

# Headless executable(s).
# -static: avoid DLL ABI mismatches on Windows (not supported on macOS).
#
# On Windows we build TWO variants from the same source, following the
# python.exe / pythonw.exe convention:
#   femm_headless  - console subsystem: run from a terminal, the shell waits
#                    for it, stdout/stderr go to the console, and Ctrl+C
#                    interrupts a running script (see the SIGINT handler in
#                    headless_main.cpp). For interactive / batch / CI use.
#   femm_headlessw - GUI subsystem (-mwindows): no console window ever, so the
#                    language bindings can spawn it silently. The SIGINT
#                    handler is simply inert here (no console -> no signal),
#                    which is fine: bindings kill the process directly.
# On Linux/macOS there is no subsystem concept (a spawned process never pops a
# window and SIGINT already works in a terminal), so only femm_headless is
# built and the bindings spawn it directly.
add_executable(femm_headless headless_main.cpp)
target_link_libraries(femm_headless PRIVATE femm_ipc)
target_link_options(femm_headless PRIVATE $<$<PLATFORM_ID:Windows>:-static>)

if(WIN32)
    add_executable(femm_headlessw headless_main.cpp)
    target_link_libraries(femm_headlessw PRIVATE femm_ipc)
    target_link_options(femm_headlessw PRIVATE -static -mwindows)
endif()
