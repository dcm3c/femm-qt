// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// pybridge_host.cpp - see pybridge_host.h for what this is and why it carries
// no Qt.
//
// Three things here are worth knowing before reading:
//
//   * Discovery spawns a real worker per candidate interpreter rather than
//     running a separate probe script. The worker's `hello` reply already
//     reports python/numpy/matplotlib versions and the backend it actually
//     brought up, so a candidate that greets with no backend is exactly a
//     candidate that cannot plot. One mechanism, one JSON shape, and no
//     `python -c` child -- which matters on Windows, where popen() from a GUI
//     process is documented to hang.
//   * Blocking calls pump the GUI by calling the VM hook the GUI already
//     installed, rather than inventing a second pumping policy. Same function,
//     same 30 ms throttle, by construction.
//   * A pending KeyboardInterrupt aborts a wait by DISCARDING the worker. The
//     stream cannot be resynchronised mid-frame, and a worker wedged badly
//     enough to need Esc was going to be restarted anyway.
#include "pybridge_host.h"

#include "py_state.h"
#include "script_callbacks.h"
#include "utf8_io.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <cerrno>
#ifdef __APPLE__
// environ is not directly linkable from every macOS target; this is the
// documented accessor.
#include <crt_externs.h>
#define femm_environ (*_NSGetEnviron())
#else
extern char **environ;
#define femm_environ environ
#endif
typedef int socket_t;
#define INVALID_SOCK (-1)
#define closesocket close
#endif

namespace fs = std::filesystem;

