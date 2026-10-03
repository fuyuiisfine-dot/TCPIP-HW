#pragma once
#include <cstdint>
namespace chat {
constexpr uint32_t Magic=0x43484154;
constexpr uint16_t Version=1;
constexpr uint32_t MaxPayload=1024*1024+4096;
constexpr unsigned HeaderSize=12;
// Network byte order: magic:u32, version:u16, type:u16, payload size:u32.
}
