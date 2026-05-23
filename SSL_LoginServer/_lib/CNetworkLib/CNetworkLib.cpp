// CNetworkLib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//
#include "CNetworkLib.h"
#include <thread>

thread_local LONG64 SendTPSidx;
BOOL DomainToIP(const wchar_t *szDomain, IN_ADDR *pAddr)
{
    ADDRINFOW *pAddrInfo;
    SOCKADDR_IN *pSockAddr;
    if (GetAddrInfo(szDomain, L"0", NULL, &pAddrInfo) != 0)
    {
        return FALSE;
    }
    pSockAddr = (SOCKADDR_IN *)pAddrInfo->ai_addr;
    *pAddr = pSockAddr->sin_addr;
    FreeAddrInfo(pAddrInfo);
    return TRUE;
}
BOOL GetLogicalProcess(DWORD &out)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *infos = nullptr;
    DWORD len;
    DWORD cnt;
    DWORD temp = 0;

    len = 0;

    GetLogicalProcessorInformation(infos, &len);

    infos = new SYSTEM_LOGICAL_PROCESSOR_INFORMATION[len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)];

    if (infos == nullptr)
        return false;

    GetLogicalProcessorInformation(infos, &len);

    cnt = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); // 반복문

    for (DWORD i = 0; i < cnt; i++)
    {
        if (infos[i].Relationship == RelationProcessorCore)
        {
            ULONG_PTR mask = infos[i].ProcessorMask;
            // 논리 프로세서의 수는 set된 비트의 개수
            while (mask)
            {
                temp += (mask & 1);
                mask >>= 1;
            }
        }
    }
    printf("LogicalProcess Cnt : %d \n", temp);

    delete[] infos;
    out = temp;
    return true;
}
// SRWLOCK srw_Log;

void CLanServer::WorkerThread()
{
    {
        CSystemLog::GetInstance()->Log(L"Socket", en_LOG_LEVEL::SYSTEM_Mode,
                                       L"%-20s ",
                                       L"This is WorkerThread");
        CSystemLog::GetInstance()->Log(L"TlsObjectPool", en_LOG_LEVEL::SYSTEM_Mode,
                                       L"%-20s ",
                                       L"This is WorkerThread");
    }

    // 매개변수를 통한 초기화

    static LONG64 s_arrTPS_idx = 0;
    SendTPSidx = _interlockedincrement64(&s_arrTPS_idx);

    DWORD transferred;
    ull key;
    OVERLAPPED *overlapped;
    clsSession *session; // 특정 Msg를 목적으로 nullptr을 PQCS하는 경우가 존재.

    ull local_IoCount;
    BOOL bGQCS;
    while (1)
    {
        // 지역변수 초기화
        {
            transferred = 0;
            key = 0;
            overlapped = nullptr;
            session = nullptr;
        }
        bGQCS = GetQueuedCompletionStatus(m_hIOCP, &transferred, &key, &overlapped, INFINITE);

        // 종료메세지
        if (transferred == 0 && overlapped == nullptr && key == 0)
            break;

        // PQCS로 overlapped에 nullptr을 넣는 경우를 제한 함.
        if (overlapped == nullptr && bGQCS)
        {
            CSystemLog::GetInstance()->Log(L"GQCS.txt", en_LOG_LEVEL::ERROR_Mode, L"GetQueuedCompletionStatus Overlapped is nullptr");
            //__debugbreak();
        }
        else if (overlapped == nullptr)
        {
            // GQCS를 실패한 경우임. 위에서 처리하지 못하고 여기까지 오는 경우가 있는지에 대한 분기.
            CSystemLog::GetInstance()->Log(L"GQCS.txt", en_LOG_LEVEL::ERROR_Mode, L"GQCS Failed overlapped is nullptr : %05d", GetLastError());
            break;
        }
        session = reinterpret_cast<clsSession *>(key);

        switch (reinterpret_cast<stOverlapped *>(overlapped)->_mode)
        {
        case Job_Type::Recv:
            //// FIN 의 경우에
            if (transferred == 0)
            {
                // CancleIO 유도
                session->m_blive = false;
            }

            RecvComplete(*session, transferred);
            break;
        case Job_Type::Send:
            SendComplete(*session, transferred);
            break;
        case Job_Type::ReleasePost:
            ReleaseComplete(*session);
            continue;

        default:
            CSystemLog::GetInstance()->Log(L"GQCS.txt", en_LOG_LEVEL::ERROR_Mode, L"UnDefine Error Overlapped_mode : %d", reinterpret_cast<stOverlapped *>(overlapped)->_mode);
        }
        local_IoCount = InterlockedDecrement(&session->m_ioCount);

        if (local_IoCount == 0)
        {
            ull compareRetval = InterlockedCompareExchange(&session->m_ioCount, (ull)1 << 47, 0);
            if (compareRetval != 0)
            {
                continue;
            }

            ull seqID = session->m_SeqID;
            ReleaseSession(seqID);
        }
    }
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L"WorkerThread Terminated ");
}

