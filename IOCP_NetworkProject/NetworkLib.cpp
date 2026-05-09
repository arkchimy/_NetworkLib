#include "NetworkLib.h"

network::NetworkLib::NetworkLib()
{

    for (short i = 0; i < SESSION_MAX_CNT; i++)
    {
        stackSessionIdx_Push(i);
    }

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
    CreateIoCompletionPort((HANDLE)mListenSock,mHcp, 0, 0);
    short idx;
    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
    {
        mWorkerThreads[i] = std::thread(&NetworkLib::workerThread, this);
        RT_ASSERT(stackSessionIdx_Pop(idx) != false);

        Session &session = mSessions[idx]; 
        session.mSock = socket(AF_INET, SOCK_STREAM, 0);

        /*
          AcceptEx 매개변수

          AcceptEx(
              mListenSock,       // 리슨 소켓
              acceptSocket,      // 미리 만들어둔 빈 소켓 (socket()으로 생성해야 함)
              buffer,            // 4번쨰 인자를 0으로 두면 주소 정보 저장할 버퍼
              0,                 // 첫 데이터 받을 크기 → 0 권장 (데이터 없어도 즉시 완료)
              sizeof(SOCKADDR_IN) + 16,   // 로컬 주소 공간 + 여유공간
              sizeof(SOCKADDR_IN) + 16,   // 원격 주소 공간 + 여유공간
              &bytesReceived,    // 받은 바이트 수
              &mAcceptOv         // overlapped
          );
        */
        {
            DWORD recvByte;
            AcceptEx(mListenSock, session.mSock, session.mAcceptBuf, 0,
                     sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &recvByte, &session.mAcceptOv); // 주석
        }
    }

}

void network::NetworkLib::workerThread()
{
    // 벤치마크
    // GetQueuedCompletionStatusEx 한번에 뽑는다? 
    while (true)
    {
        DWORD trandfferd = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *overlapped = nullptr;
        

        GetQueuedCompletionStatus(mHcp, &trandfferd, &key, &overlapped, INFINITE);
        {
            // 종료 메세지
            if (overlapped == nullptr && key == 0)
            {
                break;
            }
            MyOverlapped *ov = static_cast<MyOverlapped *>(overlapped);
            //TODO : key가 nullptr 일 경우가 존재함.
            Session& session = *reinterpret_cast<Session *>(key);
            switch (ov->GetMode())
            {
            case COMPLETE_ACCEPT:

                break;
            case COMPLETE_RECV:
                break;
            case COMPLETE_SEND:
                break;
            case COMPLETE_RELEASE:
                break;
            default:
                RT_ASSERT(FALSE);
            }
        }
    }
}

bool network::NetworkLib::stackSessionIdx_Pop(short &out)
{
    std::lock_guard lock(mStackMutex);
    if (mStackSessionIdx.empty())
        return false;

    out = mStackSessionIdx.top();
    mStackSessionIdx.pop();
    return true;
}

void network::NetworkLib::stackSessionIdx_Push(const short input)
{
    std::lock_guard lock(mStackMutex);
    mStackSessionIdx.push(input);

}
/*
  AcceptEx 매개변수

  AcceptEx(
      mListenSock,       // 리슨 소켓
      acceptSocket,      // 미리 만들어둔 빈 소켓 (socket()으로 생성해야 함)
      buffer,            // 4번쨰 인자를 0으로 두면 주소 정보 저장할 버퍼
      0,                 // 첫 데이터 받을 크기 → 0 권장 (데이터 없어도 즉시 완료)
      sizeof(SOCKADDR_IN) + 16,   // 로컬 주소 공간 + 여유공간
      sizeof(SOCKADDR_IN) + 16,   // 원격 주소 공간 + 여유공간
      &bytesReceived,    // 받은 바이트 수
      &mAcceptOv         // overlapped
  );
*/