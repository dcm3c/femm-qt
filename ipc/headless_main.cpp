// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// headless_main.cpp — FEMM headless analysis engine
//
// Initializes the Python engine and then does one of three things:
//   * serves the TCP IPC protocol (what the language bindings spawn), or
//   * runs a script from the command line, or
//   * presents an interactive MicroPython REPL at the terminal.
//
// Usage:
//   femm_headless                 # REPL if stdin is a terminal, else TCP server
//   femm_headless -i [script.py]  # force the REPL (runs script first, if given)
//   femm_headless --port N        # TCP server on port N (what the bindings use)
//   femm_headless script.py       # run script, then serve
//   femm_headless -run script.py  # run script and exit (batch / CI)
//
// The bindings always pass --port, so they always land in server mode; the REPL
// only appears for a human running it bare at a terminal (or with -i).

#include "py_state.h"
#include "tcp_server.h"
#include "script_callbacks.h"   // set_exit_hook
#include "utf8_io.h"

#include <csignal>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

// Set once the REPL loop is active (read by the signal handler).
static volatile std::sig_atomic_t g_repl_mode = 0;

// SIGINT (Ctrl+C) handling, for when femm_headless is run from a shell. The TCP
// clients launch it detached with no controlling terminal, so this is inert
// there. While a script/command is running, Ctrl+C interrupts it (raises
// KeyboardInterrupt at the next VM branch via the shared py_request_interrupt
// mechanism). In the REPL, an idle Ctrl+C (at the prompt) is a harmless no-op:
// it sets a pending exception that exec_python clears at the next entry. Only a
// non-REPL idle process (an idle server / finished batch run) falls through to
// the default action and exits. Re-armed each time because both Windows and
// System-V signal() reset the disposition to default before invoking the handler.
static void sigint_handler(int sig)
{
    if (g_repl_mode || py_is_running()) {
        py_request_interrupt();
        std::signal(sig, sigint_handler);
    } else {
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

static bool stdin_is_tty()
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

// ---------------------------------------------------------------------------
// Tier-1 interactive REPL
// ---------------------------------------------------------------------------

static bool is_blank(const std::string &s)
{
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

static std::string rstrip(const std::string &s)
{
    size_t e = s.size();
    while (e > 0 && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r' || s[e-1] == '\n'))
        --e;
    return s.substr(0, e);
}

// Net depth of ()/[]/{} in s, ignoring brackets inside '...'/"..." strings and
// after a '#' comment. Triple-quoted strings aren't special-cased — good enough
// for deciding when an interactively typed statement is still open.
static int bracket_depth(const std::string &s)
{
    int depth = 0;
    char q = 0;  // active quote char, or 0
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (q) {
            if (c == '\\' && i + 1 < s.size()) ++i;   // skip escaped char
            else if (c == q || c == '\n') q = 0;      // close (or unterminated)
            continue;
        }
        if (c == '#') { while (i < s.size() && s[i] != '\n') ++i; continue; }
        else if (c == '\'' || c == '"') q = c;
        else if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
    }
    return depth;
}

static void run_repl()
{
    std::fprintf(stderr,
        "MicroPython REPL - enter FEMM/Python commands.\n"
        "Type 'exit' to quit (or Ctrl+Z then Enter on Windows; Ctrl+D on macOS/Linux).\n"
        "Ctrl+C interrupts a running command.\n");

    std::string buf, line;
    bool blockMode = false;   // inside a compound (def/for/if/...) block

    while (true) {
        std::fputs(buf.empty() ? ">>> " : "... ", stdout);
        std::fflush(stdout);

        if (!std::getline(std::cin, line)) {   // EOF (Ctrl+D / Ctrl+Z)
            std::fputs("\n", stdout);
            break;
        }

        const bool firstLine = buf.empty();
        if (firstLine) {
            if (is_blank(line)) continue;      // nothing typed yet
            std::string r = rstrip(line);
            if (r == "exit" || r == "quit" || r == "exit()" || r == "quit()") break;
            buf = line;
        } else {
            buf += "\n";
            buf += line;
        }

        // Decide whether the accumulated input is a complete statement.
        int depth = bracket_depth(buf);
        std::string r = rstrip(buf);
        bool complete;
        if (depth > 0 || (!r.empty() && r.back() == '\\')) {
            complete = false;                                  // open bracket / line continuation
        } else if (!r.empty() && r.back() == ':') {
            blockMode = true;                                  // compound-statement header
            complete = false;
        } else if (blockMode) {
            complete = !firstLine && is_blank(line);           // a blank line ends the block
        } else {
            complete = true;                                   // simple statement
        }
        if (!complete) continue;

        char errmsg[1024] = {};
        int status = exec_python(buf.c_str(), errmsg, sizeof errmsg);
        if (status != 0) {
            std::fprintf(stdout, "%s\n",
                strcmp(errmsg, "KeyboardInterrupt") == 0 ? "KeyboardInterrupt" : errmsg);
        } else if (!py_last_display().empty()) {
            std::fprintf(stdout, "%s\n", py_last_display().c_str());
        }
        std::fflush(stdout);

        buf.clear();
        blockMode = false;
    }
}

// Change the working directory to the folder containing `script`, matching the
// GUI's Run-Script behavior so a script referenced by a path (e.g.
// `-run ../models/foo.py`) can open its companion files with relative names
// (open("foo.fem")).
//
// Returns the script's ABSOLUTE path, which the caller must use to run it: a
// relative path given on the command line is resolved against the old cwd, so
// once we chdir away it no longer points at the script ("../../examples/x.py"
// run from build/bin would be re-resolved against .../examples and miss).
//
// Unicode-safe on every platform: femm_path() builds the fs::path from UTF-8 --
// the plain fs::path(std::string) ctor would decode ANSI on Windows -- and
// generic_u8string() yields forward-slash UTF-8 that embeds safely in the Python
// string literal (native backslashes would be mangled by Python's escape
// parsing). The chdir itself is done by the setcurrentdirectory binding, which
// widens to _wchdir on Windows.
static std::string chdir_to_script_dir(const std::string &script)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path abs = fs::absolute(femm_path(script), ec);
    if (ec) return script;                      // leave cwd alone; run it as given
    auto u8 = abs.parent_path().generic_u8string();
    std::string dirStr(u8.begin(), u8.end());   // u8string (C++20) or string (C++17)
    std::string cmd = "setcurrentdirectory(\"" + dirStr + "\")";
    char errmsg[1024] = {};
    exec_python(cmd.c_str(), errmsg, sizeof(errmsg));
    return femm_u8string(abs);
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "FEMM headless engine v5.0\n");

    // Layer 1: initialize Python
    init_py();

    // Ctrl+C interrupts a running script/command when launched from a terminal.
    std::signal(SIGINT, sigint_handler);
#ifdef _WIN32
    // Ctrl+Break too, which on Windows is the companion "stop this console
    // program" key -- and the only console signal deliverable to another
    // process group, so it is also how a supervising process (or a test) can
    // interrupt a long run without killing it outright.
    std::signal(SIGBREAK, sigint_handler);
#endif

    // Parse arguments
    int port = 48015;
    bool portGiven = false;
    bool runAndExit = false;
    bool wantRepl = false;
    std::string script;               // empty => no script given
    bool haveScript = false;

    // Decode argv to UTF-8 so a Unicode script path (e.g. an umlaut in a folder
    // name) survives the ANSI argv the CRT hands a narrow main() on Windows.
    std::vector<std::string> args = femm_utf8_args(argc, argv);
    for (size_t i = 1; i < args.size(); i++) {
        if ((args[i] == "-port" || args[i] == "--port") && i + 1 < args.size()) {
            port = atoi(args[++i].c_str());
            portGiven = true;
        } else if (args[i] == "-run" && i + 1 < args.size()) {
            runAndExit = true;
            script = args[++i];
            haveScript = true;
        } else if (args[i] == "-i" || args[i] == "--interactive") {
            wantRepl = true;
        } else if (!haveScript) {
            script = args[i];
            haveScript = true;
        }
    }

    // Interactive REPL when asked for explicitly (-i), or when launched bare at
    // a terminal (no --port, no script, stdin is a TTY). The bindings always
    // pass --port, so they never land here. The REPL does not also serve TCP.
    bool replMode = wantRepl ||
        (!portGiven && !runAndExit && !haveScript && stdin_is_tty());

    if (replMode) {
        if (haveScript) {   // `-i script.py`: run the script first, then drop to the REPL
            fprintf(stderr, "Executing: %s\n", script.c_str());
            // chdir so the script's own relative paths resolve; run it by the
            // absolute path it hands back (the relative one dies with the cwd).
            const std::string absScript = chdir_to_script_dir(script);
            char errmsg[1024] = {};
            if (exec_python_file(absScript.c_str(), errmsg, sizeof(errmsg)) != 0)
                fprintf(stderr, "Script error: %s\n", errmsg);
        }
        g_repl_mode = 1;
        run_repl();
        close_py();
        return 0;
    }

    // Layer 2: TCP server (the path the language bindings use)
    TcpServer server;
    if (!server.listen(port)) {
        fprintf(stderr, "Failed to start IPC server on port %d\n", port);
        close_py();
        return 1;
    }

    // A scripted quit() must not leave the port advertised while the process
    // takes its time dying; see set_exit_hook in script_callbacks.h.
    static TcpServer *s_ipcServer = &server;
    set_exit_hook([]() { if (s_ipcServer) s_ipcServer->stop(); });

    // Execute startup script if provided
    if (haveScript) {
        fprintf(stderr, "Executing: %s\n", script.c_str());
        // chdir so the script's own relative paths resolve; run it by the
        // absolute path it hands back (the relative one dies with the cwd).
        const std::string absScript = chdir_to_script_dir(script);
        char errmsg[1024] = {};
        int status = exec_python_file(absScript.c_str(), errmsg, sizeof(errmsg));
        if (status != 0) {
            // A Ctrl+C interrupt surfaces as KeyboardInterrupt; report it
            // distinctly (and with the conventional 128+SIGINT exit code) so a
            // deliberate abort isn't mistaken for a script bug.
            bool interrupted = (strcmp(errmsg, "KeyboardInterrupt") == 0);
            if (interrupted)
                fprintf(stderr, "Interrupted.\n");
            else
                fprintf(stderr, "Script error: %s\n", errmsg);
            if (runAndExit) {
                // Listener first: close_py() is slow -- it also stops the
                // matplotlib worker -- and a port that is about to die must
                // not go on accepting. See TcpServer's disconnect path.
                server.stop();
                close_py();
                return interrupted ? 130 : 1;
            }
        }
        if (runAndExit) {
            fprintf(stderr, "Script complete, exiting.\n");
            server.stop();          // before close_py(); see above
            close_py();
            return 0;
        }
    }

    // Run event loop (blocks until quit)
    server.run();

    server.stop();                  // idempotent; makes the ordering explicit
    close_py();
    return 0;
}