void CLanServer::AcceptThread()
{

    CSystemLog::GetInstance()->Log(L"Socket", en_LOG_LEVEL::SYSTEM_Mode,
                                   L"%-20s ",
                                   L"This is AcceptThread");
    CSystemLog::GetInstance()->Log(L"TlsObjectPool", en_LOG_LEVEL::SYSTEM_Mode,
                                   L"%-20s ",
                                   L"This is AcceptThread");

    SOCKET client_sock;
    SOCKADDR_IN addr;

    DWORD listen_retval;
    DWORD flag;

    ull session_id = 0;
    ull idx;

    int addrlen;

    addrlen = sizeof(addr);

    listen_retval = listen(m_listen_sock, SOMAXCONN_HINT(65535));
    flag = 0;

    // InitializeSRWLock(&srw_Log);

    if (listen_retval == 0)
        printf("Listen Sucess\n");
    else
        CSystemLog::GetInstance()->Log(L"Socket_Error", en_LOG_LEVEL::ERROR_Mode, L"Listen_Falied %d", GetLastError());
    while (1)
    {
        client_sock = accept(m_listen_sock, (sockaddr *)&addr, &addrlen);
        if (client_sock == INVALID_SOCKET)
        {
            CSystemLog::GetInstance()->Log(L"Socket_Error", en_LOG_LEVEL::ERROR_Mode,
                                           L"accept Reseult INVALID_SOCKET  GetLastError : %05d", GetLastError());
            break;
        }

        arrTPS[0]++; // Accept TPS 측정
        m_TotalAccept++;

        {
            // 예상한 Session을 초과한다면 새로 들어온 연결을 끊음.
            if (m_SessionIdxStack.Pop(idx) == false)
            {
                closesocket(client_sock);
                InterlockedIncrement(&iDisCounnectCount);
                continue;
            }
        }

        clsSession &session = sessions_vec[idx];
        // Session 초기화 부분.
        {
            ull m_SeqID = (idx << 47) + session_id++;
            session.m_sock = client_sock;
            session.m_blive = true;
            session.m_flag = 0;

            InterlockedExchange(&session.m_SeqID, m_SeqID);
            // 1로 시작하므로써 Contents에서 오인하는 일을 방지.
            InterlockedExchange(&session.m_ioCount, 1);
        }

        _interlockedincrement64(&m_SessionCount);
        // CreateIoCompletionPort 함수가 실패하면 반환 값은 NULL입니다.
        {
            HANDLE createResult = CreateIoCompletionPort((HANDLE)client_sock, m_hIOCP, (ull)&session, 0);
            if (createResult == NULL)
            {
                CSystemLog::GetInstance()->Log(L"Socket_Error", en_LOG_LEVEL::ERROR_Mode,
                                               L"CreateIoCompletionPort Reseult NULL GetLastError : %05d", GetLastError());
                closesocket(client_sock);
                continue;
            }
        }
        // AllocMsg의 처리가 너무 많이 발생한다면 False를 반환.
        if (OnAccept(session.m_SeqID, addr) == false)
        {
            DecrementIoCountAndMaybeDeleteSession(session);
            continue;
        }

        RecvPacket(session);

        DecrementIoCountAndMaybeDeleteSession(session);
    }
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L"AcceptThread Terminated %d", 0);
}

