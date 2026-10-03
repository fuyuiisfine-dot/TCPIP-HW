#include "ChatServices.h"
#include <chrono>
namespace chat {
void ChatServices::message(const Session &session, const Packet &packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requireLogin(session))
        return;
    if (session->room.empty())
        return error(session, "Join a room first");
    if (packet.type == PacketType::History) {
        if (!packet.fields.empty())
            return error(session, "History takes no fields");
        for (const auto &message : messages_.history(session->room))
            session->send({PacketType::Message,
                           {message.room, message.sender, message.text, message.timestamp}});
        return ok(session, "End of history");
    }
    if (packet.fields.size() != 1 || packet.fields[0].empty() || packet.fields[0].size() > 4096)
        return error(session, "Message must contain 1-4096 bytes");
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    Message message{session->room, session->user.name, packet.fields[0], std::to_string(timestamp)};
    messages_.add(message);
    broadcast(session->room, {PacketType::Message,
                              {message.room, message.sender, message.text, message.timestamp}});
}
} // namespace chat
