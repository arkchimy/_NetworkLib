#include "ChatDummyClient.h"
#include "../ChattingServer/PacketCommon.h"
#include <iomanip>
#include <iostream>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

MyWsaData gWsa;

// ---- ChattingServer 패킷 ----
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
};

struct PktCS_Auth
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    wchar_t   nickname[20];
    char      tokenKey[20];
};

struct PktCS_Move
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    __int8    sectorX;
    __int8    sectorY;
};

struct PktCS_Chat
{
    PktHeader hdr;
    __int16   type;
    __int32   seqNum;
    __int16   msgLen;
    wchar_t   msg[2];
};

struct PktSC_Login
{
    PktHeader hdr;
    __int16   type;
    __int8    result;
    __int32   seqNum;
};

struct PktSC_Auth
{
    PktHeader hdr;
    __int16   type;
    __int8    sectorX;
    __int8    sectorY;
};

// ---- LoginServer 패킷 ----
struct LsHeader
{
    BYTE   byCode;
    USHORT sDataLen;
    BYTE   byRandKey;
    BYTE   byCheckSum;
};

struct PktLS_ReqAuth
{
    LsHeader hdr;
    WORD  type;
    WCHAR id[20];
    WCHAR pw[20];
};

struct PktLS_ResAuth
{
    LsHeader hdr;
    WORD   type;
    BYTE   status;
    char   tokenKey[20];
    BYTE   status2;     // Proxy.cpp의 Status 중복 기재
    BYTE   encKey;
    WCHAR  gameServerIP[16];
    USHORT gameServerPort;
    WCHAR  chatServerIP[16];
    USHORT chatServerPort;
};
#pragma pack(pop)

static constexpr int  MAIN_LOOP_CNT = 1000000;
static constexpr WORD LS_REQ_AUTH   = 101;
static constexpr WORD LS_RES_AUTH   = 102;
static constexpr BYTE LS_RESULT_OK  = 1;

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

static bool safeSSLRecv(SSL *ssl, char *buf, int size)
{
    int total = 0;
    while (total < size)
    {
        int n = SSL_read(ssl, buf + total, size - total);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

ChatDummyClient::ChatDummyClient(int userCnt)
    : mLoginServerIP(nullptr), mLoginServerPort(0), mUserCnt(userCnt), mSslCtx(nullptr)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    mSslCtx = SSL_CTX_new(TLS_client_method());
    // 테스트용이므로 서버 인증서 검증 생략
    SSL_CTX_set_verify(mSslCtx, SSL_VERIFY_NONE, nullptr);
}

ChatDummyClient::~ChatDummyClient()
{
    if (mSslCtx)
        SSL_CTX_free(mSslCtx);
}

void ChatDummyClient::Start(const char *loginIP, __int16 loginPort)
{
    mLoginServerIP   = loginIP;
    mLoginServerPort = loginPort;

    mMonitorThread = std::thread(&ChatDummyClient::monitorThread, this);
    mInputThread   = std::thread(&ChatDummyClient::inputThread, this);
    mThreadVec.resize(mUserCnt);
    for (int i = 0; i < mUserCnt; ++i)
        mThreadVec[i] = std::thread(&ChatDummyClient::clientThread, this);

    for (auto &t : mThreadVec) t.join();
    mMonitorThread.join();
    mInputThread.join();
}

SOCKET ChatDummyClient::connectToServer(const char *ip, __int16 port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    linger lingerData{1,0};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&lingerData, sizeof(lingerData));

    SOCKADDR_IN addr{0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (const sockaddr *)&addr, sizeof(addr)) != 0)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

