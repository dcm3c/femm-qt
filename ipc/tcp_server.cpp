// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// tcp_server.cpp — Simple TCP server for FEMM IPC
//
// Single-client, newline-delimited text protocol.
// Executes Python commands via exec_python() and returns results.

#include "tcp_server.h"
#include "py_state.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/tcp.h>
#include <sys/select.h>
#include <fcntl.h>
#include <cerrno>
#endif

// Return values are captured by exec_python() into py_last_result();
// the server no longer needs its own capture global or a wrapper function.

// --- TcpServer implementation ---

TcpServer::TcpServer()
    : m_listenSock(INVALID_SOCK)
    , m_clientSock(INVALID_SOCK)
    , m_port(0)
    , m_listening(false)
    , m_running(false)
    , m_bufLen(0)
{
    initSockets();
}

TcpServer::~TcpServer()
{
    stop();
    cleanupSockets();
}

void TcpServer::initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void TcpServer::cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TcpServer::listen(int port)
{
    m_listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenSock == INVALID_SOCK) {
        fprintf(stderr, "IPC: failed to create socket\n");
        return false;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // localhost only
    addr.sin_port = htons(port);

    if (bind(m_listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "IPC: failed to bind port %d\n", port);
#ifdef _WIN32
        closesocket(m_listenSock);
#else
        close(m_listenSock);
#endif
        m_listenSock = INVALID_SOCK;
        return false;
    }

    if (::listen(m_listenSock, 1) < 0) {
        fprintf(stderr, "IPC: failed to listen\n");
#ifdef _WIN32
        closesocket(m_listenSock);
#else
        close(m_listenSock);
#endif
        m_listenSock = INVALID_SOCK;
        return false;
    }

    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_listenSock, FIONBIO, &mode);
#else
    int flags = fcntl(m_listenSock, F_GETFL, 0);
    fcntl(m_listenSock, F_SETFL, flags | O_NONBLOCK);
#endif

    m_port = port;
    m_listening = true;

    fprintf(stderr, "IPC: listening on localhost:%d\n", port);
    return true;
}

void TcpServer::stop()
{
    m_running = false;
    m_listening = false;
    closeClient();
    if (m_listenSock != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(m_listenSock);
#else
        close(m_listenSock);
#endif
        m_listenSock = INVALID_SOCK;
    }
}

void TcpServer::acceptClient()
{
    if (m_listenSock == INVALID_SOCK) return;

    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    socket_t cs = accept(m_listenSock, (struct sockaddr*)&caddr, &clen);
    if (cs == INVALID_SOCK) return;  // no pending connection

    // If we already have a primary client, reject the new one cleanly:
    // send an error message and close. Without this, the second client
    // would hang on a connected-but-never-serviced socket because
    // femm-qt's IPC server is single-client. Also prevents the
    // listen-socket QSocketNotifier from firing repeatedly in a wakeup
    // loop (the pending connection stays in the kernel backlog if we
    // never accept it).
    if (m_clientSock != INVALID_SOCK) {
        const char *msg = "error:femm-qt is already connected to another client\n";
        send(cs, msg, (int)strlen(msg), 0);
#ifdef _WIN32
        closesocket(cs);
#else
        close(cs);
#endif
        fprintf(stderr, "IPC: rejected second client (single-client server)\n");
        return;
    }

    // Set client non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(cs, FIONBIO, &mode);
#else
    int flags = fcntl(cs, F_GETFL, 0);
    fcntl(cs, F_SETFL, flags | O_NONBLOCK);
#endif

    // Disable Nagle's algorithm. Without this, the kernel may delay small
    // outbound packets to coalesce with subsequent writes, which interacts
    // very badly with the small request/response RPC pattern of all the
    // language bindings (mathfemm, octavefemm, pyfemm, juliafemm, scifemm)
    // and adds 5-200ms of latency per round-trip depending on the OS's
    // delayed-ACK timer. Setting TCP_NODELAY on the server side eliminates
    // the response-direction delay; the client bindings are responsible
    // for setting it on their end too.
    int nodelay = 1;
    setsockopt(cs, IPPROTO_TCP, TCP_NODELAY,
               (const char*)&nodelay, sizeof(nodelay));

    m_clientSock = cs;
    m_bufLen = 0;
    fprintf(stderr, "IPC: client connected\n");
}

void TcpServer::closeClient()
{
    if (m_clientSock != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(m_clientSock);
#else
        close(m_clientSock);
#endif
        m_clientSock = INVALID_SOCK;
        m_bufLen = 0;
        fprintf(stderr, "IPC: client disconnected\n");
    }
}

