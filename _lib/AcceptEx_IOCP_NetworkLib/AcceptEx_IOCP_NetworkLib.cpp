// AcceptEx_IOCP_NetworkLib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//

#include "pch.h"

#include <iostream>
#include "AcceptEx_IOCP_NetworkLib.h"

network::WsadataRAII wsadata;

void fnAcceptExIOCPNetworkLib()
{
    using namespace network;
    class EchoServer : public NetworkLib
    {
        virtual void onAccept(const ull &sessionID) {};
        virtual void onRecv(utility::Message *msg) {};
        virtual void onSend(utility::Message *msg) {};

        // 보낼시 SendPost
        SeqAndIdx sessionID;
        utility::Message *msg;
        
    };
}


namespace network
{
NetworkLib::NetworkLib()
{

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

    for (seqAddrType idx = 0; idx < CONFIG_SESSION_MAX; ++idx)
    {
        stackSessionIdx_Push(idx);
        registerAcceptEx();
    }

    for (int idx = 0; idx < CONFIG_WORKER_THREAD_CNT; ++idx)
    {
        mWorkerThreads[idx] = std::thread(&NetworkLib::workerThread, this);
    }
}

void NetworkLib::workerThread()
{
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
                session = static_cast<Session *>(static_cast<AcceptOv *>(ov)->mSession);
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
                completeSend(*session);
            }
            break;
            case eComplete::COMPLETE_RELEASE:
            {
                completeRelease(*session);
            }
            break;
            default:
                RT_ASSERT(FALSE);
            }
        }
        short ioCount = InterlockedDecrement16(&session->mIOcnt);
        if (ioCount == 0)
        {
            if (InterlockedCompareExchange16(&session->mIOcnt, (short)(1 << 15), 0) == 0)
            {
                PostQueuedCompletionStatus(mHcp, 0, key, session->mReleaseOv);
            }
        }
    }
}

void NetworkLib::registerAcceptEx()
{
    ull top;
    if (stackSessionIdx_Pop(top) == false)
    {
        // TODO : sessionStack이 비엇음을 알리는 Log작성후 return 하도록 변경하기.
        wprintf(L"[NetworkLib] 세션 풀 고갈 - 최대 접속 수 초과\n");
        return;
    }

    Session &session = mSessions[top];
    session.mSock = socket(AF_INET, SOCK_STREAM, 0);

    InterlockedExchange64(&session.mSessionID.Value, top);

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

    SeqAndIdx seqIdx;
    seqIdx.Idx = session.mSessionID.Idx;
    seqIdx.Seq = seqID;
    
    InterlockedExchange8(&session.mLive, true);
    InterlockedExchange64(&session.mSessionID.Value, seqIdx.Value);
    InterlockedExchange16(&session.mIOcnt, 1);

    SOCKADDR_IN addr;
    int nameLen = sizeof(addr);
    getpeername(session.mSock, (sockaddr *)&addr, &nameLen);

    onAccept(addr,session.mSessionID);
    registerRecv(session);

    registerAcceptEx();
}

