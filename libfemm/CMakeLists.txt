# Layer 1: libfemm — core FEMM engine (no Qt dependency)
add_library(libfemm STATIC
    py_state.cpp
    py_state_globals.cpp
    py_bind.cpp
    py_bind_type.c
    py_init_script.cpp
    script_callbacks.cpp
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
target_link_libraries(libfemm PUBLIC micropython_embed femm_common tangle_lib
    Threads::Threads)
target_compile_options(libfemm PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-write-strings
    -Wno-unused-result
)
