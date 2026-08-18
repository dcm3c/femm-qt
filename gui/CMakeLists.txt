# Layer 3: Qt 6 GUI (optional, depends on Qt6::Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(femm_gui
    main.cpp
    mainwindow.cpp
    editor_widget.cpp
    results_widget.cpp
    scalar_results_widget.cpp
    scalar_doc_view.cpp
    magnetics_handler.cpp
    electrostatics_handler.cpp
    heat_handler.cpp
    heat_prop_editors.cpp
    heat_matlib_dialog.cpp
    curr_prop_editors.cpp
    curr_matlib_dialog.cpp
    curr_handler.cpp
    py_line_edit.cpp
    prop_dialogs.cpp
    point_prop_editor_base.cpp
    prop_editors.cpp
    elec_prop_editors.cpp
    prop_list_dialog.cpp
    problem_dialog.cpp
    curve_dialog.cpp
    bh_dialog.cpp
    tk_dialog.cpp
    prefs_dialog.cpp
    scalar_prefs_dialog.cpp
    pre_prefs_dialog.cpp
    script_prefs_dialog.cpp
    solver_progress_dialog.cpp
    view_colors.cpp
    py_console.cpp
    matlib_dialog_base.cpp
    matlib_dialog.cpp
    matlib_tree.cpp
    elec_matlib_dialog.cpp
    plot_widget.cpp
    output_dialog.cpp
    copy_move_dialog.cpp
    arc_dialog.cpp
    icons.qrc
    $<$<PLATFORM_ID:Windows>:femm.rc>
)
target_include_directories(femm_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(femm_gui PRIVATE femm_ipc Qt6::Widgets)
target_compile_options(femm_gui PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-unused-result
)

# Suppress console window on Windows/macOS
set_target_properties(femm_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