CLanServer::CLanServer(bool EnCoding)
    : bEnCording(EnCoding)
{
    hReadyForStopEvent = CreateEvent(nullptr, true, false, nullptr);
    if (hReadyForStopEvent == nullptr)
    {
        //__debugbreak();
    }

    headerSize = sizeof(stHeader);


    m_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_sock == INVALID_SOCKET)
    {
        CSystemLog::GetInstance()->Log(L"Socket_Error", en_LOG_LEVEL::ERROR_Mode, L"listen_sock Create Socket Error %d", GetLastError());
        //__debugbreak();
    }
    Parser parser;
    {
        if (parser.LoadFile(L"Config.txt") == false)
            CSystemLog::GetInstance()->Log(L"ParserError.txt", en_LOG_LEVEL::ERROR_Mode, L"LoadFileError %d", GetLastError());
        parser.GetValue(L"SendBufferLimit", SendBufferLimit);
        parser.GetValue(L"max_MsgLen", max_MsgLen);
    }
}
CLanServer::~CLanServer()
{
    // closesocket(m_listen_sock);
}

BOOL CLanServer::Start(const wchar_t *bindAddress, short port, int ZeroCopy, int WorkerCreateCnt, int reduceThreadCount, int noDelay, int MaxSessions)
{
    linger linger{1, 0};
    int buflen;
    DWORD lProcessCnt;
    DWORD bind_retval;
    HRESULT hr;
    SOCKADDR_IN serverAddr;

    _MaxSessions = MaxSessions;
    //sessions_vec.resize(MaxSessions);

    for (ull idx = 0; idx < MaxSessions; idx++)
        m_SessionIdxStack.Push(idx);

    ZeroMemory(&serverAddr, sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    InetPtonW(AF_INET, bindAddress, &serverAddr.sin_addr);

    if (ZeroCopy)
    {
        bZeroCopy = true;
        buflen = 0;
        setsockopt(m_listen_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buflen, sizeof(buflen));
    }

    setsockopt(m_listen_sock, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    setsockopt(m_listen_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof(noDelay));
    bNoDelay = noDelay;

    bind_retval = bind(m_listen_sock, (sockaddr *)&serverAddr, sizeof(serverAddr));
    if (bind_retval != 0)
        CSystemLog::GetInstance()->Log(L"Socket_Error", en_LOG_LEVEL::ERROR_Mode, L"Bind Failed %d", GetLastError());

    if (GetLogicalProcess(lProcessCnt) == false)
        CSystemLog::GetInstance()->Log(L"GetLogicalProcessError", en_LOG_LEVEL::ERROR_Mode, L"GetLogicalProcess_Error %d", GetLastError());

    m_hIOCP = (HANDLE)CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, NULL, lProcessCnt - reduceThreadCount);

    arrTPS.resize(WorkerCreateCnt + 1, 0); // Accept가 0

    m_WorkThreadCnt = WorkerCreateCnt;

    bOn = true;

    m_hAccept = WinThread(&CLanServer::AcceptThread, this);
    SetThreadDescription(m_hAccept.native_handle(), L"\tAcceptThread");

    m_hWorkerThread.reserve(WorkerCreateCnt);
    for (int i = 0; i < WorkerCreateCnt; i++)
    {
        m_hWorkerThread.emplace_back(&CLanServer::WorkerThread, this);
        std::wstring name = L"\tWorkerThread" + std::to_wstring(i);

        hr = SetThreadDescription(m_hWorkerThread[i].native_handle(), name.c_str());
    }

    return true;
}

void CLanServer::Stop()
{
    closesocket(m_listen_sock);
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Join For AcceptThread Finish ");
    m_hAccept.join();
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" AcceptThread Finish ");

    for (int idx = 0; idx < MAX_SESSION; idx++)
    {
        Disconnect(sessions_vec[idx].m_SeqID);
    }
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Every Session DisConnect ");

    // SignalOnForStop 호출을 대기
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Wait For Contents Singnal ");
    WaitForSingleObject(hReadyForStopEvent, INFINITE);
    CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Catch Contents Singnal ");

    for (int i = 0; i < m_WorkThreadCnt; i++)
    {
        CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Try WorkThread Finish PQCS ");
        PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
    }

    for (int i = 0; i < m_WorkThreadCnt; i++)
    {
        CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Join WorkerThread ");
        m_hWorkerThread[i].join();
        CSystemLog::GetInstance()->Log(L"SystemLog.txt", en_LOG_LEVEL::SYSTEM_Mode, L" Success WorkerThread Finish ");
    }
}

void CLanServer::SignalOnForStop()
{
    // Listen을 닫고, 유저들을 전부 내보낸 후에. WorkerThread를 종료시킬 필요가있다.
    // Player의 종료 또한 PQCS를 통한 절차가 이루어지므로 모든 Player의 종료를 Contents가 알려줄 필요가있다.
    SetEvent(hReadyForStopEvent);
}