void TcpServer::handleClient()
{
    if (m_clientSock == INVALID_SOCK) return;

    // Read available data
    int space = (int)sizeof(m_buf) - m_bufLen - 1;
    if (space <= 0) { m_bufLen = 0; return; }  // overflow, reset

    int n = recv(m_clientSock, m_buf + m_bufLen, space, 0);
    if (n <= 0) {
#ifdef _WIN32
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return;
#else
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
#endif
        // The client has gone away — either a clean EOF (n == 0) or an
        // error other than "would block" (abrupt disconnect, kernel
        // crash, user killed the binding, etc.). Clean up the socket and
        // exit the femm-qt process so we don't leave an orphaned instance
        // behind. This mirrors the original FEMM ActiveX behavior, where
        // the COM server shut down when its client died, and matches the
        // existing CloseFEMM[] / closefemm() / quit() path which already
        // calls exit(0) via lua_quit (libfemm/lua_state.cpp:73-78).
        //
        // Tradeoff: scripts that want to inspect the GUI after their work
        // is done must keep the binding's connection alive (i.e., don't
        // call closefemm at the end). This is the same model as OF and is
        // a clean "femm-qt lives iff a client is connected" lifecycle.
        closeClient();
        fprintf(stderr, "IPC: client disconnected, femm-qt exiting\n");
        exit(0);
    }
    m_bufLen += n;
    m_buf[m_bufLen] = 0;

    // Process complete lines (newline-delimited commands)
    char *start = m_buf;
    while (true) {
        char *nl = strchr(start, '\n');
        if (!nl) break;
        *nl = 0;

        // Strip trailing \r if present
        if (nl > start && *(nl-1) == '\r') *(nl-1) = 0;

        // Execute the command. exec_python tries it as an expression first
        // (capturing the return value into py_last_result(), tab-joined for
        // multiple/tuple results) and falls back to statement execution
        // (leaving the result empty) — so no __ipc_capture wrapper is needed.
        char errmsg[1024] = {};
        int status = exec_python(start, errmsg, sizeof(errmsg));

        // Send response
        std::string response;
        if (status != 0) {
            response = "error:";
            response += errmsg;
        } else {
            response = py_last_result();
        }
        response += '\n';

        const char *p   = response.c_str();
        size_t      left = response.size();
        while (left > 0) {
            int n = send(m_clientSock, p, (int)left, 0);
            if (n > 0) { p += n; left -= n; continue; }
#ifdef _WIN32
            int err = (n < 0) ? WSAGetLastError() : 0;
            bool again = (err == WSAEWOULDBLOCK);
#else
            bool again = (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
#endif
            if (!again) {
                fprintf(stderr, "IPC: send() failed mid-response, client gone\n");
                closeClient();
                return;
            }
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(m_clientSock, &wfds);
            select((int)m_clientSock + 1, nullptr, &wfds, nullptr, nullptr);
        }

        // Post-command repaint is handled by the GUI's py_state post-exec hook
        // (set_py_post_exec_hook), which fires once per top-level exec_python —
        // here, once per command line — and applies the same getter/action
        // gate (empty wire result => action => repaint). Keeping it off the
        // transport means console/dofile/Run-Script paths refresh too.

        start = nl + 1;
    }

    // Shift remaining data to front of buffer
    int remain = m_bufLen - (int)(start - m_buf);
    if (remain > 0 && start != m_buf)
        memmove(m_buf, start, remain);
    m_bufLen = remain;
}

void TcpServer::processEvents()
{
    acceptClient();
    handleClient();
}

void TcpServer::run()
{
    m_running = true;
    fprintf(stderr, "IPC: headless mode — waiting for commands on port %d\n", m_port);

    // Event-driven main loop using select() — blocks the kernel thread
    // until any watched socket is ready, with no polling and no Sleep
    // granularity issues. The previous Sleep(1) + processEvents loop was
    // limited by Windows's 15.6ms default timer tick (Sleep(1) actually
    // sleeps for ~15ms), giving ~15ms per-call latency in headless mode
    // even after the GUI got QSocketNotifier. select() drops that to
    // ~50us (kernel scheduling + the recv/accept call itself).
    //
    // The watch set is rebuilt every iteration because the client socket
    // can become valid (after acceptClient) or invalid (after closeClient,
    // which is followed by exit(0) anyway in handleClient's recv-failure
    // path, so this is mostly defensive). The 1-second timeout exists
    // only so the loop can periodically re-check m_running for clean
    // shutdown — under normal load, select() returns much sooner because
    // a socket is ready.
    while (m_running && m_listening) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int maxfd = 0;
        if (m_listenSock != INVALID_SOCK) {
            FD_SET(m_listenSock, &readfds);
            if ((int)m_listenSock > maxfd) maxfd = (int)m_listenSock;
        }
        if (m_clientSock != INVALID_SOCK) {
            FD_SET(m_clientSock, &readfds);
            if ((int)m_clientSock > maxfd) maxfd = (int)m_clientSock;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int nready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);

        if (nready > 0) processEvents();
        // nready == 0: timeout, loop and re-check m_running
        // nready  < 0: interrupted/error, loop (will retry select next iteration)
    }
}