void NetworkLib::registerRecv(Session &session)
{
    // IoCnt를 증가 시키고 Recv를 등록
    utility::ringBufferSize freeSize = session.mRecvBuffer->GetFreeSize();

    if (freeSize == 0)
    {
        disconnectSession(session.mSessionID);
        return;
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

    if (session.mLive)
    {
        DWORD Flags = 0;
        InterlockedIncrement16(&session.mIOcnt);
        // 수신 작업이 즉시 완료되면 WSARecv 는 0을 반환합니다.그렇지 않으면 SOCKET_ERROR 값이 반환되
        int recvRetval = WSARecv(session.mSock, wsabuf, bufCnt, NULL, &Flags, session.mRecvOv, NULL);
        if (recvRetval == SOCKET_ERROR)
        {
            checkAndHandleIoError(session, WSAGetLastError());
        }
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

        // WHY : Len을 조작한 경우 임. 적어도 type을 위한 0 이상의 크기가 PayLoad로 들어감.
        if (header.Len <= 0)
        {
            disconnectSession(session.mSessionID);
            return;
        }
        // 메세지가 완성이 되었다면.

        if (header.Len + sizeof(Header) <= useSize)
        {
            recvBuffer.Dequeue(&header, sizeof(header));

            utility::Message *msg = MY_NEW utility::Message();
            utility::ringBufferSize deqSize = recvBuffer.DirectDequeueSize();

            // WHY : Header를 컨텐츠까지 끌고 가지않음.
            msg->InitMessage(session.mSessionID.Value, header.RandKey);

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
        else
        {
            break;
        }
        useSize = recvBuffer.GetUseSize();
    }
    registerRecv(session);
}

void NetworkLib::registerSend(Session &session)
{
    SendOv &sendOv = *session.mSendOv;
    __int16 msgCnt = static_cast<__int16>(CONFIG_SEND_MESSAGE_MAXCOUNT < session.mSenqQSize ? CONFIG_SEND_MESSAGE_MAXCOUNT : session.mSenqQSize);

    if (msgCnt == 0)
    {
        InterlockedExchange8(&session.mSendFlag, false);
        msgCnt = static_cast<__int16>(CONFIG_SEND_MESSAGE_MAXCOUNT < session.mSenqQSize ? CONFIG_SEND_MESSAGE_MAXCOUNT : session.mSenqQSize);
        if (msgCnt != 0)
        {
            if (_InterlockedCompareExchange8(&session.mSendFlag, true, false) == false)
            {
                ZeroMemory(&sendOv, sizeof(OVERLAPPED));
                InterlockedIncrement16(&session.mIOcnt);
                PostQueuedCompletionStatus(mHcp, 0, (ULONG_PTR)&session, session.mSendOv);
            }
        }
        return;
    }
    sendOv.mMsgCnt = msgCnt;

    WSABUF wsabuf[CONFIG_SEND_MESSAGE_MAXCOUNT];
    ZeroMemory(wsabuf, sizeof(wsabuf));
    for (__int16 cnt = 0; cnt < msgCnt; ++cnt)
    {
        utility::Message *msg = session.DeQueueMsgOrNull();
        RT_ASSERT(msg != nullptr);
        sendOv.mSendMsgs[cnt] = msg;

        wsabuf[cnt].buf = msg->GetFrontPtr();
        wsabuf[cnt].len = static_cast<ULONG>(msg->GetUseSize());
    }

    ZeroMemory(&sendOv, sizeof(OVERLAPPED));

    // 수신 작업이 즉시 완료되면 WSARecv 는 0을 반환합니다.그렇지 않으면 SOCKET_ERROR 값이 반환되
    if (session.mLive)
    {
        DWORD Flags = 0;
        InterlockedIncrement16(&session.mIOcnt);
        int sendRetval = WSASend(session.mSock, wsabuf, msgCnt, NULL, Flags, &sendOv, NULL);
        if (sendRetval == SOCKET_ERROR)
        {
            checkAndHandleIoError(session, WSAGetLastError());
        }
    }
}

void NetworkLib::completeSend(Session &session)
{
    SendOv &sendOv = *session.mSendOv;

    // 완료통지에서 이미 보낸 MSG 반환.
    for (__int16 idx = 0; idx < sendOv.mMsgCnt; ++idx)
    {
        onSend(static_cast<utility::Message *>(sendOv.mSendMsgs[idx]));
        MY_DELETE sendOv.mSendMsgs[idx];
    }
    registerSend(session);
}
void NetworkLib::completeRelease(Session &session)
{
    session.ReleaseSession();
    onRelease(session.mSessionID);

    stackSessionIdx_Push(session.mSessionID.Idx);
    registerAcceptEx();
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
        InterlockedExchange8(&session.mLive, false);
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

bool NetworkLib::sessionLock(const SeqAndIdx& sessionID)
{
    Session &session = mSessions[sessionID.Idx];
    short ioCnt = InterlockedIncrement16(&session.mIOcnt);

    if ((ioCnt & RELEASE_IOCOUNT) != 0)
    {
        // 이미 끊긴 연결
        return false;
    }
    if (session.mSessionID != sessionID)
    {
        // 대상이 다름.
        ioCnt = InterlockedDecrement16(&session.mIOcnt);
        if (ioCnt == 0)
        {
            if (InterlockedCompareExchange16(&session.mIOcnt, RELEASE_IOCOUNT, 0) == 0)
            {
                PostQueuedCompletionStatus(mHcp, 0, (ULONG_PTR)&session, session.mReleaseOv);
            }
        }
        return false;
    }
    return true;
}

void NetworkLib::sessionUnLock(const SeqAndIdx& sessionID)
{
    Session &session = mSessions[sessionID.Idx];
    short ioCnt = InterlockedDecrement16(&session.mIOcnt);
    if (ioCnt == 0)
    {
        if (InterlockedCompareExchange16(&session.mIOcnt, RELEASE_IOCOUNT, 0) == 0)
        {
            PostQueuedCompletionStatus(mHcp, 0, (ULONG_PTR)&session, session.mReleaseOv);
        }
    }
}

void NetworkLib::sendPost(const SeqAndIdx& sessionID, utility::Message &msg)
{
    if (sessionLock(sessionID) == false)
    {
        MY_DELETE & msg;
        return;
    }
    Session &session = mSessions[sessionID.Idx];
    session.EnQueueMsg(msg);

    if (_InterlockedCompareExchange8(&session.mSendFlag, true, false) == false)
    {
        _InterlockedIncrement16(&session.mIOcnt);
        PostQueuedCompletionStatus(mHcp, 0, (ULONG_PTR)&session, session.mSendOv);
    }

    sessionUnLock(sessionID);
}

void NetworkLib::disconnectSession(const SeqAndIdx& sessionID)
{

    if (sessionLock(sessionID) == false)
    {
        return;
    }
    Session &session = mSessions[sessionID.Idx];
    InterlockedExchange8(&session.mLive, false);

    CancelIoEx((HANDLE)session.mSock, session.mSendOv);
    CancelIoEx((HANDLE)session.mSock, session.mRecvOv);

    sessionUnLock(sessionID);
}

}; // namespace network