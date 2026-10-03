#include "protocol/Packet.h"
#include <stdexcept>
namespace chat {
static void appendUint32(std::string &output, uint32_t value) {
    value = htonl(value);
    output.append(reinterpret_cast<const char *>(&value), 4);
}
static void appendUint16(std::string &output, uint16_t value) {
    value = htons(value);
    output.append(reinterpret_cast<const char *>(&value), 2);
}
std::string encodePacket(const Packet &packet) {
    if (packet.fields.size() > 16)
        throw std::runtime_error("Too many fields");
    std::string body;
    for (const auto &field : packet.fields) {
        if (field.size() > MaxPayload || body.size() + 4 + field.size() > MaxPayload)
            throw std::runtime_error("Packet too large");
        appendUint32(body, static_cast<uint32_t>(field.size()));
        body += field;
    }
    std::string output;
    appendUint32(output, Magic);
    appendUint16(output, Version);
    appendUint16(output, static_cast<uint16_t>(packet.type));
    appendUint32(output, static_cast<uint32_t>(body.size()));
    return output + body;
}
bool sendPacket(SOCKET socket, const Packet &packet) {
    const auto bytes = encodePacket(packet);
    size_t offset = 0;
    while (offset < bytes.size()) {
        int value = send(socket, bytes.data() + offset, static_cast<int>(bytes.size() - offset), 0);
        if (value <= 0)
            return false;
        offset += value;
    }
    return true;
}
} // namespace chat
