
#include <shared_mutex>

#include "ChattingServer.h"
#include "PacketCommon.h"
#include <iomanip>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

namespace network
{
ChattingServer::ChattingServer()
    : hEchoEvent(INVALID_HANDLE_VALUE)
{
    hEchoEvent = CreateEvent(nullptr, false, false, nullptr);
    mContentsThread = std::thread(&ChattingServer::contentsThread, this);
    mMonitorThread = std::thread(&ChattingServer::monitorThread, this);
}
void ChattingServer::onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID)
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
void ChattingServer::onRecv(utility::Message *msg)
{
    std::lock_guard<std::shared_mutex> lock(mQLock);
    mContentsQ.push(msg);

    RT_ASSERT(hEchoEvent != INVALID_HANDLE_VALUE);
    SetEvent(hEchoEvent);
}
void ChattingServer::onSend(utility::Message *msg)
{
    // Why : onSend에서 해야할일 있을까.
}
void ChattingServer::onRelease(const SeqAndIdx &sessionID)
{
    Player *player = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(mPlayerMapLock);
        auto iter = mPlayerMap.find(sessionID.Value);

        RT_ASSERT(iter != mPlayerMap.end());
        player = iter->second;
        mPlayerMap.erase(iter);
    }
    {
        Player &refPlayer = *player;
        Sector &sector = sectors[player->sectorX][player->sectorY];
        std::lock_guard<std::shared_mutex> lock(sector.mMutex);
        auto iter = sector.mPlayers.find(refPlayer.SessionID.Value);
        if (iter != sector.mPlayers.end())
        {
            sector.mPlayers.erase(refPlayer.SessionID.Value);
        }
    }
    Message *msg = MY_NEW(Message);
    *msg << reinterpret_cast<__int64>(player);
    pushDeferredQ(*msg);
}
bool ChattingServer::popContentsQ(Message **msg)
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
void ChattingServer::pushDeferredQ(Message &msg)
{
    std::lock_guard<std::shared_mutex> lock(mDeferredQLock);
    mDeferredReleaseQ.push(&msg);
}
bool ChattingServer::popDeferredQ(Message **msg)
{
    std::lock_guard<std::shared_mutex> lock(mDeferredQLock);
    if (mDeferredReleaseQ.empty())
    {
        return false;
    }
    *msg = mDeferredReleaseQ.front();
    mDeferredReleaseQ.pop();

    return true;
}
Player *ChattingServer::validatePlayerOrNull(Message &msg)
{
    Player *player = nullptr;
    __int64 ownerID = msg.GetOwnerID();
    SeqAndIdx seqIdx{0};
    seqIdx.Value = ownerID;

    __int8 FK;
    {
        std::shared_lock<std::shared_mutex> slock(mPlayerMapLock);
        auto iter = mPlayerMap.find(ownerID);
        if (iter == mPlayerMap.end())
        {
            return nullptr;
        }
        player = iter->second;
        player->lastTime = timeGetTime();
        FK = player->SessionFK;
    }
    msg.DeCoding(FK);
    return player;
}

bool ChattingServer::isValidateSeqNumber(Message &msg, Player &player)
{
    __int32 seqNumber;
    msg >> seqNumber;

    if (seqNumber != player.SeqNumber++)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return false;
    }
    return true;
}

void ChattingServer::packetProc(Message &msg, Player &player)
{
    __int16 wType;
    msg >> wType;
    ePacketType type = static_cast<ePacketType>(wType);

    if (isValidateSeqNumber(msg, player) == false)
    {
        return;
    }

    switch (type)
    {
    case ePacketType::CS_LOGIN:
    {
        loginProc(msg, player);
        break;
    }
    case ePacketType::CS_AUTH:
    {
        authProc(msg, player);
        break;
    }

    case ePacketType::CS_MOVE:
    {
        moveSectorProc(msg, player);
        break;
    }
    case ePacketType::CS_CHAT:
    {
        chatMessageProc(msg, player);
        break;
    }
    default:
    {
        // TODO : 임시로 중단.. 아직 공격 구현안했으므로 무조건 실수임.
        __debugbreak();
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
    }
    }
}

eRedisResult ChattingServer::getFixedKeyFromRedis(__int64 accountNo, __int8 &FK)
{
    // TODO : Redis에서 accountNo 를 Key로 FK를 가져오고 반환값을 통해 상태를 전송.
    FK = 0x00;
    return eRedisResult::Redis_Success;
}

