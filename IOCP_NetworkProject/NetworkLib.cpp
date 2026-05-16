#include "NetworkLib.h"
#include <iostream>

namespace network
{
NetworkLib::NetworkLib()
{
    for (seqAddrType idx = 0; idx < CONFIG_SESSION_MAX; ++idx)
    {
        stackSessionIdx_Push(idx);
    }

    mListenSock = socket(AF_INET, SOCK_STREAM, 0);
    RT_ASSERT(mListenSock != INVALID_SOCKET);

    SOCKADDR_IN addr;
    ZeroMemory(&addr, sizeof(SOCKADDR_IN));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONFIG_SERVER_PORT);
    addr.sin_addr.S_un.S_addr = htonl(CONFIG_SERVER_ADDR);

    if (CONFIG_ZERO_COPY)
    {
        char optval = 0;
        setsockopt(mListenSock, SOL_SOCKET, SO_SNDBUF, &optval, 0);
    }

    int retval = bind(mListenSock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    RT_ASSERT(retval != SOCKET_ERROR);

    retval = listen(mListenSock, SOMAXCONN_HINT(65535));
    RT_ASSERT(retval != SOCKET_ERROR);

    // CreateIoCompletionPort 함수가 실패하면 반환 값은 NULL입니다
    mHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, CONFIG_CONCURRENT_THREAD_CNT);
    RT_ASSERT(mHcp != nullptr);

    HANDLE bSucces = CreateIoCompletionPort((HANDLE)mListenSock, mHcp, 0, 0);
    RT_ASSERT(bSucces != nullptr);

    for (int idx = 0; idx < CONFIG_WORKER_THREAD_CNT; ++idx)
    {
        mWorkerThreads[idx] = std::thread(&NetworkLib::workerThread, this);
        registerAcceptEx();
    }
}

void NetworkLib::workerThread()
{
    // 벤치마크
    // GetQueuedCompletionStatusEx 한번에 뽑는다?

    while (true)
    {
        DWORD transferred = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *overlapped = nullptr;
        Session *session = nullptr;
        GetQueuedCompletionStatus(mHcp, &transferred, &key, &overlapped, INFINITE);
        {
            // 종료 메세지
            if (overlapped == nullptr && key == 0)
            {
                break;
            }
            MyOverlapped *ov = static_cast<MyOverlapped *>(overlapped);
            session = reinterpret_cast<Session *>(key);

            switch (ov->GetMode())
            {
            case eComplete::COMPLETE_ACCEPT:
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

                session = static_cast<Session*>(static_cast<AcceptOv *>(ov)->mSession);
                completeAcceptEx(*session);
            }
            break;

            case eComplete::COMPLETE_RECV:
            {
                // Client의 FIN 대응
                if (transferred == 0)
                {
                    InterlockedExchange8(&session->mLive, 0);
                    CancelIoEx(reinterpret_cast<HANDLE>(session->mSock), session->mSendOv);
                    break;
                }
                completeRecv(*session, transferred);
            }
            break;
            case eComplete::COMPLETE_SEND:
            {
            }
            break;
            case eComplete::COMPLETE_RELEASE:
            {
            }
            break;
            default:
                RT_ASSERT(FALSE);
            }
        }
        short ioCount = InterlockedDecrement16(&session->mIOcnt);
        if (ioCount == 0)
        {
            // TODO: 연결 끊기.
        }
    }
}

void NetworkLib::registerAcceptEx()
{
    ull top;
    if (stackSessionIdx_Pop(top) == false)
    {
        // TODO : sessionStack이 비엇음을 알리는 Log작성후 return 하도록 변경하기.
        return;
    }

    Session &session = mSessions[top];
    session.mSock = socket(AF_INET, SOCK_STREAM, 0);

    InterlockedExchange64(&session.mSessionID.Value, top);
    session.mIOcnt = 1;

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
        ZeroMemory(session.mAcceptOv, sizeof(OVERLAPPED));
        DWORD recvByte;
        // AcceptEx 함수가 실패하면 AcceptEx 는 FALSE를 반환합니다.
        bool retval = AcceptEx(mListenSock, session.mSock, session.mAcceptBuf, 0,
                               sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &recvByte, session.mAcceptOv); // 주석
        if (retval == false)
        {
            int lastError = WSAGetLastError();
            if (lastError != ERROR_IO_PENDING)
            {
                closesocket(session.mSock);
                stackSessionIdx_Push(top);
            }
        }
    }
}