bool CLanServer::Disconnect(const ull SessionID)
{
    // WorkerThread에서 호출하는 DisConnect이므로  IO가 '0' 이 되는 경우의 수가 없음.
    clsSession &session = sessions_vec[SessionID >> 47];

    ull Local_ioCount;

    // session의 보장 장치.
    Local_ioCount = InterlockedIncrement(&session.m_ioCount);

    if ((Local_ioCount & (ull)1 << 47) != 0)
    {
        return false;
    }

    if (SessionID != session.m_SeqID)
    {
        Local_ioCount = InterlockedDecrement(&session.m_ioCount);
        return false;
    }

    ull retval = InterlockedExchange(&session.m_blive, 0);
    if (retval == 1)
    {
        InterlockedIncrement(&iDisCounnectCount);
    }
    CancelIO_Routine(SessionID);
    // ContentsThread가 호출하게되면, 연결이 끊긴 Session의 ID가 올 수 있다.

    Local_ioCount = InterlockedDecrement(&session.m_ioCount);
    // 앞으로 Session 초기화는 IoCount를 '0'으로 하면 안된다.
    if (InterlockedCompareExchange(&session.m_ioCount, (ull)1 << 47, 0) == 0)
        ReleaseSession(SessionID);
    return true;
}

void CLanServer::CancelIO_Routine(const ull SessionID)
{
    // Session에 대한 안정성은  외부에서 보장해주세요.
    BOOL retval;
    clsSession &session = sessions_vec[SessionID >> 47];

    retval = CancelIoEx((HANDLE)session.m_sock, &session.m_sendOverlapped);
    retval = CancelIoEx((HANDLE)session.m_sock, &session.m_recvOverlapped);
}

LONG64 CLanServer::GetSessionCount() const
{
    // AcceptThread에서 전담한다면, interlock이 필요없다.
    return m_SessionCount;
}

LONG64 CLanServer::Get_IdxStack() const
{
    return m_SessionIdxStack.m_size;
}

void CLanServer::RecvComplete(clsSession &session, DWORD transferred)
{
    stHeader header;
    ringBufferSize useSize;

    ull SessionID;
    // 한번의 루프에 Msg카운트를 제한함.
    short msgCount;
    bool bChkSum = false;
    {
        session.m_recvBuffer.MoveRear(transferred);
        SessionID = session.m_SeqID;
    }
    // Header의 크기만큼을 확인.

    msgCount = 0;
    while (session.m_recvBuffer.Peek(&header, headerSize) == headerSize)
    {
        msgCount++;
        // OverSend를 150개 이상하였다면 끊기.
        if (msgCount >= 150)
        {
            Disconnect(session.m_SeqID);
            CSystemLog::GetInstance()->Log(L"Attack", en_LOG_LEVEL::ERROR_Mode,
                                           L"%-20s ",
                                           L" OverSend Count ");
            return;
        }
        // sDataLen 을 크게 작성하여 공격하는 경우.
        // 하나의 메세지
        if (header.sDataLen >= max_MsgLen)
        {
            Disconnect(session.m_SeqID);
            return;
        }
        useSize = session.m_recvBuffer.GetUseSize();
        if (useSize < header.sDataLen + headerSize)
        {
            // 데이터가 덜 옴.
            break;
        }
        // 메세지 생성
        CMessage *msg = CreateMessage(session, header);
        if (msg == nullptr)
        {
            // CreateMessage에서 Dequeue에서 문제가 일어났다는 뜻.
            Disconnect(session.m_SeqID);
            return;
        }
        if (bEnCording)
        {
            {
                Profiler profile(L"DeCoding");
                bChkSum = msg->DeCoding();
            }
            if (bChkSum == false)
            {
                // Attack : 조작된 패킷으로 checkSum이 다름.
                InterlockedExchange(&session.m_blive, 0);
                CancelIoEx((HANDLE)session.m_sock, &session.m_sendOverlapped);

                CSystemLog::GetInstance()->Log(L"Attack", en_LOG_LEVEL::ERROR_Mode,
                                               L"%-20s ",
                                               L" false Packet CheckSum Not Equle ");

                _MsgPool.Release(msg);
                return;
            }
        }

        InterlockedExchange(&msg->ownerID, SessionID);

        msg->_frontPtr = msg->_frontPtr + headerSize;

        // PayLoad를 읽고 무엇인가 처리하는 Logic이 NetWork에 들어가선 안된다.
        {
            Profiler profile(L"OnRecv");
            OnRecv(SessionID, msg);
            Win32::AtomicIncrement<LONG64>(m_RecvTPS);
        }
    }
    RecvPacket(session);
}

