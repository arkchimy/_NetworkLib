#pragma once

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#ifndef RT_ASSERT
#define RT_ASSERT(x) \
    if (!(x))        \
    __debugbreak();
#endif

#include <Windows.h>
#include <WS2tcpip.h>
#include <thread>

#pragma comment(lib, "WS2_32.lib")


namespace network
{
	enum eConfig :unsigned int
	{
		WORKER_THREAD_CNT = 5,
	};
	class NetworkLib
	{

	private:
		NetworkLib();
		void WorkerThread();

		std::thread mWorkerThreads[WORKER_THREAD_CNT];
        SOCKET mListenSock;
		// iocpHandle
        HANDLE mHcp;
	};
}