namespace {

// Protocol constants shared with the worker. Kept minimal: this file builds
// exactly two frames (greeting check and hello) and parses exactly two.
const int kProtocol = 1;
const unsigned char kKindJson = 0x4A;  // 'J'
const size_t kMaxFrame = 256u * 1024u * 1024u;

// Spawn-to-hello. Generous on purpose: importing matplotlib off a cold network
// drive is slow, and being aggressive here just means failing on a machine
// that would have worked.
const int kHelloTimeoutMs = 15000;

// How often a blocking wait wakes to pump the GUI and check for an interrupt.
// Matches the console's own cadence (py_console.cpp pumps at most every 30 ms);
// this is the wait granularity, and the hook throttles itself.
const int kPumpIntervalMs = 20;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Host {
    socket_t sock = INVALID_SOCK;
    std::string token;
    std::string info;          // the hello reply, verbatim
    std::string interpreter;   // the one that worked, or the forced one
    std::string forced;        // explicit preference, "" = discover
    std::string backend;       // FEMMQT_PYBRIDGE_BACKEND override
    std::vector<std::string> attempts;
    std::string rx;            // bytes read but not yet consumed
    long session = 1;
    bool inFlight = false;
    bool started = false;
#ifdef _WIN32
    HANDLE proc = nullptr;
#else
    pid_t pid = -1;
#endif
};

Host &host() {
    static Host h;
    return h;
}

bool socketsReady() {
#ifdef _WIN32
    static bool once = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return once;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Just enough JSON for the handshake
//
// The C++ side must read two fields (the greeting's token, hello's ok/backend)
// before MicroPython may touch the connection; everything richer is parsed in
// Python. A scanner this small is honest about that boundary -- it is not a
// JSON parser and must not grow into one.
// ---------------------------------------------------------------------------

// Value text for "key" at the top level, unquoted for strings. Returns false
// if the key is absent. Good enough for the flat objects the worker sends.
bool jsonField(const std::string &doc, const std::string &key, std::string *out) {
    const std::string pat = "\"" + key + "\"";
    size_t k = doc.find(pat);
    if (k == std::string::npos) return false;
    size_t c = doc.find(':', k + pat.size());
    if (c == std::string::npos) return false;
    size_t i = doc.find_first_not_of(" \t\r\n", c + 1);
    if (i == std::string::npos) return false;
    if (doc[i] == '"') {
        std::string s;
        for (size_t j = i + 1; j < doc.size(); j++) {
            if (doc[j] == '\\' && j + 1 < doc.size()) {
                char e = doc[++j];
                s += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
                continue;
            }
            if (doc[j] == '"') { *out = s; return true; }
            s += doc[j];
        }
        return false;
    }
    size_t e = doc.find_first_of(",}", i);
    *out = doc.substr(i, (e == std::string::npos ? doc.size() : e) - i);
    // trim
    while (!out->empty() && (out->back() == ' ' || out->back() == '\r' ||
                             out->back() == '\n' || out->back() == '\t'))
        out->pop_back();
    return true;
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

std::string buildJsonFrame(const std::string &json) {
    std::string body;
    body.push_back((char)kKindJson);
    body += json;
    std::string frame(4, '\0');
    uint32_t n = (uint32_t)body.size();
    frame[0] = (char)(n & 0xFF);
    frame[1] = (char)((n >> 8) & 0xFF);
    frame[2] = (char)((n >> 16) & 0xFF);
    frame[3] = (char)((n >> 24) & 0xFF);
    return frame + body;
}

// A complete frame's payload, or npos-length if more bytes are needed.
// Returns the total frame size consumed (0 = incomplete).
size_t frameLength(const std::string &buf) {
    if (buf.size() < 4) return 0;
    uint32_t n = (uint32_t)(unsigned char)buf[0]
               | ((uint32_t)(unsigned char)buf[1] << 8)
               | ((uint32_t)(unsigned char)buf[2] << 16)
               | ((uint32_t)(unsigned char)buf[3] << 24);
    if (n < 1 || n > kMaxFrame)
        throw pybridge_error("python worker sent a malformed frame");
    if (buf.size() < 4 + (size_t)n) return 0;
    return 4 + (size_t)n;
}

// The JSON text of a complete frame. Binary frames carry a header length the
// handshake never needs, so this only handles kind 'J'; anything else during
// the handshake is a protocol violation.
std::string frameJson(const std::string &frame) {
    if (frame.size() < 5 || (unsigned char)frame[4] != kKindJson)
        throw pybridge_error("python worker sent an unexpected frame kind");
    return frame.substr(5);
}

// ---------------------------------------------------------------------------
// Pumping and interruption
// ---------------------------------------------------------------------------

void pump() {
    // The GUI's own hook: same function the VM calls every 1024 branches, so a
    // blocking bridge call keeps the UI exactly as alive as a busy script does.
    if (PyVmHook hook = py_vm_hook()) hook();
}

void killWorker() {
    Host &h = host();
#ifdef _WIN32
    if (h.proc) {
        TerminateProcess(h.proc, 1);
        WaitForSingleObject(h.proc, 2000);
        CloseHandle(h.proc);
        h.proc = nullptr;
    }
#else
    if (h.pid > 0) {
        ::kill(h.pid, SIGKILL);
        int status = 0;
        ::waitpid(h.pid, &status, 0);
        h.pid = -1;
    }
#endif
    if (h.sock != INVALID_SOCK) {
        closesocket(h.sock);
        h.sock = INVALID_SOCK;
    }
    h.started = false;
    h.info.clear();
    h.rx.clear();
    h.session++;
}

// Distinguishes "the user gave up" from "this candidate interpreter did not
// work out". Discovery must retry on the second and stop dead on the first,
// and both travel as exceptions through the same code.
struct bridge_interrupted : pybridge_error {
    bridge_interrupted() : pybridge_error("interrupted; worker discarded") {}
};

// Abort a wait because the user pressed Esc. The worker goes with it: half a
// frame has been written or read and there is no way to resynchronise, so the
// only honest move is to discard the session. The pending KeyboardInterrupt
// then fires normally at the next VM branch, once we have returned.
void abortForInterrupt() {
    killWorker();
    throw bridge_interrupted();
}

// ---------------------------------------------------------------------------
// Socket helpers. All waits pump and honour interrupts; none of them block for
// longer than kPumpIntervalMs at a stretch.
// ---------------------------------------------------------------------------

bool waitReadable(socket_t s, int ms) {
#ifdef _WIN32
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = select((int)s + 1, &rd, nullptr, nullptr, &tv);
    if (r == SOCKET_ERROR)
        throw pybridge_error("the connection to the python worker failed");
    return r > 0;
#else
    // poll, not select: a GUI process can hold more than FD_SETSIZE (1024)
    // descriptors open, and select() silently corrupts memory past that.
    struct pollfd p;
    p.fd = s;
    p.events = POLLIN;
    p.revents = 0;
    int r = ::poll(&p, 1, ms);
    if (r < 0) {
        if (errno == EINTR) return false;   // a signal; the caller loops
        throw pybridge_error("the connection to the python worker failed");
    }
    return r > 0;
#endif
}

void sendAll(socket_t s, const std::string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        // MSG_NOSIGNAL where it exists (Linux, FreeBSD): without it, writing
        // to a socket whose peer has died raises SIGPIPE, whose default action
        // terminates the process -- so "the worker crashed" would take femm-qt
        // down with it rather than raising. macOS has no MSG_NOSIGNAL and uses
        // the SO_NOSIGPIPE socket option instead, set where the socket is made.
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        int n = (int)::send(s, data.data() + sent,
                            (int)(data.size() - sent), flags);
        if (n < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAEINTR || e == WSAEWOULDBLOCK) continue;
#else
            if (errno == EINTR) continue;
#endif
            throw pybridge_error("python worker exited unexpectedly");
        }
        if (n == 0)
            throw pybridge_error("python worker exited unexpectedly");
        sent += (size_t)n;
        if (py_interrupt_pending()) abortForInterrupt();
    }
}

// Suppress SIGPIPE per socket where the platform wants it there (macOS,
// FreeBSD) rather than as a send flag, and keep the descriptor out of any
// child we spawn later.
void tameSocket(socket_t s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&on, sizeof(on));
#endif
#ifndef _WIN32
    // The worker inherits every descriptor without this, including the
    // listening socket -- which would keep the port alive for as long as the
    // worker lives.
    int fl = fcntl(s, F_GETFD, 0);
    if (fl != -1) fcntl(s, F_SETFD, fl | FD_CLOEXEC);
#endif
}