void CLanServer::DecrementIoCountAndMaybeDeleteSession(clsSession &session)
{
    LONG64 local_IoCount;

    local_IoCount = InterlockedDecrement(&session.m_ioCount);
    if (local_IoCount == 0)
    {

        if (InterlockedCompareExchange(&session.m_ioCount, (ull)1 << 47, 0) != 0)
            return;
        ReleaseSession(session.m_SeqID);
    }
}

CMessage *CLanServer::CreateMessage(clsSession &session, struct stHeader &header)
{
    // TODO : Header를 읽고, 생성하고

    CMessage *msg;
    ringBufferSize deQsize;

    // 메세지 할당
    {
        {
            Profiler profile(L"PoolAlloc");
            msg = static_cast<CMessage *>(_MsgPool.Alloc());
        }
    }
    // 순수하게 데이터만 가져옴.  EnCording 의 경우 RandKey와 CheckSum도 가져옴.
    deQsize = session.m_recvBuffer.Dequeue(msg->_frontPtr, header.sDataLen + headerSize);
    msg->_rearPtr = msg->_frontPtr + deQsize;

    if (header.sDataLen + headerSize != deQsize)
    {
        return nullptr;
    }
    return msg;
}

void CLanServer::SendComplete(clsSession &session, DWORD transferred)
{
    ringBufferSize useSize;

    DWORD bufCnt;
    int send_retval = 0;

    WSABUF wsaBuf[SENDWSABUF_MAX];
    DWORD LastError;
    // LONG64 beforeTPS;

    ull local_IoCount;
    CMessage *msg;

 
    for (DWORD i = 0; i < session.m_sendOverlapped.msgCnt; i++)
    {
        OnSend(session.m_SeqID, session.m_sendOverlapped.msgs[i]);
        _MsgPool.Release(session.m_sendOverlapped.msgs[i]);
    }

    {
        session.m_sendOverlapped.msgCnt = 0;
        ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));
    }

    useSize = (ringBufferSize)session.m_sendBuffer.m_size;

    if (useSize == 0)
    {
        useSize = (ringBufferSize)session.m_sendBuffer.m_size;
        // flag 끄기
        if (_InterlockedCompareExchange(&session.m_flag, 0, 1) == 1)
        {
            useSize = (ringBufferSize)session.m_sendBuffer.m_size;
            if (useSize != 0)
            {
                //// 누군가 진입 했다면 return
                if (_InterlockedCompareExchange(&session.m_flag, 1, 0) == 0)
                {
                    ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));

                    InterlockedIncrement(&session.m_ioCount);
                    PostQueuedCompletionStatus(m_hIOCP, 0, (ULONG_PTR)&session, &session.m_sendOverlapped);
                }
            }
        }
        return;
    }

    {
        ZeroMemory(wsaBuf, sizeof(wsaBuf));
        ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));
    }

    bufCnt = 0;

    {
        Profiler profile(L"LFQ_Pop");
        while (session.m_sendBuffer.Pop(msg))
        {
            wsaBuf[bufCnt].buf = msg->_frontPtr;
            wsaBuf[bufCnt].len = ULONG(msg->_rearPtr - msg->_frontPtr);

            session.m_sendOverlapped.msgs[bufCnt++] = msg;
            if (bufCnt == SENDWSABUF_MAX)
            {
                break;
            }
        }
    }

    session.m_sendOverlapped.msgCnt = bufCnt;
    arrTPS[SendTPSidx] += bufCnt;

    if (session.m_blive)
    {
        local_IoCount = _InterlockedIncrement(&session.m_ioCount);

        if (bZeroCopy)
        {
            Profiler profile(L"ZeroCopy WSASend");
            send_retval = WSASend(session.m_sock, wsaBuf, bufCnt, nullptr, 0, (OVERLAPPED *)&session.m_sendOverlapped, nullptr);
            LastError = GetLastError();
        }
        else
        {
            Profiler profile(L"NoZeroCopy WSASend");
            send_retval = WSASend(session.m_sock, wsaBuf, bufCnt, nullptr, 0, (OVERLAPPED *)&session.m_sendOverlapped, nullptr);
            LastError = GetLastError();
        }
        if (send_retval < 0)
            WSASendError(LastError, session.m_SeqID);
    }
}
void CLanServer::ReleaseComplete(clsSession &session)
{
    // 로직상  Session당 한번만 호출되게 짰음.

    OnRelease(session.m_SeqID);
    session.Release(_MsgPool);
    closesocket(session.m_sock);

    m_SessionIdxStack.Push(session.m_SeqID >> 47);
    _interlockeddecrement64(&m_SessionCount);
}
bool CLanServer::SessionLock(ull SessionID)
{
    /*
        ※ 인자로 Msg들고오기 싫음.
        => False를 리턴받는다면 호출부에서 msg폐기를 요구합니다.

        * Release된 Session이라면  False를 리턴
        * SessionID가 다르다면     False를 리턴  IO를 감소 시킨후 Release시도 ,  적어도 진입순간에는 Session은 살아있었음.
        * 증가시킨 IO가 '1'인 경우 감소 시킨 후  Release시도 , False를 리턴
        *
    */
    clsSession &session = sessions_vec[SessionID >> 47];
    ull Local_ioCount;
    ull seqID;

    // session의 보장 장치.
    Local_ioCount = InterlockedIncrement(&session.m_ioCount);

    if ((Local_ioCount & (ull)1 << 47) != 0)
    {
        // 새로운 세션으로 초기화되지않았고, r_flag가 세워져있으면 진입하지말자.
        // 이미 r_flag가 올라가있는데 IoCount를 잘못 올린다고 문제가 되지않을 것같다.
        return false;
    }

    // session의 Release는 막았으므로 변경되지않음.
    seqID = session.m_SeqID;
    if (seqID != SessionID)
    {
        // 새로 세팅된 Session이므로 다른 연결이라 판단.
        // 내가 잘못 올린 ioCount를 감소 시켜주어야한다.
        Local_ioCount = InterlockedDecrement(&session.m_ioCount);
        // 앞으로 Session 초기화는 IoCount를 '0'으로 하면 안된다.

        if (InterlockedCompareExchange(&session.m_ioCount, (ull)1 << 47, 0) == 0)
            ReleaseSession(seqID);

        return false;
    }

    if (Local_ioCount == 1)
    {
        // 원래 '0'이 었는데 내가 증가시켰다.
        Local_ioCount = InterlockedDecrement(&session.m_ioCount);
        // 앞으로 Session 초기화는 IoCount를 '0'으로 하면 안된다.

        if (InterlockedCompareExchange(&session.m_ioCount, (ull)1 << 47, 0) == 0)
            ReleaseSession(SessionID);

        return false;
    }
    return true;
}
void CLanServer::SessionUnLock(ull SessionID)
{
    clsSession &session = sessions_vec[SessionID >> 47];
    ull Local_ioCount;

    Local_ioCount = InterlockedDecrement(&session.m_ioCount);
    // 앞으로 Session 초기화는 IoCount를 '0'으로 하면 안된다.
    // TODO :  ContentsThread에서 Contents_Enq하는 경우의 수. 문제가없는가?
    if (InterlockedCompareExchange(&session.m_ioCount, (ull)1 << 47, 0) == 0)
        ReleaseSession(SessionID);
}

