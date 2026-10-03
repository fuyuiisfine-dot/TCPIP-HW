#include "ChatServices.h"
#include <algorithm>
namespace chat {
bool ChatServices::validName(const std::string& s){return !s.empty() && s.size()<=32 && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';});}
bool ChatServices::requireLogin(const Session& s){if(!s->user.name.empty())return true; error(s,"Login required"); return false;}
void ChatServices::attach(const Session& s){std::lock_guard<std::mutex> lock(mutex_); sessions_.push_back(s);}
void ChatServices::detach(const Session& s){std::lock_guard<std::mutex> lock(mutex_); users_.remove(s->user.name); sessions_.erase(std::remove(sessions_.begin(),sessions_.end(),s),sessions_.end());}
void ChatServices::interruptAll(){std::lock_guard<std::mutex> lock(mutex_); for(auto& s:sessions_)s->interrupt();}
void ChatServices::broadcast(const std::string& room,const Packet& p){for(auto& s:sessions_)if(s->room==room&&!s->user.name.empty())s->send(p);}
}
