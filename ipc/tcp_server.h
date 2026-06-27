// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// tcp_server.h — Simple TCP server for FEMM IPC
// Accepts Lua command strings, executes them, returns results.
// Protocol: newline-delimited text.
//   Client sends: Lua command + '\n'
//   Server sends: result + '\n' (empty line if no result, "error:..." on failure)
//
// No Qt dependency — uses POSIX sockets / Winsock directly.
#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#endif

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    // Start listening on the given port. Returns true on success.
    bool listen(int port = 48015);

    // Stop listening and close all connections.
    void stop();

    // Process pending connections and commands (non-blocking).
    // Call this from an event loop (GUI timer or headless main loop).
    void processEvents();

    // Blocking event loop for headless mode.
    // Runs until the server is stopped (e.g. via "quit" Lua command).
    void run();

    int port() const { return m_port; }
    bool isListening() const { return m_listening; }

    // Raw socket accessors. Exposed so the GUI can attach a QSocketNotifier
    // to the listen and client sockets and drive processEvents() from
    // socket-readable events instead of polling on a QTimer. Both sockets
    // are non-blocking. Returns INVALID_SOCK if the corresponding socket is
    // not currently open.
    socket_t listenSocket() const { return m_listenSock; }
    socket_t clientSocket() const { return m_clientSock; }

    // NB: post-command repaint is no longer a transport concern. The GUI
    // installs a py_state post-exec hook (set_py_post_exec_hook) that fires for
    // every script path — TCP line, console, dofile, Run Script File — so all
    // of them refresh the editor consistently. See libfemm/py_state.h.

private:
    socket_t m_listenSock;
    socket_t m_clientSock;  // single client for now
    int m_port;
    bool m_listening;
    bool m_running;
    char m_buf[65536];      // receive buffer
    int m_bufLen;

    void acceptClient();
    void handleClient();
    void closeClient();
    void initSockets();
    void cleanupSockets();
};

#endif // TCP_SERVER_H
