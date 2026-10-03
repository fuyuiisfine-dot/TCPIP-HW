#include "Repositories.h"
namespace chat {
bool UserRepository::add(const std::string &name) {
    return users_.insert(name).second;
}
void UserRepository::remove(const std::string &name) {
    users_.erase(name);
}
} // namespace chat