void NetworkLib::completeAcceptEx(Session &session)
{

    setsockopt(session.mSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               reinterpret_cast<char *>(&mListenSock), sizeof(mListenSock));
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(session.mSock), mHcp, reinterpret_cast<ULONG_PTR>(&session), 0);

    seqAddrType seqID = _InterlockedIncrement64(&mSeqID);
    session.mSessionID.Seq = seqID;

    onAccept(session.mSessionID.Idx);
    registerRecv(session);

    registerAcceptEx();
}

void NetworkLib::registerRecv(Session &session)
{
    // IoCnt를 증가 시키고 Recv를 등록
    InterlockedIncrement16(&session.mIOcnt);
    utility::ringBufferSize freeSize = session.mRecvBuffer->GetFreeSize();

    // TODO : 링버퍼가 가득 찼다면 끊어야할 상황. 조작된 패킷
    if (freeSize == 0)
    {
        __debugbreak();
    }
    WSABUF wsabuf[2];
    ZeroMemory(wsabuf, sizeof(wsabuf));

    DWORD bufCnt = 1;
    wsabuf[0].buf = session.mRecvBuffer->GetRearPtr();
    wsabuf[0].len = session.mRecvBuffer->DirectEnqueueSize();

    if (freeSize - wsabuf[0].len != 0)
    {
        ++bufCnt;
        wsabuf[1].buf = session.mRecvBuffer->GetBeginPtr();
        wsabuf[1].len = freeSize - session.mRecvBuffer->DirectEnqueueSize();
    }
    ZeroMemory(session.mRecvOv, sizeof(OVERLAPPED));

    DWORD Flags = 0;
    int recvRetval;
    // 수신 작업이 즉시 완료되면 WSARecv 는 0을 반환합니다.그렇지 않으면 SOCKET_ERROR 값이 반환되
    recvRetval = WSARecv(session.mSock, wsabuf, bufCnt, NULL, &Flags, session.mRecvOv, NULL);
    if (recvRetval == SOCKET_ERROR)
    {
        checkAndHandleIoError(session, WSAGetLastError());
    }
}

void NetworkLib::completeRecv(Session &session, DWORD transferred)
{
    utility::RingBuffer &recvBuffer = *session.mRecvBuffer;
    recvBuffer.MoveRear(transferred);

    utility::ringBufferSize useSize = recvBuffer.GetUseSize();

    while (sizeof(Header) < useSize)
    {
        Header header;
        recvBuffer.Peek(&header, sizeof(header));

        // WHY : Len을 조작한 경우
        if (header.Len < 0)
        {
            disconnectSession();
            return;
        }
        // 메세지가 완성이 되었다면.

        if (header.Len + sizeof(Header) <= useSize)
        {
            recvBuffer.Dequeue(&header, sizeof(header));

            utility::Message *msg = MY_NEW utility::Message();
            utility::ringBufferSize deqSize = recvBuffer.DirectDequeueSize();

            // WHY : Header를 컨텐츠까지 끌고 가지않음.
            msg->InitMessage(session.mSessionID.Value,header.RandKey);

            if (deqSize < header.Len)
            {
                msg->PutData(recvBuffer.GetFrontPtr(), deqSize);
                msg->PutData(recvBuffer.GetBeginPtr(), header.Len - deqSize);
            }
            else
            {
                msg->PutData(recvBuffer.GetFrontPtr(), header.Len);
            }
            recvBuffer.MoveFront(header.Len);

            onRecv(msg);
        }
        useSize = recvBuffer.GetUseSize();
  
    }
    registerRecv(session);
}

void NetworkLib::checkAndHandleIoError(Session &session, const int lastError)
{
    switch (lastError)
    {
    case WSA_IO_PENDING:
        if (session.mLive == 0)
        {
            CancelIoEx((HANDLE)session.mSock, session.mRecvOv);
            CancelIoEx((HANDLE)session.mSock, session.mSendOv);
        }
        break;

    case WSAENOTSOCK: // 10038

    case WSAECONNABORTED: //    10053 :

    case WSAECONNRESET: // 10054:
        session.mLive = 0;
        _InterlockedDecrement16(&session.mIOcnt);
        break;

    default:
        RT_ASSERT(false);
    }
}
bool NetworkLib::stackSessionIdx_Pop(ull &out)
{
    std::lock_guard lock(mStackMutex);
    if (mStackSessionIdx.empty())
    {
        return false;
    }

    out = mStackSessionIdx.top();
    mStackSessionIdx.pop();
    return true;
}

void NetworkLib::stackSessionIdx_Push(const ull &input)
{
    std::lock_guard lock(mStackMutex);
    mStackSessionIdx.push(input);
}

}; // namespace network