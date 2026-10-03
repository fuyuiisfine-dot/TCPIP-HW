#pragma once
#include <string>
namespace chat {
struct Message {
    std::string room, sender, text, timestamp;
};
} // namespace chat
