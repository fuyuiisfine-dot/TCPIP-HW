#include "ChatServices.h"
namespace chat {
void handleFile(ChatServices& services,const Session& session,const Packet& packet){services.file(session,packet);}
}