// LoginServer에 SSL로 로그인 → tokenKey/encKey/ChatServer 주소 획득
// accountNo는 Proxy.cpp 버그로 응답에 없음 → botIdx를 accountNo로 사용
// (MySQL 계정 bot1, bot2, ... 를 순서대로 생성해두면 accountNo가 일치)
bool ChatDummyClient::loginServerFlow(__int64 botIdx, LoginInfo &outInfo)
{
    SOCKET sock = connectToServer(mLoginServerIP, mLoginServerPort);
    if (sock == INVALID_SOCKET) return false;
    linger lingerData{1, 0};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&lingerData, sizeof(lingerData));
    SSL *ssl = SSL_new(mSslCtx);
    if (!ssl) { closesocket(sock); return false; }

    SSL_set_fd(ssl, static_cast<int>(sock));
    if (SSL_connect(ssl) != 1)
    {
        SSL_free(ssl);
        closesocket(sock);
        return false;
    }

    // CS_LOGIN_REQ_AUTH 전송
    PktLS_ReqAuth req{};
    req.hdr.byCode   = 0x77;
    req.hdr.sDataLen = static_cast<USHORT>(sizeof(PktLS_ReqAuth) - sizeof(LsHeader));
    req.type         = LS_REQ_AUTH;
    swprintf_s(req.id, 20, L"bot%lld", botIdx);
    wcscpy_s(req.pw, 20, L"1234");

    bool ok = (SSL_write(ssl, reinterpret_cast<char *>(&req), sizeof(req)) > 0);

    if (ok)
    {
        // 헤더 수신
        LsHeader resHdr{};
        ok = safeSSLRecv(ssl, reinterpret_cast<char *>(&resHdr), sizeof(LsHeader));

        if (ok && resHdr.sDataLen <= sizeof(PktLS_ResAuth) - sizeof(LsHeader))
        {
            // 페이로드 수신 후 파싱
            char payload[sizeof(PktLS_ResAuth) - sizeof(LsHeader)]{};
            ok = safeSSLRecv(ssl, payload, resHdr.sDataLen);
            if (ok)
            {
                int    off = 0;
                WORD   resType;
                BYTE   status;
                memcpy(&resType, payload + off, sizeof(WORD)); off += sizeof(WORD);
                memcpy(&status,  payload + off, sizeof(BYTE)); off += sizeof(BYTE);

                if (resType == LS_RES_AUTH && status == LS_RESULT_OK)
                {
                    memcpy(outInfo.tokenKey, payload + off, 20); off += 20;
                    off += sizeof(BYTE);  // status2 (중복)
                    memcpy(&outInfo.encKey, payload + off, sizeof(BYTE)); off += sizeof(BYTE);

                    WCHAR  gameIP[16]{};
                    USHORT gamePort;
                    memcpy(gameIP,   payload + off, 32);              off += 32;
                    memcpy(&gamePort, payload + off, sizeof(USHORT)); off += sizeof(USHORT);

                    WCHAR  chatIP[16]{};
                    USHORT chatPort;
                    memcpy(chatIP,   payload + off, 32);              off += 32;
                    memcpy(&chatPort, payload + off, sizeof(USHORT)); off += sizeof(USHORT);

                    size_t conv;
                    wcstombs_s(&conv, outInfo.chatServerIP, 16, chatIP, _TRUNCATE);
                    outInfo.chatServerPort = static_cast<__int16>(chatPort);
                }
                else
                    ok = false;
            }
        }
        else
            ok = false;
    }

    SSL_free(ssl);
    closesocket(sock);
    return ok;
}

