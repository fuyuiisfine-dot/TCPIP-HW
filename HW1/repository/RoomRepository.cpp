#include "Repositories.h"
namespace chat {
bool RoomRepository::add(const std::string &name) {
    return rooms_.size() < 100 && rooms_.emplace(name, Room{name}).second;
}
bool RoomRepository::contains(const std::string &name) const {
    return rooms_.count(name) != 0;
}
std::string RoomRepository::list() const {
    std::string result;
    for (const auto &item : rooms_)
        result += item.first + "\n";
    return result;
}
} // namespace chat