// Read until one complete frame is buffered. `deadlineMs` <= 0 means wait
// indefinitely (an RPC may legitimately take minutes to render).
std::string readFrame(socket_t s, int deadlineMs, std::string &buf) {
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        size_t n = frameLength(buf);
        if (n) {
            std::string frame = buf.substr(0, n);
            buf.erase(0, n);
            return frame;
        }
        if (waitReadable(s, kPumpIntervalMs)) {
            char tmp[65536];
            int got = (int)::recv(s, tmp, sizeof(tmp), 0);
            if (got == 0)
                throw pybridge_error("python worker exited unexpectedly");
            if (got < 0) {
#ifdef _WIN32
                int e = WSAGetLastError();
                if (e == WSAEWOULDBLOCK || e == WSAEINTR) continue;
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    continue;
#endif
                throw pybridge_error("python worker exited unexpectedly");
            }
            buf.append(tmp, (size_t)got);
            continue;
        }
        pump();
        if (py_interrupt_pending()) abortForInterrupt();
        if (deadlineMs > 0) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (ms > deadlineMs)
                throw pybridge_error("python worker did not answer in time");
        }
    }
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

std::vector<std::string> candidates() {
    std::vector<std::string> c;
    const Host &h = host();
    if (!h.forced.empty()) { c.push_back(h.forced); return c; }
    if (const char *env = std::getenv("FEMMQT_PYTHON")) {
        if (*env) c.push_back(env);
    }
#ifdef _WIN32
    c.push_back("python.exe");
    c.push_back("python3.exe");
#else
    c.push_back("python3");
    c.push_back("python");
#ifdef __APPLE__
    c.push_back("/opt/homebrew/bin/python3");
    c.push_back("/usr/local/bin/python3");
#else
    c.push_back("/usr/local/bin/python3");  // FreeBSD pkg lands here
#endif
#endif
    return c;
}

