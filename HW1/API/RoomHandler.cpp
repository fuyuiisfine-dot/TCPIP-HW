#include "ChatServices.h"
namespace chat {
void handleRoom(ChatServices &services, const Session &session, const Packet &packet) {
    services.room(session, packet);
}
} // namespace chat
