#pragma once
#include "Repositories.h"
#include "domain/ClientSession.h"
#include <memory>
#include <vector>
namespace chat {
using Session=std::shared_ptr<ClientSession>;
class ChatServices {
 std::mutex mutex_;
 UserRepository users_; RoomRepository rooms_; MessageRepository messages_;
 std::vector<Session> sessions_;
 void broadcast(const std::string&,const Packet&);
 static void ok(const Session& s,const std::string& text){s->send({PacketType::Ok,{text}});}
 static void error(const Session& s,const std::string& text){s->send({PacketType::Error,{text}});}
 bool requireLogin(const Session&);
public:
 static bool validName(const std::string&);
 void attach(const Session&); void detach(const Session&); void interruptAll();
 void login(const Session&,const Packet&);
 void room(const Session&,const Packet&);
 void message(const Session&,const Packet&);
 void file(const Session&,const Packet&);
};
void handleAuth(ChatServices&,const Session&,const Packet&);
void handleRoom(ChatServices&,const Session&,const Packet&);
void handleMessage(ChatServices&,const Session&,const Packet&);
void handleFile(ChatServices&,const Session&,const Packet&);
}
