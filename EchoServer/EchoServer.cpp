#include "EchoServer.h"
#include <shared_mutex>

namespace network
{
EchoServer::EchoServer()
    : hEchoEvent(INVALID_HANDLE_VALUE)
{
    hEchoEvent = CreateEvent(nullptr, false, false, nullptr);
    mContentsThread = std::thread(&EchoServer::ContentsThread, this);

}
void EchoServer::onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID)
{
    Player& player = *MY_NEW Player();
    player.Addr = addr;
    player.SessionID = sessionID.Value;

}
void EchoServer::onRecv(utility::Message *msg)
{
    std::lock_guard<std::mutex> lock(mQLock);
    mContentsQ.push(msg);

    RT_ASSERT(hEchoEvent != INVALID_HANDLE_VALUE);
    SetEvent(hEchoEvent);

}
void EchoServer::onSend(utility::Message *msg)
{
    // TODO : onSend에서 해야할일 있을까.
}
void EchoServer::onRelease(const SeqAndIdx &sessionID)
{
    std::lock_guard<std::mutex> lock(mPlayerMapLock);
    auto iter = mPlayerMap.find(sessionID.Value);

    RT_ASSERT(iter != mPlayerMap.end());
    mPlayerMap.erase(iter);
}

void EchoServer::packProc(Message &msg)
{
    __int64 ownerID = msg.GetOwnerID();
    std::shared_lock<std::mutex> slock(mPlayerMapLock);
    auto iter  = mPlayerMap.find(ownerID);
    if (iter == mPlayerMap.end())
    {
        MY_DELETE &msg;
        return;
    }
    Player &player = *iter->second;
    if (msg.DeCoding(player.SessionFK) == false)
    {
        //TODO : 조작된 패킷 처리.
    }
    WORD wType;
    msg >> wType;
    switch (wType)
    {
        //TODO : Packet Case 추가하기.
    }
}

bool EchoServer::popContentsQ(Message** msg)
{
    if (mContentsQ.empty())
    {
        return false;
    }
    *msg = mContentsQ.front();
    mContentsQ.pop();

    return true;
}


void EchoServer::ContentsThread()
{
    DWORD retval;
    while (1)
    {
        retval = WaitForSingleObject(hEchoEvent, INFINITE);
        RT_ASSERT(retval != WAIT_FAILED);
        Message *msg = nullptr;
        while (popContentsQ(&msg))
        {
            RT_ASSERT(msg != nullptr);
            packProc(*msg);
        }
    }
}

} // namespace network