#include "ChatServices.h"
namespace chat {
void ChatServices::login(const Session& s,const Packet& p){
 std::lock_guard<std::mutex> lock(mutex_);
 if(p.fields.size()!=1||!validName(p.fields[0]))return error(s,"Nickname: 1-32 ASCII letters, digits, _ or -");
 if(!s->user.name.empty())return error(s,"Already logged in");
 if(!users_.add(p.fields[0]))return error(s,"Nickname already in use");
 s->user.name=p.fields[0]; s->room="lobby"; ok(s,"Logged in; joined lobby");
}
}
