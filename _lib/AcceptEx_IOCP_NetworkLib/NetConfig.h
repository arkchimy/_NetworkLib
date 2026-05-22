#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#include <Windows.h>
#include <WS2tcpip.h>
#include <Mswsock.h>

#include "Common.h"
#include "Header.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "Mswsock.lib")

namespace network
{
enum eNetConfig
{
    CONFIG_CONCURRENT_THREAD_CNT = 0,
    CONFIG_WORKER_THREAD_CNT = 10,

    CONFIG_RINGBUFFER_SIZE = 2001,
    CONFIG_SESSION_MAX = 7000,

    CONFIG_SERVER_PORT = 32000,
    CONFIG_SERVER_ADDR = 0,
    CONFIG_SEND_MESSAGE_MAXCOUNT = 500,

    CONFIG_ZERO_COPY = 0,
};
struct WsadataRAII
{
    WsadataRAII()
    {
        // WSAStartup 함수는 성공하면 0을 반환합니다.
        if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
        {
            RT_ASSERT(false);
        }
    }
    ~WsadataRAII()
    {
        WSACleanup();
    }
    WSADATA wsadata;
};

}