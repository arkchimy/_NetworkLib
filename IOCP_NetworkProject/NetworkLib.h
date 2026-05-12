#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef RT_ASSERT
#define RT_ASSERT(x) \
    if (!(x))        \
        __debugbreak();
#endif

#include "Session.h"
#include <Mswsock.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <mutex>
#include <stack>
#include <thread>

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "Mswsock.lib")

namespace network
{
    enum eNetConfig
    {
        CONFIG_WORKER_THREAD_CNT = 5,
        CONFIG_RINGBUFFER_SIZE = 2048,
        CONFIG_SESSION_MAX = 7000,
        
        CONFIG_SERVER_PORT = 32000,
        CONFIG_SERVER_ADDR = 0,

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
    class NetworkLib
    {
      public:
        NetworkLib();
        virtual ~NetworkLib() = default;

      protected:
        virtual void onAccept(const SeqAndIdx &sessionID) = 0;
        virtual void onRecv() = 0;
        virtual void onSend() = 0;
        virtual void onRelease() = 0;

      private:
        void workerThread();

        bool stackSessionIdx_Pop(ull &out);
        void stackSessionIdx_Push(const ull &input);

        void errorChkRegister(Session &session, const int lastError);

        void registerAcceptEx();
        void registerRecv(Session &session);

        void completeAcceptEx(Session& session);

      private:
        std::thread mWorkerThreads[CONFIG_WORKER_THREAD_CNT];
        SOCKET mListenSock;

        HANDLE mHcp; // iocpHandle
        Session mSessions[CONFIG_SESSION_MAX];

        std::stack<seqAddrType> mStackSessionIdx;
        std::mutex mStackMutex;

        seqAddrType mSeqID = -1;

        WsadataRAII wsadata;
    };


    } // namespace network
