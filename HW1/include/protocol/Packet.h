#pragma once
#include <winsock2.h>
#include <string>
#include <vector>
#include "PacketType.h"
#include "PacketHeader.h"
namespace chat {
struct Packet { PacketType type; std::vector<std::string> fields; };
bool receivePacket(SOCKET socket, Packet& packet);
bool sendPacket(SOCKET socket, const Packet& packet);
std::string encodePacket(const Packet& packet);
}
