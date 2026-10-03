#pragma once
#include "protocol/Packet.h"
#include "User.h"
#include <mutex>
namespace chat {
struct ClientSession {
    explicit ClientSession(SOCKET s) : socket(s) {}
    // Immutable handle: only the owning receiver reads it without sendMutex.
    const SOCKET socket;
    User user;
    std::string room;
    std::mutex sendMutex;
    bool closed = false; // guarded by sendMutex

    bool send(const Packet& packet) {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (closed) return false;
        if (sendPacket(socket, packet)) return true;
        shutdown(socket, SD_BOTH);
        return false;
    }
    // Used only during server shutdown, after the listener has been closed.
    // closesocket cancels a pending blocking recv, including a partial frame.
    void interrupt() { close(); }
    void close() {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (!closed) {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
            closed = true;
        }
    }
};
}
