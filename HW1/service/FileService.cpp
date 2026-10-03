#include "ChatServices.h"
namespace chat {
void ChatServices::file(const Session &s, const Packet &p) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requireLogin(s))
        return;
    if (s->room.empty())
        return error(s, "Join a room first");
    if (p.fields.size() != 2 || p.fields[0].empty() || p.fields[0].size() > 128 ||
        p.fields[1].size() > 1024 * 1024)
        return error(s, "File limit: 1 MiB; name limit: 128 bytes");
    for (unsigned char c : p.fields[0])
        if (c < 32 || c == 127 || std::string("/\\:*?\"<>|").find(c) != std::string::npos)
            return error(s, "Unsafe filename");
    if (p.fields[0] == "." || p.fields[0] == "..")
        return error(s, "Unsafe filename");
    broadcast(s->room, {PacketType::File, {s->room, s->user.name, p.fields[0], p.fields[1]}});
}
} // namespace chat