std::string workerScript() {
    // In its own directory beside the binaries -- deliberately NOT the python/
    // directory that holds the MicroPython client and the matplotlib stubs.
    // CPython puts a script's own directory first on sys.path, so a worker
    // living next to python/matplotlib/ would import that forwarding stub as
    // matplotlib, fail on `import _pybridge`, and report itself as having no
    // matplotlib.
    //
    // The hyphen in the name is load-bearing: bin/ is the GUI's working
    // directory, so it is on MicroPython's sys.path ahead of python/, and a
    // directory called "pybridge" there would be imported as an empty
    // namespace package instead of python/pybridge.py. A hyphen cannot be an
    // identifier. The repo copy is the fallback, so a developer build works
    // before any install step has run.
    std::string shipped = exeDir() + "pybridge-worker/femmqt_pybridge_worker.py";
    std::error_code ec;
    if (fs::exists(femm_path(shipped), ec)) return shipped;
    std::string dev = exeDir() + "../../pybridge/worker/femmqt_pybridge_worker.py";
    if (fs::exists(femm_path(dev), ec)) return femm_u8string(
        fs::weakly_canonical(femm_path(dev), ec));
    return shipped;  // report the shipped path in the error message
}

std::string makeToken() {
    static const char *hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
    std::string t;
    for (int i = 0; i < 32; i++) t += hex[gen() & 0xF];
    return t;
}

void setChildEnv(const std::string &token, const std::string &backend) {
    // Set in the parent and let the child inherit: building an environment
    // block by hand is the only alternative, and this code path is single
    // threaded (scripts run on one thread by design).
#ifdef _WIN32
    SetEnvironmentVariableW(L"FEMMQT_PYBRIDGE_TOKEN", femm_widen(token).c_str());
    if (backend.empty())
        SetEnvironmentVariableW(L"FEMMQT_PYBRIDGE_BACKEND", nullptr);
    else
        SetEnvironmentVariableW(L"FEMMQT_PYBRIDGE_BACKEND",
                                femm_widen(backend).c_str());
#else
    setenv("FEMMQT_PYBRIDGE_TOKEN", token.c_str(), 1);
    if (backend.empty()) unsetenv("FEMMQT_PYBRIDGE_BACKEND");
    else setenv("FEMMQT_PYBRIDGE_BACKEND", backend.c_str(), 1);
#endif
}

void clearChildEnv() {
#ifdef _WIN32
    SetEnvironmentVariableW(L"FEMMQT_PYBRIDGE_TOKEN", nullptr);
#else
    unsetenv("FEMMQT_PYBRIDGE_TOKEN");
#endif
}

// Start `python worker --connect 127.0.0.1:port`. Returns false if the
// interpreter could not be launched at all (wrong name, not on PATH).
bool spawnWorker(const std::string &python, const std::string &script,
                 int port) {
    Host &h = host();
    char portArg[64];
    std::snprintf(portArg, sizeof(portArg), "127.0.0.1:%d", port);
#ifdef _WIN32
    std::string cmd = "\"" + python + "\" \"" + script + "\" --connect "
                    + portArg;
    std::wstring wcmd = femm_widen(cmd);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    // CREATE_NO_WINDOW, not DETACHED_PROCESS: python.exe is a console
    // subsystem binary, so a GUI parent would otherwise flash a console. This
    // form still inherits a console when one exists, which is what keeps the
    // worker's stderr visible under femm_headless in a terminal.
    //
    // Note this deliberately differs from the solver spawn in
    // script_callbacks.cpp, which uses DETACHED_PROCESS *because* inheriting
    // the console made every per-iteration progress line from a chatty solver
    // pay for a conhost write. The worker says nothing unless something is
    // wrong, so it never pays that cost -- and when something IS wrong, its
    // stderr is the only diagnostic there is. Do not "align" the two flags
    // without re-reading both reasons.
    // bInheritHandles FALSE: sockets are inheritable by default on Windows, so
    // TRUE would hand the worker our listening socket and keep the port alive
    // for the worker's lifetime. The console is inherited independently of
    // this flag, so the worker's stderr still reaches a terminal.
    BOOL ok = CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    h.proc = pi.hProcess;
    return true;
#else
    // posix_spawnp rather than system() -- which is what the solver spawn in
    // script_callbacks.cpp uses -- because that one runs a child to completion
    // and wants only its exit code, while this one must be kept, polled for
    // liveness and killed. It is also the right primitive in a Qt process:
    // fork() in a multithreaded program is safe only if you exec immediately,
    // and posix_spawn is exactly that operation done portably.
    const char *argv[] = {python.c_str(), script.c_str(), "--connect",
                          portArg, nullptr};
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, python.c_str(), nullptr, nullptr,
                          (char *const *)argv, femm_environ);
    if (rc != 0) return false;
    h.pid = pid;
    return true;
