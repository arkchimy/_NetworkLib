#pragma once

#include <AcceptEx_IOCP_NetworkLib/AcceptEx_IOCP_NetworkLib.h>
#pragma comment(lib, "AcceptEx_IOCP_NetworkLib.lib")

#include <map>
#include <queue>

namespace network
{
using namespace utility;

struct Player
{
    SOCKADDR_IN Addr{0};
    __int64 SessionID = 0;
    char SessionFK = '\xFF';
};

class EchoServer : public NetworkLib
{
  public:
    EchoServer();

  private:
    virtual void onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID);
    virtual void onRecv(Message *msg);
    virtual void onSend(Message *msg);
    virtual void onRelease(const SeqAndIdx &sessionID);

    void packProc(Message& msg);
    bool popContentsQ(Message** msg);
  private:
    void ContentsThread();
  private:
    std::thread mContentsThread;
    HANDLE hEchoEvent;

    std::mutex mQLock;
    std::queue<Message *> mContentsQ;

    std::mutex mPlayerMapLock;
    std::map<__int64, Player *> mPlayerMap;
};
} // namespace network
