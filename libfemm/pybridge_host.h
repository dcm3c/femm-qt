// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// pybridge_host.h - lifecycle and transport for the CPython worker process
// that gives scripts matplotlib (design: pybridge-implementation-plan.md).
//
// femm-qt listens, the worker connects back, and every exchange is one
// synchronous length-prefixed frame each way. This file owns the socket, the
// child process and the handshake; it knows nothing about what the frames mean.
// All protocol logic lives in Python (pybridge.py, milestone M3).
//
// No Qt here, deliberately, though the plan first placed the host in gui/:
// libfemm is Qt-free and femm_headless links only libfemm, so a Qt host would
// deny the bridge to `femm_headless -run` -- which is exactly where batch
// plotting happens and where the whole test suite runs. Both halves already
// have a non-Qt precedent in this tree: sockets in ipc/tcp_server.cpp, process
// spawning in script_callbacks.cpp. The GUI contributes only its event pump,
// through the VM hook it already installs.
#ifndef PYBRIDGE_HOST_H
#define PYBRIDGE_HOST_H

#include <stdexcept>
#include <string>
#include <vector>

// Every failure reaching MicroPython is one of these; py_pybridge.cpp turns it
// into OSError, so a script says `except OSError` for a dead worker exactly as
// it does for a missing file.
struct pybridge_error : std::runtime_error {
    explicit pybridge_error(const std::string &what) : std::runtime_error(what) {}
};

// Spawn the worker and complete the handshake if it is not already running.
// Idempotent and lazy: nothing is spawned until a script first touches the
// bridge. Pumps the GUI and honours a pending KeyboardInterrupt throughout --
// discovery can take tens of seconds when several interpreters must be tried.
// Throws pybridge_error with an actionable message if no usable Python exists.
void femm_pybridge_ensure_started();

// One round trip. `frame` is a complete outgoing frame; the return value is the
// complete reply. Blocks, pumping the GUI on the established cadence.
// Throws pybridge_error if the worker dies, if the call is nested, or if the
// user interrupts (in which case the worker is discarded -- see the .cpp).
std::string femm_pybridge_rpc(const std::string &frame);

// Generation counter: bumps on every respawn, restart and interpreter reset.
// The Python client tags proxies with it so a handle from a dead worker fails
// loudly instead of resolving against a new worker's table.
long femm_pybridge_session();

// True while the worker is up and past the handshake.
bool femm_pybridge_running();

// The worker's `hello` reply, verbatim: the whole response frame's JSON, so
// the version report is at ["v"] and not at the top level. Kept verbatim
// because unwrapping it here would mean parsing JSON properly in C++, which
// this layer deliberately does not do. Empty when not started.
const std::string &femm_pybridge_info();

// Stop the worker: `shutdown` op, then terminate if it does not go quietly.
// Safe to call when nothing is running. Bumps the session.
void femm_pybridge_shutdown();

// Kill and forget, so the next use spawns afresh. Bumps the session.
void femm_pybridge_restart();

// Interpreter selection. An empty path restores automatic discovery. Setting
// it stops any running worker, since the point is to use a different Python.
void femm_pybridge_set_interpreter(const std::string &path);
std::string femm_pybridge_interpreter();

// Backend override, passed to the worker as FEMMQT_PYBRIDGE_BACKEND. Empty =
// let the worker probe. Takes effect at the next spawn.
void femm_pybridge_set_backend(const std::string &backend);
std::string femm_pybridge_backend();

// Interpreters tried and why each was rejected, most recent attempt first.
// The GUI shows this when discovery fails; it is the whole support surface.
const std::vector<std::string> &femm_pybridge_attempts();

#endif  // PYBRIDGE_HOST_H