void CLanServer::SendPacket(ull SessionID, CMessage *msg, BYTE SendType,
                            std::vector<ull> *pIDVector, size_t wVecLen)
{
    // InterlockedIncrement64(&m_RecvMsgArr[en_PACKET_CS_CHAT_RES_MESSAGE]);
    switch (SendType)
    {
    case 0:
        Unicast(SessionID, msg);
        break;
    case 1:
        BroadCast(SessionID, msg, pIDVector, wVecLen);
        break;
    }
}
void CLanServer::Unicast(ull SessionID, CMessage *msg, LONG64 Account)
{
    Profiler profile(L"UnitCast_Cnt");
    if (SessionLock(SessionID) == false)
    {
        _MsgPool.Release(msg);
        return;
    }
    clsSession &session = sessions_vec[SessionID >> 47];

    // 여기까지 왔다면, 같은 Session으로 판단하자.
    CMessage **ppMsg;
    ull local_IoCount;
    ppMsg = &msg;

    {

        {
            Profiler profile(L"LFQ_Push");
            session.m_sendBuffer.Push(msg);
        }
    }

    // PQCS를 시도.
    if (InterlockedCompareExchange(&session.m_flag, 1, 0) == 0)
    {
        ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));

        local_IoCount = InterlockedIncrement(&session.m_ioCount);

        PostQueuedCompletionStatus(m_hIOCP, 0, (ULONG_PTR)&session, &session.m_sendOverlapped);
    }
    if (session.m_sendBuffer.m_size >= SendBufferLimit)
    {
        Disconnect(SessionID);
        CSystemLog::GetInstance()->Log(L"Attack", en_LOG_LEVEL::SYSTEM_Mode,
                                       L"%-20s %-10s,%llu",
                                       L"UnitCast_sendBuffer.m_size == SendBufferLimit",
                                       L"SessionID :", SessionID);
    }
    SessionUnLock(SessionID);
}
void CLanServer::BroadCast(ull SessionID, CMessage *msg, std::vector<ull> *pIDVector, size_t wVecLen)
{
    ull local_IoCount;

    InterlockedExchange64(&msg->iUseCnt, wVecLen);
    for (size_t i = 0; i < wVecLen; i++)
    {
        ull currentSessionID = (*pIDVector)[i];
        if (SessionLock(currentSessionID) == false)
        {
            _MsgPool.Release(msg);
            continue;
        }
        clsSession &session = sessions_vec[currentSessionID >> 47];

        // 여기까지 왔다면, 같은 Session으로 판단하자.
        {
            Profiler profile(L"LFQ_Push");
            session.m_sendBuffer.Push(msg);
        }

        // PQCS를 시도.
        if (InterlockedCompareExchange(&session.m_flag, 1, 0) == 0)
        {
            ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));

            local_IoCount = InterlockedIncrement(&session.m_ioCount);

            PostQueuedCompletionStatus(m_hIOCP, 0, (ULONG_PTR)&session, &session.m_sendOverlapped);
        }
        if (session.m_sendBuffer.m_size >= SendBufferLimit)
        {
            Disconnect(currentSessionID);
            CSystemLog::GetInstance()->Log(L"Attack", en_LOG_LEVEL::SYSTEM_Mode,
                                           L"%-20s %-10s,%llu",
                                           L"Broad_sendBuffer.m_size == SendBufferLimit",
                                           L"SessionID :", currentSessionID);
        }

        SessionUnLock(currentSessionID);
    }
}
void CLanServer::RecvPacket(clsSession &session)
{
    ringBufferSize freeSize;
    ringBufferSize directEnQsize;

    int wsaRecv_retval = 0;
    DWORD LastError;
    DWORD flag = 0;
    DWORD bufCnt;
    ull local_IoCount;

    WSABUF localRecvWSABuf[2]{0};

    char *f = session.m_recvBuffer._frontPtr, *r = session.m_recvBuffer._rearPtr;

    {
        ZeroMemory(&session.m_recvOverlapped, sizeof(OVERLAPPED));
    }

    directEnQsize = session.m_recvBuffer.DirectEnqueueSize(f, r);
    freeSize = session.m_recvBuffer.GetFreeSize(f, r); // SendBuffer에 바로넣기 위함.

    if (freeSize == 0)
    {
        // Attack : 조작된 Len으로 인해 리시브 버퍼가 가득참.
        InterlockedExchange(&session.m_blive, 0);
        CancelIoEx((HANDLE)session.m_sock, &session.m_sendOverlapped);
        return;
    }

    if (freeSize < directEnQsize)
    {
        //__debugbreak();
    }
    if (freeSize <= directEnQsize)
    {
        localRecvWSABuf[0].buf = r;
        localRecvWSABuf[0].len = (ULONG)directEnQsize;

        bufCnt = 1;
    }
    else
    {
        localRecvWSABuf[0].buf = r;
        localRecvWSABuf[0].len = (ULONG)directEnQsize;

        localRecvWSABuf[1].buf = session.m_recvBuffer._begin;
        localRecvWSABuf[1].len = (ULONG)(freeSize - directEnQsize);

        bufCnt = 2;
    }

    if (session.m_blive)
    {
        local_IoCount = _InterlockedIncrement(&session.m_ioCount);
        wsaRecv_retval = WSARecv(session.m_sock, localRecvWSABuf, bufCnt, nullptr, &flag, &session.m_recvOverlapped, nullptr);

        LastError = GetLastError();
        if (wsaRecv_retval < 0)
            WSARecvError(LastError, session.m_SeqID);
    }
}

