#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <atomic>
#include <conio.h>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

class ChatDummyClient
{
  public:
    ChatDummyClient(int userCnt);
    void Start(const char *ip, __int16 port);

  private:
    void clientThread();
    void monitorThread();
    void inputThread();

    SOCKET connectToServer();
    bool loginFlow(SOCKET sock, __int64 accountNo, __int32 &outSeqNum);
    bool authFlow(SOCKET sock, __int32 &seqNum, __int8 &outX, __int8 &outY);
    bool mainLoop(SOCKET sock, __int32 &seqNum, __int8 &sectorX, __int8 &sectorY);
    void drainRecv(SOCKET sock);

  private:
    const char *mServerIP;
    __int16     mServerPort;
    int         mUserCnt;

    std::vector<std::thread> mThreadVec;
    std::thread              mMonitorThread;
    std::thread              mInputThread;

    std::atomic<__int64> mNextAccountNo{1};
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
        {
            __debugbreak();
        }
    }
    ~MyWsaData() { WSACleanup(); }
    WSADATA wsa;
};
