#include <iomanip>
#include <shared_mutex>

#include "ChattingServer.h"

#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

thread_local cpp_redis::client *gRedisClient;

namespace network
{
ChattingServer::ChattingServer()
    : hEchoEvent(INVALID_HANDLE_VALUE),
      mContentsTPS(0),
      mContentsQSize(0),
      mUserCnt(0),
      mPacketTypeTPS{0}
{
    hEchoEvent = CreateEvent(nullptr, false, false, nullptr);
    mContentsThread = std::thread(&ChattingServer::contentsThread, this);
    mMonitorThread = std::thread(&ChattingServer::monitorThread, this);

    start();
}
void ChattingServer::onAccept(SOCKADDR_IN &addr, const SeqAndIdx &sessionID)
{
    Message &msg = *MY_NEW Message();
    msg.InitMessage(sessionID.Value, '\xFF');
    msg << static_cast<__int16>(ePacketType::CHAT_PLAYER_ALLOC);
    msg.PutData(&addr, sizeof(SOCKADDR_IN));
    pushContentsQ(&msg);

    SetEvent(hEchoEvent);
}
void ChattingServer::onRecv(utility::Message *msg)
{
    std::lock_guard<std::shared_mutex> lock(mQLock);
    mContentsQ.push(msg);
    mContentsQSize = mContentsQ.size();
    RT_ASSERT(hEchoEvent != INVALID_HANDLE_VALUE);
    SetEvent(hEchoEvent);
}
void ChattingServer::onSend(utility::Message *msg)
{
    // Why : onSend에서 해야할일 있을까.
}
void ChattingServer::onRelease(const SeqAndIdx &sessionID)
{
    Message &msg = *MY_NEW Message();
    msg.InitMessage(sessionID.Value, '\xFF');
    msg << static_cast<__int16>(ePacketType::CHAT_PLAYER_DELETE);
    pushContentsQ(&msg);

    SetEvent(hEchoEvent);
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
    mContentsQSize = mContentsQ.size();
    return true;
}

void ChattingServer::pushContentsQ(Message *msg)
{
    std::lock_guard<std::shared_mutex> lock(mQLock);
    mContentsQ.push(msg);
    mContentsQSize = mContentsQ.size();
}

Player *ChattingServer::validatePlayerOrNull(Message &msg)
{
    Player *player = nullptr;
    __int64 ownerID = msg.GetOwnerID();
    __int8 FK;
    {
        // std::shared_lock<std::shared_mutex> slock(mPlayerMapLock);
        auto iter = mPlayerMap.find(ownerID);
        if (iter == mPlayerMap.end())
        {
            MY_DELETE msg;
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

void ChattingServer::packetProc(Message &msg)
{
    __int16 wType;
    msg >> wType;
    ePacketType type = static_cast<ePacketType>(wType);

    switch (type)
    {
    case ePacketType::CHAT_PLAYER_ALLOC:
    case ePacketType::CHAT_PLAYER_DELETE:
        chatProc(msg, wType);
        return;
    }

    Player *playerNullTest = validatePlayerOrNull(msg);

    if (playerNullTest == nullptr)
    {
        // 맵에서 찾지 못했음. 내부에서 msg 반환
        return;
    }
    Player &player = *playerNullTest;

    if (isValidateSeqNumber(msg, player) == false)
    {
        return;
    }
    ++mPacketTypeTPS[wType];

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

std::string HgetSync(cpp_redis::client &client,
                     const std::string &key,
                     const std::string &field)
{
    std::promise<std::string> prom;
    auto fut = prom.get_future();

    client.hget(key, field, [&prom](cpp_redis::reply &reply)
                { prom.set_value(reply.is_null() ? "" : reply.as_string()); });

    client.sync_commit();
    return fut.get();
}

eRedisResult ChattingServer::getFixedKeyFromRedis(__int64 accountNo, Player &player) const
{

    std::string key = "session:" + std::to_string(accountNo);

    std::string tokenKey = HgetSync(*gRedisClient, key, "sessionToken");
    std::string encodeKey = HgetSync(*gRedisClient, key, "fixedKey");

    if (encodeKey.empty() || tokenKey.empty())
    {
        return eRedisResult::Redis_No_Data;
    }

    RT_ASSERT(tokenKey.size() == CONFIG_TOKENKEY_LEN);

    player.SessionFK = static_cast<__int8>(std::stoi(encodeKey));
    memcpy_s(player.tokenKey, CONFIG_TOKENKEY_LEN, tokenKey.c_str(), CONFIG_TOKENKEY_LEN);
    return eRedisResult::Redis_Success;
}

void ChattingServer::moveSector(__int64 sessionID, const __int8 beforeX, const __int8 beforeY, const __int8 x, const __int8 y)
{
    __int16 before = beforeX * 50 + beforeY;
    __int16 after = x * 50 + y;
    Sector &beforeSector = mSectors[beforeX][beforeY];
    Sector &afterSector = mSectors[x][y];

    if (before < after)
    {
        // std::lock_guard<std::shared_mutex> xlock1(beforeSector.mMutex);
        // std::lock_guard<std::shared_mutex> xlock2(afterSector.mMutex);

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
        // std::lock_guard<std::shared_mutex> xlock2(afterSector.mMutex);
        // std::lock_guard<std::shared_mutex> xlock1(beforeSector.mMutex);

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

    // std::vector<std::shared_lock<std::shared_mutex>> vec;
    std::vector<std::pair<__int16, __int16>> pos;
    // vec.reserve(9);
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
        // vec.emplace_back(sectors[rx][ry].mMutex);
        pos.emplace_back(std::make_pair(rx, ry));
    }
    for (int idx = 0; idx < vecSize; ++idx)
    {
        for (auto hash : mSectors[pos[idx].first][pos[idx].second].mPlayers)
        {
            Player &player = *hash.second;
            // Why : 과연 로직의 느림때문일까 측정하기위한 방법.
            //if (wcscmp(player.Nickname, Nickname) != 0)
            //{
            //    continue;
            //}
            Message *msg = MY_NEW Message();
            msg->InitMessage(player.SessionID.Value, '\xFF');
            makeChatMessage(player.SessionFK, player.SessionID.Value, Nickname, MessageLen, buffer, *msg);
            // std::cout << std::setw(10) << "SendMsg" << std::setw(8) << player.SessionID.Value
            //           << std::setw(10) << "AccounNo :" << std::setw(8) << player.accountNo << "\n";
            RT_ASSERT(player.bAuth == true);
            sendPost(player.SessionID, *msg);
            ++mPacketTypeTPS[static_cast<__int64>(ePacketType::SC_CHAT)];
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
    __int8 RK = rand() % UCHAR_MAX;
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

    __int8 RK = rand() % UCHAR_MAX;
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

    __int8 RK = rand() % UCHAR_MAX;
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
    eRedisResult bRetval = getFixedKeyFromRedis(accountNo, player);
    if (bRetval != eRedisResult::Redis_Success)
    {
        MY_DELETE & msg;
        disconnectSession(player.SessionID);
        return;
    }
    __int32 seqNumber = rand() % INT_MAX;

    player.SeqNumber = seqNumber;
    player.accountNo = accountNo;

    makeLoginMessage(player.SessionFK, bRetval, seqNumber, player.SessionID.Value, msg);
    // std::cout <<std::right << std::setw(10) << "loginProc" << std::setw(8) << player.SessionID.Value
    //           << std::setw(10) << "AccounNo :" << std::setw(8) << accountNo << "\n";
    sendPost(player.SessionID, msg);
    ++mPacketTypeTPS[static_cast<__int64>(ePacketType::SC_LOGIN)];
}
void ChattingServer::authProc(Message &msg, Player &player)
{
    //{
    //	wchar_t	Nickname[20]		// null 포함
    //  char TokenKey[20]           // LoginServer에서 받아온 것
    //} 이때 부터 SessionKey로 암호화를 통한 세션 인증과
    RT_ASSERT(player.bAuth == false);
    if (player.bAuth == true)
    {
        disconnectSession(player.SessionID);
        MY_DELETE & msg;
        return;
    }

    wchar_t Nickname[20]{0};
    // 2번째 매개변수가 Byte 단위임
    msg.GetData(Nickname, 40);

    char tokenKey[20]{0};
    // 2번째 매개변수가 Byte 단위임
    msg.GetData(tokenKey, 20);

    size_t useSize = msg.GetUseSize();
    RT_ASSERT(useSize == 0);

    memcpy_s(player.Nickname, sizeof(player.Nickname), Nickname, sizeof(Nickname));
    if (memcmp(tokenKey, player.tokenKey, CONFIG_TOKENKEY_LEN) != 0)
    {
        MY_DELETE & msg;
        disconnectSession(player.SessionID);
        return;
    }

    __int8 SectorX = rand() % 50;
    __int8 SectorY = rand() % 50;
    player.sectorX = SectorX;
    player.sectorY = SectorY;
    player.bAuth = true;
    {
        Sector &sector = mSectors[SectorX][SectorY];
        // std::lock_guard<std::shared_mutex> lock(sector.mMutex);
        auto iter = sector.mPlayers.find(player.SessionID.Value);
        RT_ASSERT(iter == sector.mPlayers.end());
        sector.mPlayers.insert({player.SessionID.Value, &player});
    }
    makeAuthMessage(player.SessionFK, player.SessionID.Value, SectorX, SectorY, msg);
    // std::cout << std::setw(10) << "authProc" << std::setw(8) << player.SessionID.Value
    //           << std::setw(10) << "AccounNo :" << std::setw(8) << player.accountNo << "\n";
    sendPost(player.SessionID, msg);
    ++mPacketTypeTPS[static_cast<__int64>(ePacketType::SC_AUTH)];
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

    aroundLockAndSendMsg(player.sectorX, player.sectorY, player.Nickname, MessageLen, Message);
}

void ChattingServer::contentsThread()
{

    cpp_redis::client Client;
    gRedisClient = &Client;
    gRedisClient->connect(CONFIG_REDIS_IP, CONFIG_REDIS_PORT);

    DWORD retval;
    while (1)
    {
        retval = WaitForSingleObject(hEchoEvent, 1000);
        Message *msg = nullptr;
        RT_ASSERT(retval != WAIT_FAILED);
        while (1)
        {
            if (!popContentsQ(&msg))
            {
                break;
            }
            RT_ASSERT(msg != nullptr);
            packetProc(*msg);
            ++mContentsTPS;
        }
    }
}

void ChattingServer::monitorThread()
{
    timeBeginPeriod(1);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // 커서 숨기기 (깜빡임 방지)
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    constexpr int timeInterver = 1000;
    DWORD currentTime = timeGetTime();
    DWORD nextTime = currentTime + timeInterver;
    while (1)
    {
        currentTime = timeGetTime();
        if (nextTime <= currentTime)
        {
            COORD coord = {0, 0};
            SetConsoleCursorPosition(hConsole, coord);
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

void ChattingServer::chatProc(Message &msg, __int16 wType)
{

    ePacketType packType = static_cast<ePacketType>(wType);
    switch (packType)
    {
    case ePacketType::CHAT_PLAYER_ALLOC:
        chatPlayerAlloc(msg);
        break;
    case ePacketType::CHAT_PLAYER_DELETE:
        chatPlayerDelete(msg);
        break;
    default:
        RT_ASSERT(FALSE);
    }
}

void ChattingServer::chatPlayerAlloc(Message &msg)
{
    Player &player = *MY_NEW Player();
    ull sessionID = msg.GetOwnerID();

    SOCKADDR_IN addr{0};
    msg.GetData(&addr, sizeof(addr));

    auto iter = mPlayerMap.find(sessionID);
    RT_ASSERT(iter == mPlayerMap.end());
    player.SessionID.Value = sessionID;
    player.Addr = addr;

    mPlayerMap.insert({sessionID, &player});
    MY_DELETE & msg;
    ++mUserCnt;
}

void ChattingServer::chatPlayerDelete(Message &msg)
{

    __int64 sessionID = msg.GetOwnerID();

    auto iter = mPlayerMap.find(sessionID);
    RT_ASSERT(iter != mPlayerMap.end());

    Player &player = *iter->second;
    if (player.bAuth)
    {
        __int8 x = player.sectorX;
        __int8 y = player.sectorY;
        auto iter2 = mSectors[x][y].mPlayers.find(sessionID);
        mSectors[x][y].mPlayers.erase(iter2);
    }
    mPlayerMap.erase(sessionID);
    MY_DELETE & msg;
    MY_DELETE & player;
    --mUserCnt;
}

namespace
{
constexpr WORD COLOR_WHITE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD COLOR_GREEN = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD COLOR_YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD COLOR_RED = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD COLOR_GRAY = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

void printRow(std::ostream &out, HANDLE hConsole, const char *label, __int64 value, WORD valueColor)
{
    SetConsoleTextAttribute(hConsole, COLOR_GRAY);
    out << "  " << std::left << std::setw(14) << label << ": ";
    SetConsoleTextAttribute(hConsole, valueColor);
    out << std::right << std::setw(8) << value << "\n";
}
} // namespace

std::ostream &operator<<(std::ostream &out, const ChattingServer &server)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    thread_local __int64 beforeAccept;
    thread_local __int64 beforeRecv;
    thread_local __int64 beforeSend;
    thread_local __int64 beforeContent;
    thread_local uint64_t beforePacketTypeTPS[static_cast<__int64>(ePacketType::CHAT_MAX)];

    __int64 currentAccept = server.GetAcceptCount();
    __int64 currentRecv = server.GetRecvCount();
    __int64 currentSend = server.GetSendCount();
    __int64 currentContent = server.mContentsTPS;

    uint64_t currentPacketTypeTPS[static_cast<__int64>(ePacketType::CHAT_MAX)];
    memcpy(currentPacketTypeTPS, server.mPacketTypeTPS, sizeof(server.mPacketTypeTPS));

    __int64 tpsAccept = currentAccept - beforeAccept;
    __int64 tpsRecv = currentRecv - beforeRecv;
    __int64 tpsSend = currentSend - beforeSend;
    __int64 tpsContent = currentContent - beforeContent;

    SetConsoleTextAttribute(hConsole, COLOR_WHITE);
    printRow(out, hConsole, "WokerThread Cnt : ", CONFIG_WORKER_THREAD_CNT, COLOR_WHITE);
    out << "=== ChattingServer Monitor ===\n\n";

    SetConsoleTextAttribute(hConsole, COLOR_WHITE);
    out << "[ TPS ]\n";
    printRow(out, hConsole, "Accept", tpsAccept, COLOR_GREEN);
    printRow(out, hConsole, "Recv", tpsRecv, COLOR_GREEN);
    printRow(out, hConsole, "Send", tpsSend, COLOR_GREEN);
    printRow(out, hConsole, "Contents", tpsContent, COLOR_GREEN);
    printRow(out, hConsole, "AccTotal", currentAccept, COLOR_GRAY);
    out << "\n";

    out << "[ Packet TPS ]\n";
    printRow(out, hConsole, "CS_LOGIN", currentPacketTypeTPS[0] - beforePacketTypeTPS[0], COLOR_GRAY);
    printRow(out, hConsole, "SC_LOGIN", currentPacketTypeTPS[1] - beforePacketTypeTPS[1], COLOR_GRAY);
    printRow(out, hConsole, "CS_AUTH", currentPacketTypeTPS[2] - beforePacketTypeTPS[2], COLOR_GRAY);
    printRow(out, hConsole, "SC_AUTH", currentPacketTypeTPS[3] - beforePacketTypeTPS[3], COLOR_GRAY);
    printRow(out, hConsole, "CS_MOVE", currentPacketTypeTPS[4] - beforePacketTypeTPS[4], COLOR_GRAY);
    printRow(out, hConsole, "CS_CHAT", currentPacketTypeTPS[5] - beforePacketTypeTPS[5], COLOR_GRAY);
    printRow(out, hConsole, "SC_CHAT", currentPacketTypeTPS[6] - beforePacketTypeTPS[6], COLOR_GRAY);
    out << "\n";

    SetConsoleTextAttribute(hConsole, COLOR_WHITE);
    out << "[ State ]\n";
    printRow(out, hConsole, "Sessions", server.GetSessionCount(), COLOR_WHITE);
    printRow(out, hConsole, "Users", server.mUserCnt, COLOR_WHITE);
    printRow(out, hConsole, "ContentsQ", server.mContentsQSize, server.mContentsQSize > 0 ? COLOR_RED : COLOR_GREEN);
    printRow(out, hConsole, "Disconnect", server.GetDisConnectCount(), COLOR_YELLOW);

    SetConsoleTextAttribute(hConsole, COLOR_GRAY);
    out << "\n";

    beforeAccept = currentAccept;
    beforeRecv = currentRecv;
    beforeSend = currentSend;
    beforeContent = currentContent;

    memcpy(beforePacketTypeTPS, currentPacketTypeTPS, sizeof(currentPacketTypeTPS));
    return out;
}

} // namespace network