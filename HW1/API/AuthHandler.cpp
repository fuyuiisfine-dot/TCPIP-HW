#include "ChatServices.h"
namespace chat {
void handleAuth(ChatServices &services, const Session &session, const Packet &packet) {
    services.login(session, packet);
}
} // namespace chat
