#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <thread>
#include <vector>
#include <string>

#include "Common.h"
#include "ClientConfig.h"


#pragma comment(lib, "ws2_32.lib")

class DummyClient
{
  public:
    DummyClient(__int8 userCnt);
    void Start(const char* addr,__int16 port);

  private:
    void clientThread();
  private:
    std::vector<std::thread> mThread_vec;
    std::vector<std::string> mString_vec;

    const char *mServerIP;
    __int16 mServerPort;
    __int8 mUserCnt;
};



struct MyWsaData
{
    MyWsaData()
    {
        int err;
        err = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (err != 0)
        {
            /* Tell the user that we could not find a usable */
            /* WinSock DLL.                                  */
            return;
        }
    }
    ~MyWsaData()
    {
        WSACleanup();
    }
    WSADATA wsa;
};