void ChattingServer::moveSector(__int64 sessionID, const __int8 beforeX, const __int8 beforeY, const __int8 x, const __int8 y)
{
    __int16 before = beforeX * 50 + beforeY;
    __int16 after = x * 50 + y;
    Sector &beforeSector = sectors[beforeX][beforeY];
    Sector &afterSector = sectors[x][y];

    if (before < after)
    {
        std::lock_guard<std::shared_mutex> xlock1(beforeSector.mMutex);
        std::lock_guard<std::shared_mutex> xlock2(afterSector.mMutex);

        auto iter = beforeSector.mPlayers.find(sessionID);
        RT_ASSERT(iter != beforeSector.mPlayers.end());
        Player &player = *iter->second;
        beforeSector.mPlayers.erase(iter);

        auto iter2 = afterSector.mPlayers.find(sessionID);
        RT_ASSERT(iter2 == afterSector.mPlayers.end());
        afterSector.mPlayers.insert({sessionID, &player});
        player.sectorX = x;
        player.sectorY = y;
    }
    else
    {
        std::lock_guard<std::shared_mutex> xlock2(afterSector.mMutex);
        std::lock_guard<std::shared_mutex> xlock1(beforeSector.mMutex);

        auto iter = beforeSector.mPlayers.find(sessionID);
        RT_ASSERT(iter != beforeSector.mPlayers.end());
        Player &player = *iter->second;
        beforeSector.mPlayers.erase(iter);

        auto iter2 = afterSector.mPlayers.find(sessionID);
        RT_ASSERT(iter2 == afterSector.mPlayers.end());
        afterSector.mPlayers.insert({sessionID, &player});
        player.sectorX = x;
        player.sectorY = y;
    }
    
}

void ChattingServer::aroundLockAndSendMsg(__int16 x, __int16 y, wchar_t Nickname[20], __int16 MessageLen, wchar_t *const buffer)
{
    __int16 dx[] = {-1, -1, -1, +0, 0, 0, +1, 1, 1};
    __int16 dy[] = {-1, +0, +1, -1, 0, 1, -1, 0, 1};

    std::vector<std::shared_lock<std::shared_mutex>> vec;
    std::vector<std::pair<__int16, __int16>> pos;
    vec.reserve(9);
    pos.reserve(9);
    int vecSize = 0;
    for (__int16 loop = 0; loop < 9; ++loop)
    {
        __int16 rx = x + dx[loop];
        __int16 ry = y + dy[loop];
        if (rx < 0 || ry < 0 || 50 <= rx || 50 <= ry)
        {
            continue;
        }
        ++vecSize;
        vec.emplace_back(sectors[rx][ry].mMutex);
        pos.emplace_back(std::make_pair(rx, ry));
    }
    for (int idx = 0; idx < vecSize; ++idx)
    {
        for (auto hash : sectors[pos[idx].first][pos[idx].second].mPlayers)
        {
            Player &player = *hash.second;
            Message *msg = MY_NEW Message();
            makeChatMessage(player.SessionFK, player.SessionID.Value, Nickname, MessageLen, buffer, *msg);
            sendPost(player.SessionID, *msg);
        }
    }
}

void ChattingServer::makeLoginMessage(const __int8 FK, const eRedisResult &result, const __int32 seqNumber, const __int64 sessionID, Message &msg)
{
    //{
    //  __int16    Type
    //
    //  __int8  Result
    //  __int32 SeqNumber
    //}// 이때 랜덤값으로 클라이언트에게 송신
    __int8 RK = rand() % UCHAR_MAX + 1;
    msg.InitMessage(sessionID, RK);

    __int16 type = static_cast<__int16>(ePacketType::SC_LOGIN);

    Header header;
    header.Len = static_cast<__int16>(sizeof(type) + sizeof(result) + sizeof(seqNumber));
    header.RandKey = RK;

    msg.PutData(&header, sizeof(header));
    msg << type;
    msg << static_cast<__int8>(result);
    msg << seqNumber;

    msg.EnCoding(FK);
}

