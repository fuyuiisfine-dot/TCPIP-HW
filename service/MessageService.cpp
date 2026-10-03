#include "ChatServices.h"
#include <chrono>
namespace chat {
void ChatServices::message(const Session& s,const Packet& p){
 std::lock_guard<std::mutex> lock(mutex_); if(!requireLogin(s))return;
 if(s->room.empty())return error(s,"Join a room first");
 if(p.type==PacketType::History){
  if(!p.fields.empty())return error(s,"History takes no fields");
  for(const auto& m:messages_.history(s->room))s->send({PacketType::Message,{m.room,m.sender,m.text,m.timestamp}});
  return ok(s,"End of history");
 }
 if(p.fields.size()!=1||p.fields[0].empty()||p.fields[0].size()>4096)return error(s,"Message must contain 1-4096 bytes");
 auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Message m{s->room,s->user.name,p.fields[0],std::to_string(now)}; messages_.add(m);
 broadcast(s->room,{PacketType::Message,{m.room,m.sender,m.text,m.timestamp}});
}
}