#endif
}

bool workerAlive() {
#ifdef _WIN32
    Host &h = host();
    if (!h.proc) return false;
    DWORD code = 0;
    return GetExitCodeProcess(h.proc, &code) && code == STILL_ACTIVE;
#else
    Host &h = host();
    if (h.pid <= 0) return false;
    int status = 0;
    pid_t r = ::waitpid(h.pid, &status, WNOHANG);
    if (r == h.pid) { h.pid = -1; return false; }
    return true;
#endif
}

// Try one interpreter end to end: spawn, accept, greet, hello. On success the
// host is started. On failure everything is cleaned up and `why` explains it.
// `requireBackend` false accepts an interpreter that cannot plot. Discovery
// runs a second pass that way when nothing can: the bridge is a general
// package proxy -- xlsxwriter, csv, scipy, sympy all work perfectly well
// without a plotting backend -- so refusing a working Python because it has no
// matplotlib would deny every non-plotting use. `greetedNoBackend` reports
// which candidates fell into exactly that case.
bool tryCandidate(const std::string &python, const std::string &script,
                  socket_t listener, int port, std::string *why,
                  bool requireBackend = true,
                  bool *greetedNoBackend = nullptr) {
    Host &h = host();
    h.token = makeToken();
    setChildEnv(h.token, h.backend);
    bool spawned = spawnWorker(python, script, port);
    clearChildEnv();
    if (!spawned) {
        *why = "could not be launched";
        return false;
    }

    // Accept, but do not wait forever on a python that died immediately.
    // Everything from here on is inside the try below, so a socket failure
    // rejects this candidate rather than escaping and leaking the listener.
    auto start = std::chrono::steady_clock::now();
    socket_t client = INVALID_SOCK;
    try {
        for (;;) {
            if (waitReadable(listener, kPumpIntervalMs)) {
                sockaddr_in addr{};
#ifdef _WIN32
                int alen = sizeof(addr);
#else
                socklen_t alen = sizeof(addr);
#endif
                client = ::accept(listener, (sockaddr *)&addr, &alen);
                if (client != INVALID_SOCK) break;
            }
            pump();
            if (py_interrupt_pending()) abortForInterrupt();
            if (!workerAlive()) {
                *why = "started but exited immediately (is the worker script "
                       "readable, and is this a Python 3.9+?)";
                killWorker();
                return false;
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (ms > kHelloTimeoutMs) {
                *why = "did not connect back within 15 s";
                killWorker();
                return false;
            }
        }
        h.sock = client;
        tameSocket(h.sock);
        int one = 1;
        setsockopt(h.sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
                   sizeof(one));

        std::string buf;
        // The worker proves itself first: anything else on this port is not
        // ours, and must not be allowed to answer a single RPC.
        std::string greeting = frameJson(readFrame(h.sock, kHelloTimeoutMs, buf));
        std::string tok;
        if (!jsonField(greeting, "token", &tok) || tok != h.token) {
            *why = "failed authentication";
            killWorker();
            return false;
        }
        std::string hello = "{\"id\":1,\"op\":\"hello\",\"token\":\"" + h.token
                          + "\",\"protocol\":1,\"client\":\"femm-qt\"}";
        sendAll(h.sock, buildJsonFrame(hello));
        std::string reply = frameJson(readFrame(h.sock, kHelloTimeoutMs, buf));
        std::string ok;
        if (!jsonField(reply, "ok", &ok) || ok != "true") {
            std::string msg;
            jsonField(reply, "msg", &msg);
            *why = "refused the handshake" + (msg.empty() ? "" : ": " + msg);
            killWorker();
            return false;
        }
        std::string backend;
        bool haveBackend = jsonField(reply, "backend", &backend)
                        && backend != "null";
        if (!haveBackend && requireBackend) {
            // It answers, but it cannot draw. Prefer a candidate that can;
            // ensure_started() comes back for this one if none does.
            std::string err;
            jsonField(reply, "backend_error", &err);
            *why = err.empty() ? "has no interactive matplotlib backend" : err;
            if (greetedNoBackend) *greetedNoBackend = true;
            killWorker();
            return false;
        }
        h.info = reply;
        h.interpreter = python;
        h.started = true;
        return true;
    } catch (const bridge_interrupted &) {
        throw;  // the user gave up; do not go on to try another interpreter
    } catch (const pybridge_error &e) {
        *why = e.what();
        killWorker();
        return false;
    }
}

std::string remedy() {
    // Two audiences, one message. This fires when NO interpreter answered at
    // all, so the primary want is a Python 3 -- but the commonest reason to
    // want one here is plotting, so the plotting advice comes with it. Naming
    // only matplotlib was wrong once xlsxwriter could be imported the same
    // way: it told someone who wanted a spreadsheet to install a plotting
    // library.
    //
    // Keep this honest about what is actually required: matplotlib needs *a*
    // window toolkit, and the python.org installers already ship Tk. Telling
    // everyone to install PyQt6 sent people after a package they mostly do not
    // need -- Qt is a preference, not a requirement.
#ifdef __FreeBSD__
    return "Install Python 3, and for plots use pkg rather than pip -- there "
           "are no prebuilt wheels for FreeBSD: pkg install py311-matplotlib "
           "py311-tkinter";
#elif defined(__linux__)
    return "Install Python 3 (python3 from your distribution). For plots it "
           "also needs matplotlib and a window toolkit: pip install matplotlib "
           "plus your distribution's python3-tk (or python3-matplotlib and "
           "python3-tk together)";
#elif defined(__APPLE__)
    return "Install Python 3. For plots, add matplotlib: pip install "
           "matplotlib. If that reports no backend, the interpreter has no Tk "
           "-- Homebrew's Python needs brew install python-tk as well";
#else
    return "Install Python 3 from python.org. For plots, add matplotlib: pip "
           "install matplotlib -- the python.org installer already provides the "
           "Tk toolkit it needs; PyQt6 is optional and only changes the window "
           "style";
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void femm_pybridge_ensure_started() {
    Host &h = host();
    if (h.started) {
        if (workerAlive()) return;
        killWorker();  // died between calls; fall through and respawn
    }
    if (!socketsReady())
        throw pybridge_error("could not initialise networking for the "
                             "python bridge");

    const std::string script = workerScript();
    std::error_code ec;
    if (!fs::exists(femm_path(script), ec))
        throw pybridge_error("the python bridge worker script is missing: "
                             + script);

    socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCK)
        throw pybridge_error("could not open a socket for the python bridge");
    // Closed on every path out, including the interrupt abort, which throws
    // straight through the candidate loop.
    struct Closer {
        socket_t s;
        ~Closer() { if (s != INVALID_SOCK) closesocket(s); }
    } closer{listener};
    tameSocket(listener);
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // localhost only, ever
    addr.sin_port = 0;                              // ephemeral
    if (::bind(listener, (sockaddr *)&addr, sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0)
        throw pybridge_error("could not listen for the python bridge");
    sockaddr_in bound{};
#ifdef _WIN32
    int blen = sizeof(bound);
#else
    socklen_t blen = sizeof(bound);
#endif
    getsockname(listener, (sockaddr *)&bound, &blen);
    const int port = ntohs(bound.sin_port);

    h.attempts.clear();
    std::vector<std::string> plotless;
    for (const std::string &cand : candidates()) {
        std::string why;
        bool noBackend = false;
        if (tryCandidate(cand, script, listener, port, &why, true, &noBackend))
            return;
        if (noBackend) plotless.push_back(cand);
        h.attempts.push_back(cand + " (" + why + ")");
    }

    // Nothing here can plot. An interpreter that answered is still worth
    // having: csv, xlsxwriter, scipy and the rest do not need a figure window.
    // Take the first one that did, and let the plotting error come from
    // matplotlib itself if a script goes on to ask for a figure.
    for (const std::string &cand : plotless) {
        std::string why;
        if (tryCandidate(cand, script, listener, port, &why, false)) return;
    }

    // Deliberately not "the matplotlib bridge": the same discovery backs
    // xlsxwriter and anything else pybridge.import_module() reaches.
    std::string msg = "no usable Python 3 found for the bridge (needed by "
                      "matplotlib, xlsxwriter, and anything imported with "
                      "pybridge.import_module).\nTried: ";
    for (size_t i = 0; i < h.attempts.size(); i++) {
        msg += h.attempts[i];
        if (i + 1 < h.attempts.size()) msg += "\n       ";
    }
    msg += "\n" + remedy()
        + "\nOr point femm-qt at a specific interpreter with "
          "pybridge.set_interpreter(path) or the FEMMQT_PYTHON environment "
          "variable.";
    throw pybridge_error(msg);
}

std::string femm_pybridge_rpc(const std::string &frame) {
    Host &h = host();
    // The one genuinely new hazard (plan §4.1): event pumping can re-enter
    // script execution, and a second RPC on a strictly request/response stream
    // would desynchronise it. Nothing else here needs guarding -- general GUI
    // re-entrancy predates the bridge, and py_is_running() would reject every
    // legitimate call, since it is true for the whole script.
    if (h.inFlight)
        throw pybridge_error("a bridge call is already in progress; the "
                             "bridge cannot be used re-entrantly");
    femm_pybridge_ensure_started();

    h.inFlight = true;
    struct Clear {
        Host &h;
        ~Clear() { h.inFlight = false; }
    } clear{h};

    try {
        sendAll(h.sock, frame);
        return readFrame(h.sock, 0, h.rx);
    } catch (const pybridge_error &) {
        // killWorker() clears the read buffer: leftover bytes from a dead
        // worker must never be read as the next worker's reply.
        if (h.started) killWorker();
        h.rx.clear();
        throw;
    }
}

long femm_pybridge_session()   { return host().session; }
bool femm_pybridge_running()   { return host().started; }
const std::string &femm_pybridge_info() { return host().info; }

void femm_pybridge_shutdown() {
    Host &h = host();
    if (!h.started) return;
    try {
        // Ask nicely first so the worker closes its figures and exits 0.
        sendAll(h.sock, buildJsonFrame("{\"id\":0,\"op\":\"shutdown\"}"));
        std::string buf;
        readFrame(h.sock, 2000, buf);
    } catch (const pybridge_error &) {
        // Already gone, or wedged; killWorker() settles it either way.
    }
    killWorker();
}

void femm_pybridge_restart() {
    if (host().started) killWorker();
    else host().session++;
}

void femm_pybridge_set_interpreter(const std::string &path) {
    Host &h = host();
    if (h.forced == path) return;
    h.forced = path;
    if (h.started) femm_pybridge_shutdown();
}

std::string femm_pybridge_interpreter() {
    const Host &h = host();
    return h.started ? h.interpreter : h.forced;
}

void femm_pybridge_set_backend(const std::string &backend) {
    host().backend = backend;
}

std::string femm_pybridge_backend() { return host().backend; }

const std::vector<std::string> &femm_pybridge_attempts() {
    return host().attempts;
}
