#pragma once
#include "protocol/Packet.h"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace chat {
constexpr UINT NetworkEvent = WM_APP + 1;
struct GuiEvent {
    enum Kind { Connected, Incoming, Failure, Disconnected } kind;
    Packet packet{PacketType::Ok, {}};
    std::string text;
};
class GuiConnection {
  public:
    explicit GuiConnection(HWND window, std::filesystem::path downloads);
    ~GuiConnection();
    void start(std::string address, unsigned short port, std::string nickname);
    void stop();
    bool send(Packet packet);
    std::deque<GuiEvent> drain();

  private:
    HWND window_;
    std::filesystem::path downloads_;
    std::atomic<SOCKET> socket_{INVALID_SOCKET};
    std::atomic<bool> stopping_{true};
    std::thread receiver_, sender_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool connected_ = false;
    std::deque<Packet> outgoing_;
    std::deque<GuiEvent> events_;
    size_t outgoingBytes_ = 0;
    void cancel();
    void emit(GuiEvent event);
    void receiveLoop(const std::string &, unsigned short, const std::string &);
    void sendLoop();
    std::string saveFile(const std::string &originalName, const std::string &content);
};
} // namespace chat