int CLanServer::getAcceptTPS()
{
    static ull old_AcceptTps = 0;
    ull retval;
    ull current_AcceptTps = m_TotalAccept;

    retval = current_AcceptTps - old_AcceptTps;
    old_AcceptTps = current_AcceptTps;

    return (int)retval;
}
int CLanServer::getRecvMessageTPS()
{
    static LONG64 old_RecvTps = 0;
    LONG64 retval;
    LONG64 current_RecvTps;

    current_RecvTps = m_RecvTPS;

    retval = current_RecvTps - old_RecvTps;
    old_RecvTps = current_RecvTps;

    return (int)retval;
}
int CLanServer::getSendMessageTPS()
{
    static std::vector<LONG64> before_arrTPS(m_WorkThreadCnt + 1, 0);
    LONG64 total = 0;
    for (int i = 0; i <= m_WorkThreadCnt; i++)
    {
        LONG64 old_arrTPS = arrTPS[i];
        total += old_arrTPS - before_arrTPS[i];
        before_arrTPS[i] = old_arrTPS;
    }

    return (int)total;
}

void CLanServer::WSASendError(const DWORD LastError, const ull SessionID)
{
    clsSession &session = sessions_vec[SessionID >> 47];
    ull local_IoCount;

    switch (LastError)
    {
    case WSA_IO_PENDING:
        if (session.m_blive == 0)
        {
            CancelIoEx((HANDLE)session.m_sock, &session.m_sendOverlapped);
        }
        break;

    case WSAEINTR: // 10004
        session.m_blive = 0;
        local_IoCount = _InterlockedDecrement(&session.m_ioCount);
        break;
    case WSAENOTSOCK:     // 10038
    case WSAECONNABORTED: //    10053 :
    case WSAECONNRESET:   // 10054:
        session.m_blive = 0;
        local_IoCount = _InterlockedDecrement(&session.m_ioCount);

        break;

    default:

        CSystemLog::GetInstance()->Log(L"Socket", en_LOG_LEVEL::ERROR_Mode,
                                       L"[ %-10s %05d ],%10s %05lld  %10s %012llu  %10s %4llu  %10s %3llu",
                                       L"Send_UnDefineError", LastError,
                                       L"HANDLE : ", session.m_sock, L"seqID :", SessionID, L"seqIndx : ", session.m_SeqID >> 47,
                                       L"IO_Count", session.m_ioCount);
        session.m_blive = 0;
        local_IoCount = _InterlockedDecrement(&session.m_ioCount);
    }
}

