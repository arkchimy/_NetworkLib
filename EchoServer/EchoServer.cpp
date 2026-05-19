
#include <shared_mutex>

#include "EchoServer.h"
#include "PacketCommon.h"
#include <iomanip>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

namespace network
{
EchoServer::EchoServer()
    : hEchoEvent(INVALID_HANDLE_VALUE)
{
    hEchoEvent = CreateEvent(nullptr, false, false, nullptr);
    mContentsThread = std::thread(&EchoServer::contentsThread, this);
    mMonitorThread = std::thread(&EchoServer::monitorThread, this);
}
void EchoServer::onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID)
{
    Player &player = *MY_NEW Player();
    player.Addr = addr;
    player.SessionID = sessionID;

    {
        std::lock_guard<std::shared_mutex> lock(mPlayerMapLock);
        auto iter = mPlayerMap.find(sessionID.Value);
        // 같은 sessionID로 두번연속 올수 없음.
        RT_ASSERT(iter == mPlayerMap.end());
        mPlayerMap.insert({sessionID.Value, &player});
    }
}
void EchoServer::onRecv(utility::Message *msg)
{
    std::lock_guard<std::shared_mutex> lock(mQLock);
    mContentsQ.push(msg);

    RT_ASSERT(hEchoEvent != INVALID_HANDLE_VALUE);
    SetEvent(hEchoEvent);
}
void EchoServer::onSend(utility::Message *msg)
{
    // Why : onSend에서 해야할일 있을까.
}
void EchoServer::onRelease(const SeqAndIdx &sessionID)
{
    std::lock_guard<std::shared_mutex> lock(mPlayerMapLock);
    auto iter = mPlayerMap.find(sessionID.Value);

    RT_ASSERT(iter != mPlayerMap.end());
    Player *player = iter->second;
    mPlayerMap.erase(iter);
    MY_DELETE player;
}

void EchoServer::packProc(Message &msg)
{
    __int64 ownerID = msg.GetOwnerID();
    SeqAndIdx seqIdx;
    seqIdx.Value = ownerID;
    {
        std::shared_lock<std::shared_mutex> slock(mPlayerMapLock);
        auto iter = mPlayerMap.find(ownerID);
        if (iter == mPlayerMap.end())
        {
            MY_DELETE & msg;
            return;
        }
        Player &player = *iter->second;
        if (msg.DeCoding(player.SessionFK) == false)
        {
            disconnectSession(seqIdx);
            MY_DELETE & msg;
            return;
        }
    }

    WORD wType;
    msg >> wType;
    ePacketType type = static_cast<ePacketType>(wType);
    switch (type)
    {
    case ePacketType::CS_ECHO_REQ:
    {
        procEchoMessage(seqIdx, msg);
    }
    break;
    default:
    {
        // TODO : 임시로 중단.. 아직 공격 구현안했으므로 무조건 실수임.
        __debugbreak();
        SeqAndIdx seqIdx{ownerID};
        disconnectSession(seqIdx);
        MY_DELETE & msg;
    }
    }
}

void EchoServer::procEchoMessage(const SeqAndIdx &sessionID, Message &msg)
{
    __int16 strLen;
    msg >> strLen;
    if (CONTENTS_MSG_MAX_SIZE < strLen)
    {
        disconnectSession(sessionID);
        MY_DELETE & msg;
        return;
    }
    char buffer[CONTENTS_MSG_MAX_SIZE];
    msg.GetData(buffer, strLen);

    __int8 sessionFK;
    {
        std::shared_lock<std::shared_mutex> lock(mPlayerMapLock);
        auto iter = mPlayerMap.find(sessionID.Value);
        if (iter == mPlayerMap.end())
        {
            MY_DELETE & msg;
            return;
        }
        sessionFK = iter->second->SessionFK;
    }

    Header header{strLen + 4, rand() % 256};

    msg.InitMessage(sessionID.Value, header.RandKey);
    msg.PutData(&header, sizeof(header));
    msg << static_cast<__int16>(ePacketType::SC_ECHO_RES);
    msg << strLen;
    msg.PutData(buffer, strLen);
    msg.EnCoding(sessionFK);

    sendPost(sessionID, msg);
}

bool EchoServer::popContentsQ(Message **msg)
{
    std::lock_guard<std::shared_mutex> lock(mQLock);
    if (mContentsQ.empty())
    {
        return false;
    }
    *msg = mContentsQ.front();
    mContentsQ.pop();

    return true;
}

void EchoServer::contentsThread()
{
    DWORD retval;
    while (1)
    {
        retval = WaitForSingleObject(hEchoEvent, 1000);
        RT_ASSERT(retval != WAIT_FAILED);
        Message *msg;

        while (1)
        {
            if (!popContentsQ(&msg))
            {
                break;
            }
            
            RT_ASSERT(msg != nullptr);
            packProc(*msg);
        }
    }
}

void EchoServer::monitorThread()
{
    timeBeginPeriod(1);
    //TODO : Server 종료 신호를 받고 종료되는 코드 만들기.
    constexpr int timeInterver = 1000;

    DWORD currentTime = timeGetTime();
    DWORD nextTime = currentTime + timeInterver;
    while (1)
    {
        currentTime = timeGetTime();
        if (nextTime <= currentTime)
        {
            std::cout << *this;
            nextTime += timeInterver;
        }
        else
        {
            Sleep(nextTime - currentTime);
        }
    }

    timeEndPeriod(1);
}

std::ostream &operator<<(std::ostream &out, const EchoServer& server) 
{
    thread_local __int64 beforeAccept;
    thread_local __int64 beforeRecv;
    thread_local __int64 beforeSend;

    __int64 currentAccept = server.GetAcceptCount();
    __int64 currentRecv = server.GetRecvCount();
    __int64 currentSend = server.GetSendCount();

    out << std::setw(10) << "Total AcceptCnt : " << std::setw(10) << currentAccept << "\n";
    out << std::setw(10) << " AcceptCnt TPS : " << std::setw(10) << currentAccept - beforeAccept << "\n";
    out << std::setw(10) << " RecvCnt TPS : " << std::setw(10) << currentRecv - beforeRecv << "\n";
    out << std::setw(10) << " SendCnt TPS : " << std::setw(10) << currentSend - beforeSend << "\n";

    beforeAccept = currentAccept;
    beforeRecv = currentRecv;
    beforeSend = currentSend;

    return out;
}

} // namespace network