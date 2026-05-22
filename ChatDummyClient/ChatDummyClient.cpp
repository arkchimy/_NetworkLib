#include "ChatDummyClient.h"
#include "../ChattingServer/PacketCommon.h"
#include <iomanip>
#include <iostream>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

MyWsaData gWsa;

// ---- 패킷 구조체 (EnCoding/DeCoding 스텁이므로 암호화 불필요) ----
#pragma pack(push, 1)
struct PktHeader
{
    __int16 Len;
    __int8  RandKey;
};

struct PktCS_Login
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    __int64   accountNo;
};  // 17 bytes (hdr.Len = 14)

struct PktCS_Auth
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    wchar_t   nickname[20];
};  // 49 bytes (hdr.Len = 46)

struct PktCS_Move
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    __int8    sectorX;
    __int8    sectorY;
};  // 11 bytes (hdr.Len = 8)

struct PktCS_Chat
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    __int16   msgLen;
    wchar_t   msg[2];  // L"Hi"
};  // 15 bytes (hdr.Len = 12)

struct PktSC_Login
{
    PktHeader hdr;
    __int16   type;
    __int8    result;
    __int32   seqNum;
};  // 10 bytes

struct PktSC_Auth
{
    PktHeader hdr;
    __int16   type;
    __int8    sectorX;
    __int8    sectorY;
};  // 7 bytes
#pragma pack(pop)

static constexpr int MAIN_LOOP_CNT = 100;

static bool safeRecv(SOCKET sock, char *buf, int size)
{
    int total = 0;
    while (total < size)
    {
        int ret = recv(sock, buf + total, size - total, 0);
        if (ret <= 0) return false;
        total += ret;
    }
    return true;
}

static bool safeSend(SOCKET sock, const char *buf, int size)
{
    int total = 0;
    while (total < size)
    {
        int ret = send(sock, buf + total, size - total, 0);
        if (ret <= 0) return false;
        total += ret;
    }
    return true;
}

ChatDummyClient::ChatDummyClient(int userCnt)
    : mServerIP(nullptr), mServerPort(0), mUserCnt(userCnt)
{
}

void ChatDummyClient::Start(const char *ip, __int16 port)
{
    mServerIP   = ip;
    mServerPort = port;

    mMonitorThread = std::thread(&ChatDummyClient::monitorThread, this);
    mThreadVec.resize(mUserCnt);
    for (int i = 0; i < mUserCnt; ++i)
        mThreadVec[i] = std::thread(&ChatDummyClient::clientThread, this);

    for (auto &t : mThreadVec) t.join();
    mMonitorThread.join();
}

SOCKET ChatDummyClient::connectToServer()
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    SOCKADDR_IN addr{0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(mServerPort);
    inet_pton(AF_INET, mServerIP, &addr.sin_addr);

    if (connect(sock, (const sockaddr *)&addr, sizeof(addr)) != 0)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

// CS_LOGIN → SC_LOGIN. 성공 시 outSeqNum = 서버가 내려준 새 SeqNumber
bool ChatDummyClient::loginFlow(SOCKET sock, __int64 accountNo, __int32 &outSeqNum)
{
    PktCS_Login pkt{};
    pkt.hdr.Len    = static_cast<__int16>(sizeof(PktCS_Login) - sizeof(PktHeader));  // 14
    pkt.hdr.RandKey = '\xFF';
    pkt.type       = static_cast<__int16>(ePacketType::CS_LOGIN);
    pkt.seqNum     = (__int32)0xfdfdfdfd;
    pkt.accountNo  = accountNo;

    if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) 
        return false;

    PktSC_Login resp{};
    if (!safeRecv(sock, (char *)&resp, sizeof(resp))) 
        return false;

    if (resp.type != static_cast<__int16>(ePacketType::SC_LOGIN)) 
        return false;
    if (resp.result == 0) 
        return false;  // Redis_No_Data

    outSeqNum = resp.seqNum;
    mLoginCnt.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// CS_AUTH → SC_AUTH. 반환 시 seqNum은 다음 패킷에 쓸 값으로 1 증가됨
bool ChatDummyClient::authFlow(SOCKET sock, __int32 &seqNum, __int8 &outX, __int8 &outY)
{
    PktCS_Auth pkt{};
    pkt.hdr.Len    = static_cast<__int16>(sizeof(PktCS_Auth) - sizeof(PktHeader));  // 46
    pkt.hdr.RandKey = '\xFF';
    pkt.type       = static_cast<__int16>(ePacketType::CS_AUTH);
    pkt.seqNum     = seqNum;
    wcscpy_s(pkt.nickname, 20, L"Bot");

    if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) 
        return false;
    seqNum++;

    PktSC_Auth resp{};
    if (!safeRecv(sock, (char *)&resp, sizeof(resp))) 
        return false;

    if (resp.type != static_cast<__int16>(ePacketType::SC_AUTH)) 
        return false;

    outX = resp.sectorX;
    outY = resp.sectorY;
    return true;
}

