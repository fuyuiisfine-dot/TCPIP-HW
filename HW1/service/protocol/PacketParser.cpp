#include "protocol/Packet.h"
#include <cstring>
namespace chat {
static bool receiveExact(SOCKET socket, char *buffer, size_t size) {
    while (size) {
        int value = recv(socket, buffer, static_cast<int>(size), 0);
        if (value <= 0)
            return false;
        buffer += value;
        size -= value;
    }
    return true;
}
static uint32_t readUint32(const char *buffer) {
    uint32_t value;
    std::memcpy(&value, buffer, 4);
    return ntohl(value);
}
static uint16_t readUint16(const char *buffer) {
    uint16_t value;
    std::memcpy(&value, buffer, 2);
    return ntohs(value);
}
bool receivePacket(SOCKET socket, Packet &packet) {
    char header[HeaderSize];
    if (!receiveExact(socket, header, sizeof header))
        return false;
    auto size = readUint32(header + 8);
    if (readUint32(header) != Magic || readUint16(header + 4) != Version || size > MaxPayload)
        return false;
    std::string body(size, '\0');
    if (!receiveExact(socket, body.data(), size))
        return false;
    packet.type = static_cast<PacketType>(readUint16(header + 6));
    packet.fields.clear();
    size_t offset = 0;
    while (offset < body.size()) {
        if (body.size() - offset < 4 || packet.fields.size() >= 16)
            return false;
        auto fieldSize = readUint32(body.data() + offset);
        offset += 4;
        if (fieldSize > body.size() - offset)
            return false;
        packet.fields.push_back(body.substr(offset, fieldSize));
        offset += fieldSize;
    }
    return true;
}
} // namespace chat