void CLanServer::WSARecvError(const DWORD LastError, const ull SessionID)
{
    clsSession &session = sessions_vec[SessionID >> 47];
    ull local_IoCount;

    switch (LastError)
    {
    case WSA_IO_PENDING:
        if (session.m_blive == 0)
        {
            CancelIoEx((HANDLE)session.m_sock, &session.m_recvOverlapped);
        }
        break;

    case WSAEINTR:        // 10004
    case WSAENOTSOCK:     // 10038
    case WSAECONNABORTED: //    10053 :
    case WSAECONNRESET:   // 10054:
        session.m_blive = 0;
        local_IoCount = _InterlockedDecrement(&session.m_ioCount);
        break;

    default:
        CSystemLog::GetInstance()->Log(L"Socket", en_LOG_LEVEL::ERROR_Mode,
                                       L"[ %-10s %05d ] %10s %05lld  %10s %012llu  %10s %4llu  %10s %3llu",
                                       L"Recv_UnDefineError", LastError,
                                       L"HANDLE : ", session.m_sock, L"seqID :", SessionID, L"seqIndx : ", session.m_SeqID >> 47,
                                       L"IO_Count", session.m_ioCount);
        session.m_blive = 0;
        local_IoCount = _InterlockedDecrement(&session.m_ioCount);
    }
}

void CLanServer::ReleaseSession(ull SessionID)
{
    clsSession &session = sessions_vec[SessionID >> 47];
    ZeroMemory(&session.m_releaseOverlapped, sizeof(OVERLAPPED));
    PostQueuedCompletionStatus(m_hIOCP, 0, (ULONG_PTR)&session, &session.m_releaseOverlapped);
}
