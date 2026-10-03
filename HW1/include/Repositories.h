#pragma once
#include "domain/Room.h"
#include "domain/Message.h"
#include <map>
#include <set>
#include <deque>
namespace chat {
// All repository access is serialized by ChatServices::mutex_.
class UserRepository {
    std::set<std::string> users_;

  public:
    bool add(const std::string &);
    void remove(const std::string &);
};
class RoomRepository {
    std::map<std::string, Room> rooms_{{"lobby", {"lobby"}}};

  public:
    bool add(const std::string &);
    bool contains(const std::string &) const;
    std::string list() const;
};
class MessageRepository {
    std::map<std::string, std::deque<Message>> messages_;

  public:
    void add(const Message &);
    std::deque<Message> history(const std::string &) const;
};
} // namespace chat
