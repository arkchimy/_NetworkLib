#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <atomic>
#include <conio.h>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

struct LoginInfo
{
    char    tokenKey[20];
    __int8  encKey;
    char    chatServerIP[16];
    __int16 chatServerPort;
};

class ChatDummyClient
{
  public:
    ChatDummyClient(int userCnt);
    ~ChatDummyClient();
    void Start(const char *loginIP, __int16 loginPort);

  private:
    void clientThread();
    void monitorThread();
    void inputThread();

    SOCKET connectToServer(const char *ip, __int16 port);
    bool   loginServerFlow(__int64 botIdx, LoginInfo &outInfo);
    bool   loginFlow(SOCKET sock, __int64 accountNo, __int32 &outSeqNum);
    bool   authFlow(SOCKET sock, __int32 &seqNum, __int8 &outX, __int8 &outY,
                    const wchar_t *nickname, const char *tokenKey);
    bool   mainLoop(SOCKET sock, __int32 &seqNum, __int8 &sectorX, __int8 &sectorY,
                    int &pendingChat, const wchar_t *myNickname);
    int    drainRecv(SOCKET sock, const wchar_t *myNickname);

  private:
    const char *mLoginServerIP;
    __int16     mLoginServerPort;
    int         mUserCnt;
    SSL_CTX    *mSslCtx;

    std::vector<std::thread> mThreadVec;
    std::thread              mMonitorThread;
    std::thread              mInputThread;

    std::atomic<__int64> mNextBotIdx{1};
    std::atomic<__int64> mLoginCnt{0};
    std::atomic<__int64> mMoveCnt{0};
    std::atomic<__int64> mChatCnt{0};
    std::atomic<__int64> mRecvChatCnt{0};
    std::atomic<__int64> mErrCnt{0};
    std::atomic<__int64> mConnectTotal{0};
    std::atomic<bool>    mPaused{false};
};

struct MyWsaData
{
    MyWsaData()
    {
        int retval = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (retval != 0)
            __debugbreak();
    }
    ~MyWsaData() { WSACleanup(); }
    WSADATA wsa;
};
