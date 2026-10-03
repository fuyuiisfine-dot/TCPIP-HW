#include "Server.h"
#include <ws2tcpip.h>
#include <thread>
#include <iostream>
#include <stdexcept>
namespace chat {
void Server::serve(const Session& s){
 try {
 Packet p{};
 while(!stopping_&&receivePacket(s->socket,p)){
  switch(p.type){
  case PacketType::Login:handleAuth(services_,s,p);break;
  case PacketType::ListRooms:case PacketType::CreateRoom:case PacketType::JoinRoom:case PacketType::LeaveRoom:handleRoom(services_,s,p);break;
  case PacketType::SendMessage:case PacketType::History:handleMessage(services_,s,p);break;
  case PacketType::SendFile:handleFile(services_,s,p);break;
  default:s->send({PacketType::Error,{"Unknown packet type"}});
  }
 }
 }catch(const std::exception& e){std::cerr<<"Client error: "<<e.what()<<'\n';}
 services_.detach(s); s->close();
}
void Server::run(unsigned short port,const std::string& bindAddress,std::promise<void>& started){
 SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
 if(listener==INVALID_SOCKET)throw std::runtime_error("socket failed");
 BOOL exclusive=TRUE; setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof exclusive);
 sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(port);
 if(inet_pton(AF_INET,bindAddress.c_str(),&addr.sin_addr)!=1){closesocket(listener);throw std::runtime_error("Invalid bind IPv4 address");}
 if(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof addr)==SOCKET_ERROR||listen(listener,SOMAXCONN)==SOCKET_ERROR){closesocket(listener);throw std::runtime_error("bind/listen failed (port in use?)");}
 struct Worker { std::thread thread; std::shared_ptr<std::atomic<bool>> done; };
 std::vector<Worker> workers;
 started.set_value();
 std::cout<<"Listening on "<<bindAddress<<":"<<port<<"; enter /quit to stop."<<std::endl;
 try {
 while(!stopping_){
  for(auto it=workers.begin();it!=workers.end();) {if(*it->done){it->thread.join();it=workers.erase(it);}else ++it;}
  fd_set readable; FD_ZERO(&readable); FD_SET(listener,&readable); timeval timeout{0,200000};
  int ready=select(0,&readable,nullptr,nullptr,&timeout); if(ready==SOCKET_ERROR)throw std::runtime_error("select failed"); if(!ready)continue;
  SOCKET peer=accept(listener,nullptr,nullptr); if(peer==INVALID_SOCKET)continue;
  if(workers.size()>=64){closesocket(peer);continue;}
  DWORD sendTimeout=3000, receiveTimeout=300000;
  setsockopt(peer,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&sendTimeout),sizeof sendTimeout);
  setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&receiveTimeout),sizeof receiveTimeout);
  auto session=std::make_shared<ClientSession>(peer); auto done=std::make_shared<std::atomic<bool>>(false);
  services_.attach(session);
  try {workers.push_back({std::thread{},done}); workers.back().thread=std::thread([this,session,done]{serve(session);*done=true;});}
  catch(...){services_.detach(session);session->close();if(!workers.empty()&&!workers.back().thread.joinable())workers.pop_back();throw;}
 }
 }catch(...){stopping_=true;closesocket(listener);services_.interruptAll();for(auto& w:workers)w.thread.join();throw;}
 closesocket(listener);services_.interruptAll();for(auto& w:workers)w.thread.join();
}
}
