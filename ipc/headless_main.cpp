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

#include <csignal>
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
static void sigint_handler(int)
{
    if (g_repl_mode || py_is_running()) {
        py_request_interrupt();
        std::signal(SIGINT, sigint_handler);
    } else {
        std::signal(SIGINT, SIG_DFL);
        std::raise(SIGINT);
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

int main(int argc, char *argv[])
{
    fprintf(stderr, "FEMM headless engine v5.0\n");

    // Layer 1: initialize Python
    init_py();

    // Ctrl+C interrupts a running script/command when launched from a terminal.
    std::signal(SIGINT, sigint_handler);

    // Parse arguments
    int port = 48015;
    bool portGiven = false;
    bool runAndExit = false;
    bool wantRepl = false;
    const char *script = nullptr;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-port") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
            portGiven = true;
        } else if (strcmp(argv[i], "-run") == 0 && i + 1 < argc) {
            runAndExit = true;
            script = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            wantRepl = true;
        } else if (!script) {
            script = argv[i];
        }
    }

    // Interactive REPL when asked for explicitly (-i), or when launched bare at
    // a terminal (no --port, no script, stdin is a TTY). The bindings always
    // pass --port, so they never land here. The REPL does not also serve TCP.
    bool replMode = wantRepl ||
        (!portGiven && !runAndExit && script == nullptr && stdin_is_tty());

    if (replMode) {
        if (script) {   // `-i script.py`: run the script first, then drop to the REPL
            fprintf(stderr, "Executing: %s\n", script);
            char errmsg[1024] = {};
            if (exec_python_file(script, errmsg, sizeof(errmsg)) != 0)
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

    // Execute startup script if provided
    if (script) {
        fprintf(stderr, "Executing: %s\n", script);
        char errmsg[1024] = {};
        int status = exec_python_file(script, errmsg, sizeof(errmsg));
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
                close_py();
                return interrupted ? 130 : 1;
            }
        }
        if (runAndExit) {
            fprintf(stderr, "Script complete, exiting.\n");
            close_py();
            return 0;
        }
    }

    // Run event loop (blocks until quit)
    server.run();

    close_py();
    return 0;
}
