# Layer 1: libfemm — core FEMM engine (no Qt dependency)
add_library(libfemm STATIC
    py_state.cpp
    py_state_globals.cpp
    py_clinalg.cpp
    py_os.cpp
    py_pybridge.cpp
    pybridge_host.cpp
    py_bind.cpp
    py_bind_type.c
    py_init_script.cpp
    script_callbacks.cpp
    material_lib_path.cpp
    document.cpp
    mag_document.cpp
    elec_document.cpp
    heat_types.cpp
    heat_document.cpp
    heat_results_doc.cpp
    py_mag_editor.cpp
    py_elec_editor.cpp
    py_heat_editor.cpp
    results_doc.cpp
    py_mag_postproc.cpp
    elec_results_doc.cpp
    py_elec_postproc.cpp
    py_heat_postproc.cpp
    curr_types.cpp
    curr_document.cpp
    curr_results_doc.cpp
    py_curr_editor.cpp
    py_curr_postproc.cpp
    dxf_io.cpp
    femm_makeabc.cpp
)
target_include_directories(libfemm PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${COMMON_DIR}"
    "${SOLVER_DIR}/fkn"
)
# py_clinalg.cpp marshals ulab ndarrays; it needs ulab's headers (and the
# embed's MicroPython headers, already reachable via micropython_embed).
target_include_directories(libfemm PRIVATE "${MPY_DIR}/ulab/code")
target_link_libraries(libfemm PUBLIC micropython_embed femm_common tangle_lib
    Threads::Threads)
# pybridge_host.cpp talks to the matplotlib worker over a loopback socket.
# Winsock is not linked by default under MinGW the way it is by MSVC's pragma.
if(WIN32)
    target_link_libraries(libfemm PUBLIC ws2_32)
endif()

# The bridge's Python halves ship beside the binaries, in the same python/
# directory init_py() already puts on sys.path: the CPython worker, the
# MicroPython client, and the matplotlib forwarding stubs. Copied rather than
# generated or frozen -- they are ordinary source, and users are meant to be
# able to read and even edit them.
#
# Its own target rather than a POST_BUILD on libfemm: hung off libfemm, the
# copy would only run when the C++ relinks, so editing a .py would leave a
# stale file staged and the next test run would exercise the old code. A custom
# target is always considered out of date, and copy_if_different makes that
# free when nothing changed.
add_custom_target(pybridge_python ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pybridge-worker"
    # The worker goes in its own directory, not python/ beside the shims:
    # CPython puts a script's directory first on sys.path, so a worker sitting
    # next to python/matplotlib/ would import the MicroPython forwarding stub
    # as matplotlib -- which fails on `import _pybridge` and makes every
    # candidate interpreter look like it has no matplotlib at all.
    #
    # The name carries a hyphen deliberately. bin/ is the GUI's working
    # directory and therefore on MicroPython's sys.path AHEAD of python/, so a
    # directory called "pybridge" there is imported -- as an empty namespace
    # package -- in place of python/pybridge.py. A hyphen cannot be an
    # identifier, so this directory can never shadow a module.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/pybridge/worker/femmqt_pybridge_worker.py"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pybridge-worker/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/pybridge/mpy/pybridge.py"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pybridge/mpy/shims"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python"
    COMMENT "Staging the PyBridge python files into bin/python/")
add_dependencies(libfemm pybridge_python)

# pylib/ holds standard-library modules written in Python that the interpreter
# does not build in -- datetime and csv today. They land in the same bin/python/
# as the bridge files above, but they are not bridge files and start no worker:
# they run in-process and work with no CPython installed at all. Same
# always-out-of-date custom-target reasoning as pybridge_python.
#
# One line per module, deliberately: a glob over pylib/ would also stage its
# README and its vendored upstream test suite, and would go on staging a module
# after it had been deleted from the source tree.
add_custom_target(femm_pylib ALL
    # Packages: verbatim upstream in _core.py, femm-qt's additions beside it.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/datetime"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/datetime"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/itertools"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/itertools"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/operator"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/operator"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/shutil"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/shutil"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/tempfile"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/tempfile"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/logging"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/logging"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/re"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/re"
    # Single-file modules written here.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/pylib/csv.py"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/pylib/glob.py"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/pylib/textwrap.py"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/"
    # Single-file modules taken verbatim from micropython-lib, with the one
    # notice that covers all of them. Copied as a directory so that adding
    # another needs no CMake edit -- unlike the entries above, nothing here is
    # modified, so there is no femm-qt half to forget.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pylib/vendor"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python"
    COMMENT "Staging the pure-Python stdlib modules into bin/python/")
add_dependencies(libfemm femm_pylib)
# The four material libraries, staged beside the binaries that read them.
# libfemm/material_lib_path.cpp resolves them relative to exeDir() on Windows
# and Linux, so without these a build tree cannot open a material library at
# all -- MaterialLibTest.py fails with "Couldn't open .../matlib.dat".
#
# All four build scripts copy them (win_build.bat:120, linux_build.sh:76,
# freebsd_build.sh:103, and macos_build.sh:207 into the bundle's Resources/).
# What this adds is that a plain `ninja` suffices, which is how the tree is
# built during development and how a fresh build directory gets made.
#
# Its own ALL target for the same reason as pybridge_python above: hung off
# libfemm the copy would only run when the C++ relinks, and a .dat is data --
# editing one would never trigger it.
#
# macOS: this stages into bin/ as everywhere else, which makes a build tree
# self-sufficient. It does NOT put them back in the app bundle, which
# deliberately keeps its seeds in Contents/Resources/ (1effc98) -- macos_build.sh
# copies named executables into Contents/MacOS/, never bin/*.dat, and
# materialLibPath() tries ../Resources/ before exeDir() anyway.
add_custom_target(femm_matlibs ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/matlibs/matlib.dat"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/matlibs/statlib.dat"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/matlibs/heatlib.dat"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/matlibs/condlib.dat"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    COMMENT "Staging the material libraries into bin/")
add_dependencies(libfemm femm_matlibs)

target_compile_options(libfemm PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-write-strings
    -Wno-unused-result
)
