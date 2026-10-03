#include "protocol/Packet.h"
#include <stdexcept>
namespace chat {
static void u32(std::string& out,uint32_t n) { n=htonl(n); out.append(reinterpret_cast<const char*>(&n),4); }
static void u16(std::string& out,uint16_t n) { n=htons(n); out.append(reinterpret_cast<const char*>(&n),2); }
std::string encodePacket(const Packet& p) {
 if(p.fields.size()>16) throw std::runtime_error("Too many fields");
 std::string body;
 for(const auto& field:p.fields) {
  if(field.size()>MaxPayload || body.size()+4+field.size()>MaxPayload) throw std::runtime_error("Packet too large");
  u32(body,static_cast<uint32_t>(field.size())); body+=field;
 }
 std::string out; u32(out,Magic); u16(out,Version); u16(out,static_cast<uint16_t>(p.type)); u32(out,static_cast<uint32_t>(body.size())); return out+body;
}
bool sendPacket(SOCKET socket,const Packet& p) {
 const auto bytes=encodePacket(p); size_t offset=0;
 while(offset<bytes.size()) { int n=send(socket,bytes.data()+offset,static_cast<int>(bytes.size()-offset),0); if(n<=0) return false; offset+=n; }
 return true;
}
}
