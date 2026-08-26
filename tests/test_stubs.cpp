// test_stubs.cpp - link-time stubs for symbols that live in TUs not linked
// into the lightweight test harness (script_callbacks.cpp, py_state_globals.cpp).
#include "script_callbacks.h"

ViewCommandCallback get_view_command_callback() { return nullptr; }
MessageBoxCallback get_message_box_callback() { return nullptr; }
void register_global_femm_funcs() {}
void cleanup_global_femm_state() {}
std::string exeDir() { return ""; }
