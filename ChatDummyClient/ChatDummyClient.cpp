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
    mInputThread   = std::thread(&ChatDummyClient::inputThread, this);
    mThreadVec.resize(mUserCnt);
    for (int i = 0; i < mUserCnt; ++i)
        mThreadVec[i] = std::thread(&ChatDummyClient::clientThread, this);

    for (auto &t : mThreadVec) t.join();
    mMonitorThread.join();
    mInputThread.join();
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

bool ChatDummyClient::mainLoop(SOCKET sock, __int32 &seqNum, __int8 &sectorX, __int8 &sectorY, int &pendingChat)
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
            ++pendingChat;
        }

        pendingChat -= drainRecv(sock);
    }
    return true;
}

int ChatDummyClient::drainRecv(SOCKET sock)
{
    int received = 0;
    u_long avail = 0;
    while (ioctlsocket(sock, FIONREAD, &avail) == 0 && avail >= sizeof(PktHeader))
    {
        PktHeader hdr;
        if (!safeRecv(sock, (char *)&hdr, sizeof(hdr))) break;

        if (hdr.Len <= 0 || hdr.Len > 2048) break;

        char payload[2048];
        if (!safeRecv(sock, payload, hdr.Len)) break;

        __int16 type;
        memcpy(&type, payload, sizeof(type));
        if (static_cast<ePacketType>(type) == ePacketType::SC_CHAT)
        {
            mRecvChatCnt.fetch_add(1, std::memory_order_relaxed);
            ++received;
        }
    }
    return received;
}

void ChatDummyClient::clientThread()
{
    __int64 accountNo = mNextAccountNo.fetch_add(1, std::memory_order_relaxed);

    while (true)
    {
        while (mPaused.load(std::memory_order_relaxed))
            Sleep(100);

        SOCKET sock = connectToServer();
        if (sock == INVALID_SOCKET) { Sleep(100); continue; }

        mConnectTotal.fetch_add(1, std::memory_order_relaxed);

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

        int pendingChat = 0;
        if (!mainLoop(sock, seqNum, sectorX, sectorY, pendingChat))
            mErrCnt.fetch_add(1, std::memory_order_relaxed);

        while (pendingChat > 0)
        {
            int got = drainRecv(sock);
            if (got > 0)
                pendingChat -= got;
            else
                Sleep(1);
        }

        closesocket(sock);
    }
}

void ChatDummyClient::inputThread()
{
    while (true)
    {
        if (_kbhit())
        {
            char c = _getch();
            if (c == 's' || c == 'S')
                mPaused.store(!mPaused.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        Sleep(10);
    }
}

void ChatDummyClient::monitorThread()
{
    timeBeginPeriod(1);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    constexpr WORD COLOR_WHITE  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    constexpr WORD COLOR_GREEN  = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    constexpr WORD COLOR_YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    constexpr WORD COLOR_RED    = FOREGROUND_RED | FOREGROUND_INTENSITY;
    constexpr WORD COLOR_GRAY   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    constexpr int interval = 1000;
    DWORD nextTime = timeGetTime() + interval;

    __int64 beforeLogin    = 0;
    __int64 beforeMove     = 0;
    __int64 beforeChat     = 0;
    __int64 beforeRecvChat = 0;

    while (true)
    {
        DWORD now = timeGetTime();
        if (nextTime <= now)
        {
            __int64 curLogin    = mLoginCnt.load();
            __int64 curMove     = mMoveCnt.load();
            __int64 curChat     = mChatCnt.load();
            __int64 curRecvChat = mRecvChatCnt.load();
            __int64 unreceived  = curChat - curRecvChat;

            COORD coord = {0, 0};
            SetConsoleCursorPosition(hConsole, coord);

            bool paused = mPaused.load(std::memory_order_relaxed);

            SetConsoleTextAttribute(hConsole, COLOR_WHITE);
            std::cout << "=== ChatDummy Monitor [";
            SetConsoleTextAttribute(hConsole, paused ? COLOR_RED : COLOR_GREEN);
            std::cout << (paused ? "PAUSED " : "RUNNING");
            SetConsoleTextAttribute(hConsole, COLOR_WHITE);
            std::cout << "] (s: pause/resume) ===\n\n";

            auto printRow = [&](const char *label, __int64 value, WORD valueColor)
            {
                SetConsoleTextAttribute(hConsole, COLOR_GRAY);
                std::cout << "  " << std::left << std::setw(14) << label << ": ";
                SetConsoleTextAttribute(hConsole, valueColor);
                std::cout << std::right << std::setw(8) << value << "\n";
            };

            SetConsoleTextAttribute(hConsole, COLOR_WHITE);
            std::cout << "[ TPS ]\n";
            printRow("Login",    curLogin    - beforeLogin,    COLOR_GREEN);
            printRow("Move",     curMove     - beforeMove,     COLOR_GREEN);
            printRow("ChatSent", curChat     - beforeChat,     COLOR_GREEN);
            printRow("ChatRecv", curRecvChat - beforeRecvChat, COLOR_GREEN);

            std::cout << "\n";

            SetConsoleTextAttribute(hConsole, COLOR_WHITE);
            std::cout << "[ State ]\n";
            printRow("ConnTotal",  mConnectTotal.load(), COLOR_WHITE);
            printRow("Unreceived", unreceived,           unreceived > 0 ? COLOR_YELLOW : COLOR_GREEN);
            printRow("ErrCnt",     mErrCnt.load(),       mErrCnt.load() > 0 ? COLOR_RED : COLOR_GREEN);

            SetConsoleTextAttribute(hConsole, COLOR_GRAY);
            std::cout << "\n";

            beforeLogin    = curLogin;
            beforeMove     = curMove;
            beforeChat     = curChat;
            beforeRecvChat = curRecvChat;
            nextTime += interval;
        }
        else
        {
            Sleep(nextTime - now);
        }
    }
    timeEndPeriod(1);
}
