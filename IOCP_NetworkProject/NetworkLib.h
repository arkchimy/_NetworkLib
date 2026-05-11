#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef RT_ASSERT
#define RT_ASSERT(x) if (!(x)) __debugbreak();
#endif


#include <WS2tcpip.h>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <stack>
#include <Mswsock.h>
#include "Session.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "Mswsock.lib")


namespace network
{
    enum config
    {
        WORKER_THREAD_CNT = 5,
        SESSION_MAX_CNT = 7000,
    };
    class NetworkLib
    {
      public:
        NetworkLib();
        virtual ~NetworkLib() = default;

      protected:
        virtual void onAccept(const SeqAndIdx& sessionID) = 0;
        virtual void onRecv() = 0;
        virtual void onSend() = 0;
        virtual void onRelease() = 0;

      private:
        void workerThread();

        bool stackSessionIdx_Pop(sessionStackDataType& out);
        void stackSessionIdx_Push(const sessionStackDataType& input);

        void registerAcceptEx();

        void completeAcceptEx(const MyOverlapped& ov);
        
      private:
        std::thread mWorkerThreads[WORKER_THREAD_CNT];
        SOCKET mListenSock;

        HANDLE mHcp; // iocpHandle
        Session mSessions[SESSION_MAX_CNT];

        std::stack<sessionStackDataType> mStackSessionIdx;
        std::mutex mStackMutex;

        volatile ull mSeqID = -1;
    };
} // namespace network
