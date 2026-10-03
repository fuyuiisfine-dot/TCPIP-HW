#include "ChatServices.h"
namespace chat {
void handleMessage(ChatServices& services,const Session& session,const Packet& packet){services.message(session,packet);}
}