bool ChatDummyClient::loginFlow(SOCKET sock, __int64 accountNo, __int32 &outSeqNum)
{
    PktCS_Login pkt{};
    pkt.hdr.Len     = static_cast<__int16>(sizeof(PktCS_Login) - sizeof(PktHeader));
    pkt.hdr.RandKey = '\xFF';
    pkt.type        = static_cast<__int16>(ePacketType::CS_LOGIN);
    pkt.seqNum      = (__int32)0xfdfdfdfd;
    pkt.accountNo   = accountNo;

    if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;

    PktSC_Login resp{};
    if (!safeRecv(sock, (char *)&resp, sizeof(resp))) return false;
    if (resp.type != static_cast<__int16>(ePacketType::SC_LOGIN)) return false;
    if (resp.result == 0) return false;

    outSeqNum = resp.seqNum;
    mLoginCnt.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ChatDummyClient::authFlow(SOCKET sock, __int32 &seqNum, __int8 &outX, __int8 &outY,
                               const wchar_t *nickname, const char *tokenKey)
{
    PktCS_Auth pkt{};
    pkt.hdr.Len     = static_cast<__int16>(sizeof(PktCS_Auth) - sizeof(PktHeader));
    pkt.hdr.RandKey = '\xFF';
    pkt.type        = static_cast<__int16>(ePacketType::CS_AUTH);
    pkt.seqNum      = seqNum;
    wcscpy_s(pkt.nickname, 20, nickname);
    memcpy(pkt.tokenKey, tokenKey, 20);

    if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;
    seqNum++;

    PktSC_Auth resp{};
    if (!safeRecv(sock, (char *)&resp, sizeof(resp))) return false;
    if (resp.type != static_cast<__int16>(ePacketType::SC_AUTH)) return false;

    outX = resp.sectorX;
    outY = resp.sectorY;
    return true;
}

bool ChatDummyClient::mainLoop(SOCKET sock, __int32 &seqNum, __int8 &sectorX, __int8 &sectorY,
                               int &pendingChat, const wchar_t *myNickname)
{
    static constexpr int BATCH = 100;
    int totalLoops = rand() % MAIN_LOOP_CNT;

    for (int i = 0; i < totalLoops; )
    {
        int batchSize = min(BATCH, totalLoops - i);
        int pending   = 0;

        // ── 1단계: BATCH개 송신 ──────────────────────────────
        for (int j = 0; j < batchSize; ++j)
        {
            // CS_MOVE
            {
                __int8 newX, newY;
                do
                {
                    newX = (__int8)(rand() % 50);
                    newY = (__int8)(rand() % 50);
                } while (newX == sectorX && newY == sectorY);

                PktCS_Move pkt{};
                pkt.hdr.Len     = static_cast<__int16>(sizeof(PktCS_Move) - sizeof(PktHeader));
                pkt.hdr.RandKey = '\xFF';
                pkt.type        = static_cast<__int16>(ePacketType::CS_MOVE);
                pkt.seqNum      = seqNum;
                pkt.sectorX     = newX;
                pkt.sectorY     = newY;

                if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;
                seqNum++;
                sectorX = newX;
                sectorY = newY;
                mMoveCnt.fetch_add(1, std::memory_order_relaxed);
            }

            // CS_CHAT
            {
                PktCS_Chat pkt{};
                pkt.hdr.Len     = static_cast<__int16>(sizeof(PktCS_Chat) - sizeof(PktHeader));
                pkt.hdr.RandKey = '\xFF';
                pkt.type        = static_cast<__int16>(ePacketType::CS_CHAT);
                pkt.seqNum      = seqNum;
                pkt.msgLen      = 2;
                pkt.msg[0]      = L'H';
                pkt.msg[1]      = L'i';

                if (!safeSend(sock, (char *)&pkt, sizeof(pkt))) return false;
                seqNum++;
                mChatCnt.fetch_add(1, std::memory_order_relaxed);
                ++pending;
            }
        }

        // ── 2단계: BATCH개 전부 수신될 때까지 블로킹 대기 ─────
        while (pending > 0)
        {
            PktHeader hdr;
            if (!safeRecv(sock, (char *)&hdr, sizeof(hdr))) return false;
            if (hdr.Len <= 0 || hdr.Len > 2048)             return false;

            char payload[2048]{};
            if (!safeRecv(sock, payload, hdr.Len))           return false;

            __int16 type;
            memcpy(&type, payload, sizeof(type));

            if (static_cast<ePacketType>(type) == ePacketType::SC_CHAT)
            {
                wchar_t rcvNick[20]{};
                memcpy(rcvNick, payload + sizeof(__int16), sizeof(wchar_t) * 20);
                if (wcscmp(rcvNick, myNickname) == 0)
                {
                    mRecvChatCnt.fetch_add(1, std::memory_order_relaxed);
                    --pending;
                }
                // 다른 유저 채팅이 끼어들면 읽고 버림
            }
        }

        i += batchSize;
    }
    return true;
}

int ChatDummyClient::drainRecv(SOCKET sock, const wchar_t *myNickname)
{
    int    received = 0;
    u_long avail    = 0;
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
            wchar_t rcvNick[20] = {};
            memcpy(rcvNick, payload + sizeof(__int16), sizeof(wchar_t) * 20);
            if (wcscmp(rcvNick, myNickname) == 0)
            {
                mRecvChatCnt.fetch_add(1, std::memory_order_relaxed);
                ++received;
            }
        }
    }
    return received;
}

void ChatDummyClient::clientThread()
{
    __int64 botIdx = mNextBotIdx.fetch_add(1, std::memory_order_relaxed);

    wchar_t myNickname[20] = {};
    swprintf_s(myNickname, 20, L"Bot%lld", botIdx);

    while (true)
    {
        while (mPaused.load(std::memory_order_relaxed))
            Sleep(100);

        // LoginServer 인증
        LoginInfo info{};
        if (!loginServerFlow(botIdx, info))
        {
            mErrCnt.fetch_add(1, std::memory_order_relaxed);
            Sleep(1000);
            continue;
        }

        // ChatServer 연결
        SOCKET sock = connectToServer(info.chatServerIP, info.chatServerPort);
        if (sock == INVALID_SOCKET) { Sleep(100); continue; }

        mConnectTotal.fetch_add(1, std::memory_order_relaxed);

        __int32 seqNum  = 0;
        __int8  sectorX = 0, sectorY = 0;

        // botIdx를 accountNo로 사용 (MySQL 계정 bot1, bot2, ... 순서 필요)
        if (!loginFlow(sock, botIdx, seqNum) ||
            !authFlow(sock, seqNum, sectorX, sectorY, myNickname, info.tokenKey))
        {
            mErrCnt.fetch_add(1, std::memory_order_relaxed);
            closesocket(sock);
            Sleep(100);
            continue;
        }

        int pendingChat = 0;
        if (!mainLoop(sock, seqNum, sectorX, sectorY, pendingChat, myNickname))
            mErrCnt.fetch_add(1, std::memory_order_relaxed);

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
