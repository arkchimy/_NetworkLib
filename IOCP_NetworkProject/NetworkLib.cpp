#include "NetworkLib.h"

network::NetworkLib::NetworkLib()
{
    mListenSock = socket(AF_INET, SOCK_STREAM, 0);
    RT_ASSERT(mListenSock != INVALID_SOCKET);

    SOCKADDR_IN addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7800);
    addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

    int retval = bind(mListenSock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    RT_ASSERT(retval != SOCKET_ERROR);

    retval = listen(mListenSock, SOMAXCONN_HINT(65535));
    RT_ASSERT(retval != SOCKET_ERROR);

    mHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
    {
        mWorkerThreads[i] = std::thread(&NetworkLib::WorkerThread, this);
    }

}

void network::NetworkLib::WorkerThread()
{
    // 벤치마크
    // GetQueuedCompletionStatusEx 한번에 뽑는다? 

    {
        DWORD trandfferd;
        ULONG key;
        //GetQueuedCompletionStatus(mHcp, );
    }
}
