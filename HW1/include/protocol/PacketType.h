#pragma once
#include <cstdint>
namespace chat {
enum class PacketType : uint16_t { Login=1, ListRooms=2, CreateRoom=3, JoinRoom=4, LeaveRoom=5, SendMessage=6, History=7, SendFile=8, Ok=100, Error=101, Rooms=102, Message=103, File=104 };
}