void ChattingServer::makeAuthMessage(const __int8 FK, const __int64 sessionID, const __int8 SectorX, const __int8 SectorY, Message &msg)
{
    //{
    //  __int16 SectorX
    //  __int16 SectorY
    //}  초기 생성 위치. 그리고 실패시 server에서는 보낸것 확인 후 끊기
    __int16 type = static_cast<__int16>(ePacketType::SC_AUTH);

    __int8 RK = rand() % UCHAR_MAX + 1;
    msg.InitMessage(sessionID, RK);

    Header header;
    header.Len = static_cast<__int16>(sizeof(type) + sizeof(SectorX) + sizeof(SectorY));
    header.RandKey = RK;

    msg.PutData(&header, sizeof(header));
    msg << type;
    msg << SectorX;
    msg << SectorY;

    msg.EnCoding(FK);
}
void ChattingServer::makeChatMessage(const __int8 FK, const __int64 sessionID, wchar_t Nickname[20], __int16 MessageLen, wchar_t *const buffer, Message &msg)
{
    //{
    //  wchar_t Nickname[20]
    //  __int16 MessageLen
    //  wchar_t Message[MessageLen]
    //}
    __int16 type = static_cast<__int16>(ePacketType::SC_CHAT);

    __int8 RK = rand() % UCHAR_MAX + 1;
    msg.InitMessage(sessionID, RK);

    Header header;
    header.Len = static_cast<__int16>(sizeof(type) + sizeof(wchar_t) * CONFIG_NICKNAME_LEN + sizeof(MessageLen) + sizeof(wchar_t) * MessageLen);
    header.RandKey = RK;

    msg.PutData(&header, sizeof(header));

    msg << type;
    msg.PutData(Nickname, sizeof(wchar_t) * CONFIG_NICKNAME_LEN);
    msg << MessageLen;
    msg.PutData(buffer, MessageLen * sizeof(wchar_t));

    msg.EnCoding(FK);
}
void ChattingServer::loginProc(Message &msg, Player &player)
{
    //{
    //  __int64 AccountNo
    //} 첫 메세지는 초기 고정키로 암호화
    //  서버에서 Redis에서 AccountNo를 통해SessionKey 탐색 시도

    __int64 accountNo;
    msg >> accountNo;
    size_t useSize = msg.GetUseSize();
    RT_ASSERT(useSize == 0);
    // TODO : Redis에서 FK값을 가져와 회신하기.
    __int8 FK;
    eRedisResult bRetval = getFixedKeyFromRedis(accountNo, FK);
    __int32 seqNumber = rand() % INT_MAX;

    player.SessionFK = FK;
    player.SeqNumber = seqNumber;

    makeLoginMessage(FK, bRetval, seqNumber, player.SessionID.Value, msg);
    sendPost(player.SessionID, msg);
}
void ChattingServer::authProc(Message &msg, Player &player)
{
    //{
    //	wchar_t	Nickname[20]		// null 포함
    //} 이때 부터 SessionKey로 암호화를 통한 세션 인증과
    if (player.bAuth == true)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return;
    }
    wchar_t Nickname[20]{0};
    // 2번째 매개변수가 Byte 단위임
    msg.GetData(Nickname, 40);
    size_t useSize = msg.GetUseSize();
    RT_ASSERT(useSize == 0);

    memcpy_s(player.Nickname, sizeof(player.Nickname), Nickname, sizeof(Nickname));

    __int8 SectorX = rand() % 50;
    __int8 SectorY = rand() % 50;
    player.sectorX = SectorX;
    player.sectorY = SectorY;
    player.bAuth = true;
    {
        Sector &sector = sectors[SectorX][SectorY];
        std::lock_guard<std::shared_mutex> lock(sector.mMutex);
        auto iter = sector.mPlayers.find(player.SessionID.Value);
        RT_ASSERT(iter == sector.mPlayers.end());
        sector.mPlayers.insert({player.SessionID.Value, &player});
    }
    makeAuthMessage(player.SessionFK, player.SessionID.Value, SectorX, SectorY, msg);
    sendPost(player.SessionID, msg);
}

void ChattingServer::moveSectorProc(Message &msg, Player &player)
{
    if (player.bAuth == false)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return;
    }
    //{
    //  __int8 SectorX
    //  __int8 SectorY
    //}
    __int8 SectorX;
    __int8 SectorY;

    msg >> SectorX;
    msg >> SectorY;
    size_t useSize = msg.GetUseSize();
    RT_ASSERT(useSize == 0);
    // 조작된 패킷 or 잘못된 패킷
    if (0 > SectorX || 50 <= SectorX)
    {
        disconnectSession(player.SessionID);
    }
    else if (0 > SectorY || 50 <= SectorY)
    {
        disconnectSession(player.SessionID);
    }
    // 공격 의심
    else if (SectorX == player.sectorX && SectorY == player.sectorY)
    {
        disconnectSession(player.SessionID);
    }
    else
    {
        moveSector(player.SessionID.Value, player.sectorX, player.sectorY, SectorX, SectorY);
    }
    MY_DELETE & msg;
}

void ChattingServer::chatMessageProc(Message &msg, Player &player)
{
    if (player.bAuth == false)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return;
    }
    //{
    //  __int16 MessageLen
    //  wchar_t Message[MessageLen]
    //}
    __int16 MessageLen;
    wchar_t Message[CONFIG_MSG_MAX_LEN];
    msg >> MessageLen;

    // 범위를 벗어남.
    if (CONFIG_MSG_MAX_LEN < MessageLen || MessageLen <= 0)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return;
    }
    msg.GetData(Message, MessageLen * 2);
    size_t useSize = msg.GetUseSize();
    RT_ASSERT(useSize == 0);
    MY_DELETE & msg;

    aroundLockAndSendMsg(player.sectorX, player.sectorY, player.Nickname, MessageLen,Message);
}

void ChattingServer::contentsThread()
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
            Player *player = validatePlayerOrNull(*msg);
            if (player == nullptr)
            {
                MY_DELETE msg;
                continue;
            }
            packetProc(*msg, *player);
        }
        // 지연 삭제
        while (1)
        {
            if (!popDeferredQ(&msg))
            {
                break;
            }
            RT_ASSERT(msg != nullptr);
            __int64 playerPtr;
            *msg >> playerPtr;

            Player *player = reinterpret_cast<Player *>(playerPtr);
            MY_DELETE player;
            MY_DELETE msg;
        }
    }
}

void ChattingServer::monitorThread()
{
    timeBeginPeriod(1);
    // TODO : Server 종료 신호를 받고 종료되는 코드 만들기.
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

std::ostream &operator<<(std::ostream &out, const ChattingServer &server)
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