#include "NetworkLib.h"
#include <iostream>

network::NetworkLib::NetworkLib()
{

    for (sessionStackDataType i = 0; i < SESSION_MAX_CNT; i++)
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
    CreateIoCompletionPort((HANDLE)mListenSock, mHcp, 0, 0);

    for (int idx = 0; idx < WORKER_THREAD_CNT; ++idx)
    {
        mWorkerThreads[idx] = std::thread(&NetworkLib::workerThread, this);
        registerAcceptEx();
    }
}

void network::NetworkLib::workerThread()
{
    // 벤치마크
    // GetQueuedCompletionStatusEx 한번에 뽑는다?
    while (true)
    {
        DWORD transferred = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *overlapped = nullptr;

        GetQueuedCompletionStatus(mHcp, &transferred, &key, &overlapped, INFINITE);
        {
            // 종료 메세지
            if (overlapped == nullptr && key == 0)
            {
                break;
            }
            MyOverlapped *ov = static_cast<MyOverlapped *>(overlapped);

            switch (ov->GetMode())
            {
            case COMPLETE_ACCEPT:
            {
                completeAcceptEx(*ov);
            }
            break;

            case COMPLETE_RECV:
            {
                Session &session = *reinterpret_cast<Session *>(key);
            }
            break;
            case COMPLETE_SEND:
            {
                Session &session = *reinterpret_cast<Session *>(key);
            }
            break;
            case COMPLETE_RELEASE:
            {
                Session &session = *reinterpret_cast<Session *>(key);
            }
            break;
            default:
                RT_ASSERT(FALSE);
            }
        }
    }
}

bool network::NetworkLib::stackSessionIdx_Pop(sessionStackDataType &out)
{
    std::lock_guard lock(mStackMutex);
    if (mStackSessionIdx.empty())
        return false;

    out = mStackSessionIdx.top();
    mStackSessionIdx.pop();
    return true;
}

void network::NetworkLib::stackSessionIdx_Push(const sessionStackDataType &input)
{
    std::lock_guard lock(mStackMutex);
    mStackSessionIdx.push(input);
}
void network::NetworkLib::registerAcceptEx()
{

    sessionStackDataType top;
    if (stackSessionIdx_Pop(top) == false)
    {
        // TODO : sessionStack이 비엇음을 알리는 Log작성후 return 하도록 변경하기.
        return;
    }

    Session &session = mSessions[top];
    session.mSock = socket(AF_INET, SOCK_STREAM, 0);
    session.mSessionID.Idx = top;
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
    // AcceptEx 함수가 실패하면 AcceptEx 는 FALSE를 반환합니다.
    {
        DWORD recvByte;
        bool retval = AcceptEx(mListenSock, session.mSock, session.mAcceptBuf, 0,
                               sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &recvByte, &session.mAcceptOv); // 주석
        if (retval == false)
        {
            DWORD lastError = GetLastError();
            switch (lastError)
            {
            case ERROR_IO_PENDING:
                break;
            default:
                std::cout << "GetLastError : " << lastError << "\n";
            }
        }
    }
}
void network::NetworkLib::completeAcceptEx(const MyOverlapped &ov)
{
    // WHY : size_t(&nullptr->mMem); 크래시가 아니다.
    /* 이유는 &와 -> 의 조합에 있어요.
      크래시가 나는 경우는 역참조해서 값을 읽거나 쓸 때예요.

      Session* p = nullptr;
      p->mAcceptOv;   // 값 읽기 → 크래시 (주소 0에 접근)
      p->mAcceptOv = x; // 값 쓰기 → 크래시

      지금 코드는 값을 읽지 않고 주소만 계산해요.
      &(reinterpret_cast<Session*>(nullptr))->mAcceptOv

      컴파일러 입장에서 이건:
      "nullptr(=주소 0)에서 mAcceptOv가 몇 바이트 떨어져 있는가?"
      = 0 + offset = offset 자체

      실제로 주소 0에 메모리 접근을 전혀 안 해요. 위치 계산만 하는 거예요.
    */
    size_t offset = reinterpret_cast<size_t>(&(reinterpret_cast<Session *>(nullptr))->mAcceptOv);
    Session &session = *reinterpret_cast<Session *>(reinterpret_cast<size_t>(&ov) - offset);

    setsockopt(session.mSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               reinterpret_cast<char *>(&mListenSock), sizeof(mListenSock));
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(session.mSock), mHcp, reinterpret_cast<ULONG_PTR>(&session), 0);

    ull seqID = InterlockedIncrement(&mSeqID);
    session.mSessionID.Seq = seqID;
    onAccept(session.mSessionID);

    registerAcceptEx();
}
