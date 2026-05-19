#pragma once

#include <AcceptEx_IOCP_NetworkLib/AcceptEx_IOCP_NetworkLib.h>

#include <iostream>
#include <map>
#include <queue>

#pragma comment(lib, "AcceptEx_IOCP_NetworkLib.lib")

namespace network
{
using namespace utility;

struct Player
{
    SOCKADDR_IN Addr{0};
    SeqAndIdx SessionID{0};
    __int8 SessionFK = '\xFF';
};

class EchoServer : public NetworkLib
{
  public:
    EchoServer();
    friend std::ostream &operator<<(std::ostream &out, const EchoServer& server);

  private:
    virtual void onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID);
    virtual void onRecv(Message *msg);
    virtual void onSend(Message *msg);
    virtual void onRelease(const SeqAndIdx &sessionID);

    void packProc(Message &msg);
    void procEchoMessage(const SeqAndIdx &sessionID, Message &msg);

    bool popContentsQ(Message **msg);

  private:
    void contentsThread();
    void monitorThread();

  private:
    std::thread mContentsThread;
    std::thread mMonitorThread;

    HANDLE hEchoEvent;

    std::shared_mutex mQLock;
    std::queue<Message *> mContentsQ;

    std::shared_mutex mPlayerMapLock;
    std::map<__int64, Player *> mPlayerMap;

};
} // namespace network
