#include "protocol/Packet.h"
#include <cstring>
namespace chat {
static bool exact(SOCKET s,char* p,size_t size) { while(size) { int n=recv(s,p,static_cast<int>(size),0); if(n<=0)return false; p+=n; size-=n; } return true; }
static uint32_t u32(const char* p) { uint32_t n; std::memcpy(&n,p,4); return ntohl(n); }
static uint16_t u16(const char* p) { uint16_t n; std::memcpy(&n,p,2); return ntohs(n); }
bool receivePacket(SOCKET s,Packet& p) {
 char header[HeaderSize]; if(!exact(s,header,sizeof header))return false;
 auto size=u32(header+8); if(u32(header)!=Magic || u16(header+4)!=Version || size>MaxPayload)return false;
 std::string body(size,'\0'); if(!exact(s,body.data(),size))return false;
 p.type=static_cast<PacketType>(u16(header+6)); p.fields.clear(); size_t pos=0;
 while(pos<body.size()) {
  if(body.size()-pos<4 || p.fields.size()>=16)return false;
  auto len=u32(body.data()+pos); pos+=4; if(len>body.size()-pos)return false;
  p.fields.push_back(body.substr(pos,len)); pos+=len;
 }
 return true;
}
}
