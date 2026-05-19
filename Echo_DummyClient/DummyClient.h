#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "ClientConfig.h"
#include "Common.h"

#pragma comment(lib, "ws2_32.lib")

class DummyClient
{
  public:
    DummyClient(int userCnt);
    void Start(const char *addr, __int16 port);

  private:
    void clientThread();
    void monitorThread();

    SOCKET connectToServer();
    bool echoLoop(SOCKET sock);   // false = 서버가 강제로 끊음
    void attackLoop(SOCKET sock); // 잘못된 패킷 전송 후 서버 반응 확인

  private:
    std::vector<std::thread> mThreadVec;
    std::thread mMonitorThread;
    std::vector<std::string> mStringVec;

    const char *mServerIP;
    __int16 mServerPort;
    int mUserCnt;

    std::atomic<__int64> mSendCnt{0};
    std::atomic<__int64> mRecvCnt{0};
    std::atomic<__int64> mReconnectCnt{0};
    std::atomic<__int64> mForcedDisconnectCnt{0}; // 정상 에코 중 서버가 끊음
    std::atomic<__int64> mAttackCnt{0};           // 공격 패킷 전송 횟수
    std::atomic<__int64> mErrCnt{0};              // 검증 실패 횟수
};

struct MyWsaData
{
    MyWsaData() 
    {
        //성공하면 WSAStartup 함수는 0을 반환합니다. 
        RT_ASSERT(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~MyWsaData() { WSACleanup(); }
    WSADATA wsa;
};