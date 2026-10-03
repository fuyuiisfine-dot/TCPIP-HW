#include "Repositories.h"
namespace chat {
void MessageRepository::add(const Message& m){auto& q=messages_[m.room]; q.push_back(m); if(q.size()>100)q.pop_front();}
std::deque<Message> MessageRepository::history(const std::string& room)const{auto it=messages_.find(room); return it==messages_.end()?std::deque<Message>{}:it->second;}
}