bool ChatDummyClient::mainLoop(SOCKET sock, __int32 &seqNum, __int8 &sectorX, __int8 &sectorY)
{
    for (int i = 0; i < MAIN_LOOP_CNT; ++i)
    {
        // CS_MOVE: 현재와 다른 임의 섹터로 이동
        {
            __int8 newX, newY;
            do
            {
                newX = (__int8)(rand() % 50);
                newY = (__int8)(rand() % 50);
            } while (newX == sectorX && newY == sectorY);

            PktCS_Move pkt{};
            pkt.hdr.Len    = static_cast<__int16>(sizeof(PktCS_Move) - sizeof(PktHeader));  // 8
            pkt.hdr.RandKey = '\xFF';
            pkt.type       = static_cast<__int16>(ePacketType::CS_MOVE);
            pkt.seqNum     = seqNum;
            pkt.sectorX    = newX;
            pkt.sectorY    = newY;

            if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;
            seqNum++;
            sectorX = newX;
            sectorY = newY;
            mMoveCnt.fetch_add(1, std::memory_order_relaxed);
        }

        // CS_CHAT
        {
            PktCS_Chat pkt{};
            pkt.hdr.Len    = static_cast<__int16>(sizeof(PktCS_Chat) - sizeof(PktHeader));  // 12
            pkt.hdr.RandKey = '\xFF';
            pkt.type       = static_cast<__int16>(ePacketType::CS_CHAT);
            pkt.seqNum     = seqNum;
            pkt.msgLen     = 2;
            pkt.msg[0]     = L'H';
            pkt.msg[1]     = L'i';

            if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;
            seqNum++;
            mChatCnt.fetch_add(1, std::memory_order_relaxed);
        }

        // 쌓인 SC_CHAT 소비 (블로킹 없이)
        drainRecv(sock);
    }
    return true;
}

void ChatDummyClient::drainRecv(SOCKET sock)
{
    u_long avail = 0;
    char   buf[2048];
    while (ioctlsocket(sock, FIONREAD, &avail) == 0 && avail > 0)
    {
        int toRead = (avail > sizeof(buf)) ? (int)sizeof(buf) : (int)avail;
        if (recv(sock, buf, toRead, 0) <= 0) break;
    }
}

void ChatDummyClient::clientThread()
{
    __int64 accountNo = mNextAccountNo.fetch_add(1, std::memory_order_relaxed);

    while (true)
    {
        SOCKET sock = connectToServer();
        if (sock == INVALID_SOCKET) { Sleep(100); continue; }

        mReconnectCnt.fetch_add(1, std::memory_order_relaxed);

        __int32 seqNum  = 0;
        __int8  sectorX = 0, sectorY = 0;

        if (!loginFlow(sock, accountNo, seqNum) ||
            !authFlow(sock, seqNum, sectorX, sectorY))
        {
            mErrCnt.fetch_add(1, std::memory_order_relaxed);
            closesocket(sock);
            Sleep(100);
            continue;
        }

        if (!mainLoop(sock, seqNum, sectorX, sectorY))
            mErrCnt.fetch_add(1, std::memory_order_relaxed);

        closesocket(sock);
    }
}

void ChatDummyClient::monitorThread()
{
    timeBeginPeriod(1);
    constexpr int interval = 1000;
    DWORD nextTime = timeGetTime() + interval;

    __int64 beforeLogin = 0;
    __int64 beforeMove  = 0;
    __int64 beforeChat  = 0;

    while (true)
    {
        DWORD now = timeGetTime();
        if (nextTime <= now)
        {
            __int64 curLogin = mLoginCnt.load();
            __int64 curMove  = mMoveCnt.load();
            __int64 curChat  = mChatCnt.load();

            std::cout
                << std::setw(18) << "LoginTPS  : " << std::setw(8) << curLogin - beforeLogin << "\n"
                << std::setw(18) << "MoveTPS   : " << std::setw(8) << curMove  - beforeMove  << "\n"
                << std::setw(18) << "ChatTPS   : " << std::setw(8) << curChat  - beforeChat  << "\n"
                << std::setw(18) << "Reconnect : " << std::setw(8) << mReconnectCnt.load()   << "\n"
                << std::setw(18) << "ErrCnt    : " << std::setw(8) << mErrCnt.load()         << "\n"
                << "--------------------------------\n";

            beforeLogin = curLogin;
            beforeMove  = curMove;
            beforeChat  = curChat;
            nextTime += interval;
        }
        else
        {
            Sleep(nextTime - now);
        }
    }
    timeEndPeriod(1);
}
