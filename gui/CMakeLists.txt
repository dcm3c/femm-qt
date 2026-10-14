# Layer 3: Qt 6 GUI (optional, depends on Qt6::Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(femm_gui
    main.cpp
    mainwindow.cpp
    editor_widget.cpp
    mouse_release_gate.cpp
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
femm_add_version_resource(femm_gui "FEMM")
# femm.rc says ICON "icons/femm.ico", but nothing scans a .rc for what it
# includes -- ninja reported "#deps 0" for femm.rc.obj -- so replacing the icon
# rebuilt nothing and the executable kept the old one.
#
# It hid well: icons.qrc embeds the SAME file for the window icon and that path
# does rebuild, so the icon inside the running program updated while the one
# Explorer and the taskbar show stayed four days stale.
if(WIN32)
    set_property(SOURCE femm.rc APPEND PROPERTY OBJECT_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/icons/femm.ico")
endif()

target_include_directories(femm_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(femm_gui PRIVATE femm_ipc Qt6::Widgets)
target_compile_options(femm_gui PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-unused-result
)

# The minimum macOS the bundle declares (LSMinimumSystemVersion in
# Info.plist.in). macos_build.sh passes FEMM_MACOS_MIN alongside
# CMAKE_OSX_DEPLOYMENT_TARGET -- the Qt's own floor unless overridden -- and
# step 4c there refuses a bundle whose binaries declare anything higher. A
# bare cmake run without either falls back to the deployment target, and
# failing that to the macOS this is being built on, which is what clang
# targets when told nothing.
if(APPLE)
    if(NOT FEMM_MACOS_MIN)
        set(FEMM_MACOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(NOT FEMM_MACOS_MIN)
        execute_process(COMMAND sw_vers -productVersion
                        OUTPUT_VARIABLE _host_macos OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1.\\2" FEMM_MACOS_MIN "${_host_macos}")
    endif()
    set(MACOSX_BUNDLE_MIN_MACOS "${FEMM_MACOS_MIN}")
endif()

# Suppress console window on Windows/macOS
set_target_properties(femm_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

# The application icon, macOS only. Two halves are both required and neither
# reports the other missing: Info.plist.in names femm.icns in CFBundleIconFile,
# and the file itself has to be IN Contents/Resources. Without both, Finder
# draws the blank placeholder icon and says nothing about why -- which is what
# it did until 2026-08-23.
#
# MACOSX_PACKAGE_LOCATION is what puts it there, and it is why the file is
# listed as a source: CMake treats a source with that property as a bundle
# resource to copy rather than something to compile. Guarded by APPLE so the
# other platforms never see a .icns in their source list. Windows takes its
# icon from the .rc instead, and Linux from the .desktop entry's Icon= key
# (see packaging/unix_install.sh).
if(APPLE)
    set(FEMM_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/icons/femm.icns")
    target_sources(femm_gui PRIVATE "${FEMM_ICNS}")
    set_source_files_properties("${FEMM_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()
