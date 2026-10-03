#include "ChatServices.h"
namespace chat {
void ChatServices::room(const Session& s,const Packet& p){
 std::lock_guard<std::mutex> lock(mutex_); if(!requireLogin(s))return;
 const bool needsName=p.type==PacketType::CreateRoom||p.type==PacketType::JoinRoom;
 if(p.fields.size()!=(needsName?1u:0u))return error(s,"Invalid room request");
 if(needsName&&!validName(p.fields[0]))return error(s,"Invalid room name");
 switch(p.type){
 case PacketType::ListRooms:s->send({PacketType::Rooms,{rooms_.list()}});break;
 case PacketType::CreateRoom:if(!rooms_.add(p.fields[0]))return error(s,"Room exists or room limit reached"); ok(s,"Room created; use /join to enter");break;
 case PacketType::JoinRoom:if(!rooms_.contains(p.fields[0]))return error(s,"Room not found"); s->room=p.fields[0];ok(s,"Joined "+s->room);break;
 case PacketType::LeaveRoom:s->room.clear();ok(s,"Left room");break;
 default:error(s,"Invalid room request");
 }
}
}
