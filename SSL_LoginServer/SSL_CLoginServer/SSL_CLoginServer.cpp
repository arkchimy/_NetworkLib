#include "SSL_CLoginServer.h"

#include "../_lib/CDB/CDB.h"
#include <cpp_redis/cpp_redis>
#include <bcrypt.h>
#pragma comment(lib, "cpp_redis.lib")
#pragma comment(lib, "tacopie.lib")
#pragma comment(lib, "Bcrypt.lib")

// MonitorThread
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

thread_local cpp_redis::client *redisClient;

extern thread_local CDB db;

#define RT_ASSERT(x) \
    if (!(x))        \
    __debugbreak();
void SSLFatal(const char *msg)
{
    printf("[ERROR] %s\n", msg);
    ERR_print_errors_fp(stdout);
    exit(1);
}

SSL_CLoginServer::SSL_CLoginServer()
{
    HRESULT hr;
    DWORD DBThreadCnt;
    _bOn = true;
    mysql_library_init(0, nullptr, nullptr);

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_server_method();
    _ctx = SSL_CTX_new(method);
    if (_ctx == NULL)
        SSLFatal("SSL_CTX_new 실패");

    //--------------------------------------------------
    // 3. 인증서 & 개인키 로드
    //--------------------------------------------------
    if (SSL_CTX_use_certificate_file(_ctx, "server.crt", SSL_FILETYPE_PEM) <= 0)
        SSLFatal("인증서 로드 실패 - server.crt 파일 확인");

    if (SSL_CTX_use_PrivateKey_file(_ctx, "server.key", SSL_FILETYPE_PEM) <= 0)
        SSLFatal("개인키 로드 실패 - server.key 파일 확인");

    if (!SSL_CTX_check_private_key(_ctx))
        SSLFatal("인증서와 개인키가 일치하지 않음");

    printf("[서버] 인증서 로드 완료\n");

    {
        {
            Parser parser;
            parser.LoadFile(L"Config.txt");
            parser.GetValue(L"DBThreadCnt", DBThreadCnt);
            parser.GetValue(L"RedisIp", RedisIpAddress, IP_LEN);
        }
        hDBThread_vec.resize(DBThreadCnt);

        // AuthThread의 시작.
        {
            // CreateIoCompletionPort 함수가 실패하면 반환 값은 NULL입니다.
            _hDBIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 5);
            RT_ASSERT(_hDBIocp != nullptr);

            // CreateEvent 함수가 실패하면 반환 값은 NULL
            _hAuthEvent = CreateEvent(nullptr, false, false, nullptr);
            RT_ASSERT(_hAuthEvent != nullptr);

            _hAuthThread = WinThread(&SSL_CLoginServer::AuthThread, this);
            hr = SetThreadDescription(_hAuthThread.native_handle(), L"\tAuthThread ");
            RT_ASSERT(!FAILED(hr));
        }

        for (DWORD i = 0; i < DBThreadCnt; i++)
        {
            std::wstring ContentsThreadName = L"\tDBThread" + std::to_wstring(i);

            hDBThread_vec[i] = WinThread(&SSL_CLoginServer::DBworkerThread, this);

            RT_ASSERT(hDBThread_vec[i].native_handle() != nullptr);

            hr = SetThreadDescription(hDBThread_vec[i].native_handle(), ContentsThreadName.c_str());
            RT_ASSERT(!FAILED(hr));
        }
    }
    _hMonitorThread = WinThread(&SSL_CLoginServer::MonitorThread, this);
    hr = SetThreadDescription(_hMonitorThread.native_handle(), L"\tMonitorThread");
    RT_ASSERT(!FAILED(hr));
}

void SSL_CLoginServer::Start()
{

    {
        wchar_t bindAddr[16];
        short bindPort;

        int iZeroCopy;
        int iEnCording;
        int DBWorkerThreadCnt, DBContentsThreadCnt;
        int WorkerThreadCnt, ContentsThreadCnt;
        int reduceThreadCount;
        int NoDelay;
        int maxSessions;

        Parser parser;

        if (parser.LoadFile(L"Config.txt") == false)
            CSystemLog::GetInstance()->Log(L"ParserError.txt", en_LOG_LEVEL::ERROR_Mode, L"LoadFileError %d", GetLastError());
        parser.GetValue(L"ServerAddr", bindAddr, 32);
        parser.GetValue(L"ServerPort", bindPort);

        parser.GetValue(L"ChatServerIP", ChatServerIP, 32);
        parser.GetValue(L"ChatServerPort", ChatServerPort);

        parser.GetValue(L"GameServerIP", GameServerIP, 32);
        parser.GetValue(L"GameServerPort", GameServerPort);

 
        parser.GetValue(L"ZeroCopy", iZeroCopy);

        parser.GetValue(L"DBWorkerThreadCnt", DBWorkerThreadCnt);
        parser.GetValue(L"DBContentsThreadCnt", DBContentsThreadCnt);

        parser.GetValue(L"WorkerThreadCnt", WorkerThreadCnt);
        parser.GetValue(L"ReduceThreadCount", reduceThreadCount);
        parser.GetValue(L"NoDelay", NoDelay);
        parser.GetValue(L"MaxSessions", maxSessions);

        parser.GetValue(L"ContentsThreadCnt", ContentsThreadCnt);
        // parser.GetValue(L"ContentsRingBufferSize", ContentsRingBufferSize);
        parser.GetValue(L"EnCording", iEnCording);

        CLanServer::Start(bindAddr, bindPort, iZeroCopy, WorkerThreadCnt, reduceThreadCount, NoDelay, maxSessions);
    }

    _bOn = true;
}

void SSL_CLoginServer::REQ_LOGIN(ull SessionID, CMessage *msg, WCHAR *ID, WCHAR *PW,
                                 WORD wType, BYTE bBroadCast, std::vector<ull> *pIDVector, size_t wVectorLen)
{
    stDBOverlapped *DBoverLap = static_cast<stDBOverlapped *>(dbOverlapped_pool.Alloc());
    DBoverLap->msg = msg;
    msg->ownerID = SessionID;

    *msg << wType;
    msg->PutData(ID, 40);
    msg->PutData(PW, 40);

    PostQueuedCompletionStatus(_hDBIocp, 0, 0, DBoverLap);
}

void SSL_CLoginServer::REQ_ECHO(ull SessionID, CMessage *msg, WCHAR *str, int strLen,
                                WORD wType, BYTE bBroadCast, std::vector<ull> *pIDVector, size_t wVectorLen)
{
    if (strLen > 512)
    {
        __debugbreak();
    }

    {
        std::lock_guard<SharedMutex> authlock(_srw_Wait);
        auto authIter = _SessionID_wait_hash.find(SessionID);
        if (authIter == _SessionID_wait_hash.end())
        {
            __debugbreak();
        }

        stWaitSession *authSession = authIter->second;
        authSession->_lastTime = timeGetTime();

    }

    Proxy::RES_ECHO(SessionID, msg, str, strLen);
}
void SSL_CLoginServer::AuthThread()
{
    CMessage *msg;
    while (_bOn)
    {
        WaitForSingleObject(_hAuthEvent, 6000);
        while (_AuthThreadQueue.m_size != 0)
        {
            _AuthThreadQueue.Pop(msg);
            // PacketProc
            if (PacketProc(msg->ownerID, msg) == false)
            {
                Disconnect(msg->ownerID);
            }
        }
        HeartBeat();
    }
    __debugbreak();
}
void SSL_CLoginServer::DBworkerThread()
{
    DWORD transferred;
    ull key;
    OVERLAPPED *overlapped;
    clsSession *session; // 특정 Msg를 목적으로 nullptr을 PQCS하는 경우가 존재.

    stDBOverlapped *dbOverlapped;
    cpp_redis::client a;

    char RedisAddr[IP_LEN];
    size_t Convlen;
    wcstombs_s(&Convlen, RedisAddr, IP_LEN, RedisIpAddress, _TRUNCATE);
    redisClient = &a;

    redisClient->connect(RedisAddr, 6379);

    db.Connect();
    srand(GetCurrentThreadId());

    while (1)
    {
        // 지역변수 초기화
        {
            transferred = 0;
            key = 0;
            overlapped = nullptr;
            session = nullptr;
        }
        GetQueuedCompletionStatus(_hDBIocp, &transferred, &key, &overlapped, INFINITE);
        if (overlapped == nullptr)
            __debugbreak();
        dbOverlapped = reinterpret_cast<stDBOverlapped *>(overlapped);
        if (dbOverlapped->msg == nullptr)
            __debugbreak();

        // DB_PacketProc의 반환값에 따른 동작
        DB_PacketProc(dbOverlapped->msg);
        dbOverlapped_pool.Release(dbOverlapped);
        //_interlockedincrement64(&m_UpdateTPS);
        //_interlockeddecrement64(&_DBMessageCnt);
    }
}
void SSL_CLoginServer::MonitorThread()
{
    HANDLE hProcess = GetCurrentProcess();

    while (_bOn)
    {
        Sleep(1000);

        // TPS 스냅샷 (원자적으로 읽고 0으로 리셋)
        LONG64 accept = InterlockedExchange64(&_acceptTPS, 0);
        LONG64 loginTry = _loginTryTPS;
        LONG64 loginOK = _loginOK;
        LONG64 loginFail = _loginFail;
        LONG64 recv = InterlockedExchange64(&_recvTPS, 0);
        LONG64 send = InterlockedExchange64(&_sendTPS, 0);

        // 세션 해시 크기
        size_t waitCnt, authCnt;
        {
            std::shared_lock<SharedMutex> lw(_srw_Wait);
            waitCnt = _SessionID_wait_hash.size();
        }
        {
            std::shared_lock<SharedMutex> la(_srw_Auth);
            authCnt = _SessionID_auth_hash.size();
        }

        // 큐 크기
        LONG64 queueSize = _AuthThreadQueue.m_size;

        // 프로세스 메모리
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc));
        DWORD memMB = (DWORD)(pmc.WorkingSetSize / (1024 * 1024));

        // 콘솔 출력
        printf(
            "========== SSL LoginServer Monitor ==========\n"
            " Sessions    | Wait: %6zu  Auth: %6zu\n"
            " Accept/sec  | %lld\n"
            " Login/sec   | Try: %lld  OK: %lld  Fail: %lld\n"
            " Packet/sec  | Recv: %lld  Send: %lld\n"
            " AuthQueue   | %lld\n"
            " Memory      | %lu MB\n"
            " DB_Queue      | %lu \n"
            " MsgPool_Alloc      | %lu \n"
            " MsgPool_Active      | %lu \n"
            "=============================================\n",
            waitCnt, authCnt,
            accept,
            loginTry, loginOK, loginFail,
            recv, send,
            queueSize,
            memMB, _DB_ReqCnt, _MsgPool.GetAllocNodeCnt(), _MsgPool.GetActiveNodeCnt());
    }
}
void SSL_CLoginServer::HeartBeat()
{

    DWORD lastTime;
    DWORD currentTime;
    stWaitSession *waitSession;
    stAuthSession *authSession;

    currentTime = timeGetTime();

    {
        std::lock_guard<SharedMutex> wait_lock(_srw_Wait);
        for (auto iter = _SessionID_wait_hash.begin(); iter != _SessionID_wait_hash.end();)
        {
            lastTime = iter->second->_lastTime;
            if (currentTime - lastTime > 50000)
            {
                waitSession = iter->second;
                Disconnect(waitSession->_sessionID);
            }
            iter++;
        }
    }

    {
        std::lock_guard<SharedMutex> auth_lock(_srw_Auth);
        for (auto iter = _SessionID_auth_hash.begin(); iter != _SessionID_auth_hash.end();)
        {
            lastTime = iter->second->_lastTime;
            if (currentTime - lastTime > 30000)
            {
                authSession = iter->second;
                Disconnect(authSession->_sessionID);
            }
            iter++;
        }
    }
}

void SSL_CLoginServer::DB_PacketProc(CMessage *msg)
{
    // 내가만든 msg 이므로 조작 안됫다고 봄.
    WORD wType;
    ull sessionID;
    *msg >> wType;
    switch (wType)
    {
    case en_PACKET_CS_LOGIN_REQ_AUTH:
    {

        sessionID = msg->ownerID;
        PacketProc_LoginAuth(sessionID, msg);
        break;
    }
    }
}

void SSL_CLoginServer::PacketProc_LoginAuth(ull SessionID, CMessage *msg)
{

    BYTE dbResult;

    WCHAR ID[ID_LEN] = {0};
    WCHAR PW[Password_LEN] = {0};
    int accountNo = 0;
    SOCKADDR_IN addr;
    char tokenKey[20]{0}; // 랜덤으로 생성하는방법 추천해줘.
    BYTE enCodeKey = 0;
    // 패킷에서 ID, PW 추출
    msg->GetData(ID, sizeof(WCHAR) * ID_LEN);
    msg->GetData(PW, sizeof(WCHAR) * Password_LEN);


    auto ramda = [](cpp_redis::reply &r)
    {
        if (r.is_error())
        {
            CSystemLog::GetInstance()->Log(L"Redis", en_LOG_LEVEL::ERROR_Mode,
                                           L"hmset failed: %S", r.error().c_str());
        }
    };
    // 로그인 성공시 accountNo 초기화.
    InterlockedIncrement64(&_loginTryTPS);
    InterlockedIncrement64(&_DB_ReqCnt);

    dbResult = WaitDB(ID, PW, accountNo);
    // 로그인 성공
    if (dbResult == dfLOGIN_DB_OK)
    {
        InterlockedIncrement64(&_loginOK);
        {
            std::lock_guard<SharedMutex> waitlock(_srw_Wait);
            auto waitIter = _SessionID_wait_hash.find(SessionID);
            if (waitIter == _SessionID_wait_hash.end())
            {
                __debugbreak();
            }
            stWaitSession *waitSession = waitIter->second;
            addr = waitSession->_addr;
            _SessionID_wait_hash.erase(waitIter);
            _waitSessionPool.Release(waitSession);

        }
        {
            std::lock_guard<SharedMutex> authlock(_srw_Auth);
            auto authIter = _SessionID_auth_hash.find(SessionID);
            if ( authIter != _SessionID_auth_hash.end())
            {
                __debugbreak();
            }
            stAuthSession *authSession = static_cast<stAuthSession *>(_authSessionPool.Alloc());
            _SessionID_auth_hash.insert({SessionID, authSession});
            authSession->_addr = addr;
            authSession->_lastTime = timeGetTime();
            authSession->_sessionID = SessionID;

        }
        // 이부분에서 Redis에 등록!
        enCodeKey = rand() % 256;
        //BCryptGenRandom 함수가 성공하면 STATUS_SUCCESS 입니다.
        BCryptGenRandom(nullptr, (BYTE *)tokenKey, 20, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        std::vector<std::pair<std::string, std::string>> fields = {
            {"tokenKey", std::string(tokenKey, 20)},
            {"encKey", std::to_string(enCodeKey)}};
        std::string key = "session:" + std::to_string(accountNo);

        redisClient->hmset(key, fields, ramda);
        redisClient->expire(key, 60, ramda);
        redisClient->sync_commit(std::chrono::milliseconds(500));
    }
    else
    {
        InterlockedIncrement64(&_loginFail);
        std::shared_lock<SharedMutex> waitlock(_srw_Wait);
        auto waitIter = _SessionID_wait_hash.find(SessionID);
        if (waitIter == _SessionID_wait_hash.end())
        {
            __debugbreak();
        }
        stWaitSession *waitSession = waitIter->second;

    }
    InterlockedDecrement64(&_DB_ReqCnt);

    Proxy::RES_LOGIN(SessionID, msg, dbResult, accountNo, tokenKey, enCodeKey, GameServerIP, GameServerPort, ChatServerIP, ChatServerPort);
    
}

BYTE SSL_CLoginServer::WaitDB(WCHAR *ID, WCHAR *Password, int& outAccountNo)
{
    char idA[ID_LEN + 1], pwA[Password_LEN + 1];
    size_t conv;
    int accountNo;
    ZeroMemory(idA, ID_LEN + 1);
    ZeroMemory(pwA, Password_LEN + 1);

    // utf-8로 변경
    wcstombs_s(&conv, idA, ID_LEN, ID, _TRUNCATE);
    wcstombs_s(&conv, pwA, Password_LEN, Password, _TRUNCATE);

    // 쿼리 날리기
    {
        stSTMTResultSet stRow;
        db.Query("SELECT * FROM Account Where id = %s AND pw = %s ", stRow, idA, pwA);
        if (stRow.Fetch())
        {
            accountNo = atoi(stRow.GetValue("AccountNo"));
            std::string ID = stRow.GetValue("ID");
            std::string PW = stRow.GetValue("PW");

            //printf("AcccountNo : %d , ID : %s , PW : %s \n", accountNo, ID.c_str(), PW.c_str());
            outAccountNo = accountNo;
        }
        else
        {
            stSTMTResultSet stRow;
            db.Query("SELECT * FROM Account Where pw = %s ", stRow, pwA);
            if (stRow.Fetch())
            {
                int accountNo = atoi(stRow.GetValue("AccountNo"));
                std::string ID = stRow.GetValue("ID");
                std::string PW = stRow.GetValue("PW");

                //printf("AcccountNo : %d , ID : %s \n", accountNo, ID.c_str());
                //printf("PW_MISS  input : %s , db : %s \n", pwA, PW.c_str());
                return dfLOGIN_DB_PASSWORD_MISS;
            }
            return dfLOGIN_DB_ID_MISS;
        }
    }
    return dfLOGIN_DB_OK;
}

bool SSL_CLoginServer::OnAccept(ull SessionID, SOCKADDR_IN &addr)
{
    stWaitSession *reqSession = static_cast<stWaitSession *>(_waitSessionPool.Alloc());
    std::unordered_map<ull, stWaitSession *>::iterator iter;
    bool bExist = false;

    InterlockedIncrement64(&_acceptTPS);
    // Pool의 Limit에 도달햇을 때.
    if (reqSession == nullptr)
    {
        return false;
    }

    reqSession->_sessionID = SessionID;
    reqSession->_lastTime = timeGetTime();

    reqSession->_addr = addr;

    // SSL
    {
        clsSession &session = GetSession(SessionID);
        session._ssl = SSL_new(_ctx);
        RT_ASSERT(session._ssl != nullptr);

        // 2. 메모리 BIO 생성 (소켓 대신 메모리를 읽고 쓰게 함)
        session._rbio = BIO_new(BIO_s_mem()); // SSL이 여기서 읽음 (수신용)
        session._wbio = BIO_new(BIO_s_mem()); // SSL이 여기에 씀 (송신용).

        // 3. SSL에 BIO 연결
        SSL_set_bio(session._ssl, session._rbio, session._wbio);

        // 4. 서버 역할로 설정 (핸드셰이크 시 ServerHello를 보내는 쪽)
        SSL_set_accept_state(session._ssl);
    }

    {
        std::lock_guard<SharedMutex> w_lock(_srw_Wait);
        iter = _SessionID_wait_hash.find(SessionID);
        if (iter == _SessionID_wait_hash.end())
        {
            _SessionID_wait_hash.insert({SessionID, reqSession});
        }
        else
            bExist = true;
    }
    if (bExist)
    {
        CSystemLog::GetInstance()->Log(L"OnAccept", en_LOG_LEVEL::ERROR_Mode,
                                       L"%-20s SessionID %llu exist ",
                                       L"sessionID_Req_Hash.find(SessionID)", SessionID);
        __debugbreak();
    }

    return true;
}

void SSL_CLoginServer::OnRecv(ull SessionID, CMessage *msg)
{
    InterlockedIncrement64(&_recvTPS);
    _AuthThreadQueue.Push(msg);
    SetEvent(_hAuthEvent);
}

void SSL_CLoginServer::OnSend(ull SessionID, CMessage *msg)
{
    InterlockedIncrement64(&_sendTPS);
    if (msg->_bLastMessage == false)
        return;
    Disconnect(SessionID);
}



void SSL_CLoginServer::OnRelease(ull SessionID)
{
    bool bChk = false;
    {
        std::lock_guard<SharedMutex> lock(_srw_Wait);
        auto iter = _SessionID_wait_hash.find(SessionID);
        if (iter != _SessionID_wait_hash.end())
        {
            iter->second->Init();
            _waitSessionPool.Release(iter->second);
            _SessionID_wait_hash.erase(iter);
            bChk = true;
        }
    }
    {
        std::lock_guard<SharedMutex> lock(_srw_Auth);
        auto iter = _SessionID_auth_hash.find(SessionID);
        if (iter != _SessionID_auth_hash.end())
        {
            iter->second->Init();
            _authSessionPool.Release(iter->second);
            _SessionID_auth_hash.erase(iter);
            bChk = true;
        }
    }
    if (!bChk)
        __debugbreak();
}

void SSL_CLoginServer::RecvComplete(clsSession &session, DWORD transferred)
{
    std::lock_guard<SharedMutex> lock(session._srwSSL);
    char tempBuf[3000];
    // 1. 수신된 암호문을 링버퍼에 반영 후 전부 꺼내서 BIO에 주입─────────────────────────────────────────────────────
    session.m_recvBuffer.MoveRear(transferred);
    ringBufferSize len = session.m_recvBuffer.Dequeue(tempBuf, session.m_recvBuffer.GetUseSize());
    BIO_write(session._rbio, tempBuf, (int)len);

    // 2. 핸드셰이크 진행 중
    if (session._sslState == SSLState::None)
    {
        int ret = SSL_do_handshake(session._ssl);

        // wbio 응답을 직접 WSASend (이미 암호화된 ciphertext이므로 SSL_write 불필요)
        int pending = BIO_pending(session._wbio);
        if (pending > 0)
        {
            WSABUF wsaBuf[500];
            DWORD bufCnt = 0;

            ZeroMemory(&session.m_sendOverlapped, sizeof(OVERLAPPED));

            while (pending > 0 && bufCnt < 500)
            {
                CMessage *sendMsg = static_cast<CMessage *>(_MsgPool.Alloc());
                int readSize = min(pending, (int)CMessage::bufferSize);
                int n = BIO_read(session._wbio, sendMsg->_frontPtr, readSize);
                sendMsg->_rearPtr = sendMsg->_frontPtr + n;

                wsaBuf[bufCnt].buf = sendMsg->_frontPtr;
                wsaBuf[bufCnt].len = (ULONG)n;
                session.m_sendOverlapped.msgs[bufCnt] = sendMsg;
                bufCnt++;

                pending = BIO_pending(session._wbio);
            }

            session.m_sendOverlapped.msgCnt = bufCnt;
            InterlockedExchange(&session.m_flag, 1);
            InterlockedIncrement(&session.m_ioCount);

            int send_retval = WSASend(session.m_sock, wsaBuf, bufCnt, nullptr, 0,
                                       (OVERLAPPED *)&session.m_sendOverlapped, nullptr);
            if (send_retval < 0)
            {
                DWORD LastError = GetLastError();
                if (LastError != WSA_IO_PENDING)
                    WSASendError(LastError, session.m_SeqID);
            }
        }

        if (ret == 1)
        {
            // 핸드셰이크 완료
            session._sslState = SSLState::Established;
            // 핸드셰이크와 동시에 게임 데이터가 왔을 수 있으므로
            // 아래 Established 처리로 계속 진행
        }
        else
        {
            int err = SSL_get_error(session._ssl, ret);
            if (err == SSL_ERROR_WANT_READ)
            {
                // 아직 진행 중 — 다음 수신 대기
                RecvPacket(session);
                return;
            }
            Disconnect(session.m_SeqID);
            return;
        }
    }
    //  Established: 복호화 → 게임 메시지 처리
    {
        char plainBuf[3000];
        int plainLen;

        // SSL_read로 복호화 → 평문을 m_recvBuffer에 적재
        // (암호문은 이미 Dequeue로 비웠으므로 m_recvBuffer를 재활용)
        while ((plainLen = SSL_read(session._ssl, plainBuf, sizeof(plainBuf))) > 0)
        {
            session.m_recvBuffer.Enqueue(plainBuf, plainLen);
        }

        int sslErr = SSL_get_error(session._ssl, plainLen);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_NONE)
        {
            Disconnect(session.m_SeqID);
            return;
        }

        // 복호화된 평문에서 게임 패킷 언마샬링
        stHeader header;
        ull SessionID = session.m_SeqID;
        short msgCount = 0;

        while (session.m_recvBuffer.Peek(&header, headerSize) == headerSize)
        {
            msgCount++;
            if (msgCount >= 150)
            {
                Disconnect(session.m_SeqID);
                return;
            }
            if (header.sDataLen >= max_MsgLen)
            {
                Disconnect(session.m_SeqID);
                return;
            }
            if (session.m_recvBuffer.GetUseSize() < header.sDataLen + headerSize)
                break;

            // CMessage 생성 (CreateMessage가 private이므로 인라인)
            CMessage *msg = static_cast<CMessage *>(_MsgPool.Alloc());
            ringBufferSize deQsize = session.m_recvBuffer.Dequeue(
                msg->_frontPtr, header.sDataLen + headerSize);
            msg->_rearPtr = msg->_frontPtr + deQsize;

            if (header.sDataLen + headerSize != deQsize)
            {
                _MsgPool.Release(msg);
                Disconnect(session.m_SeqID);
                return;
            }

            InterlockedExchange(&msg->ownerID, SessionID);
            msg->_frontPtr = msg->_frontPtr + headerSize;

            OnRecv(SessionID, msg);
        }
    }
    RecvPacket(session);
}

void SSL_CLoginServer::SendComplete(clsSession &session, DWORD transferred)
{

    std::lock_guard<SharedMutex> lock(session._srwSSL);

    ringBufferSize useSize;

    DWORD bufCnt;
    int send_retval = 0;

    WSABUF wsaBuf[500];
    DWORD LastError;
    // LONG64 beforeTPS;

    ull local_IoCount;
    CMessage *msg;

    //OnSend(session.m_SeqID);
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
        // 1단계: 평문 메시지들을 SSL_write로 암호화
        while (session.m_sendBuffer.Pop(msg))
        {
            int plainLen = (int)(msg->_rearPtr - msg->_frontPtr);
            SSL_write(session._ssl, msg->_frontPtr, plainLen);
            _MsgPool.Release(msg); // 평문 원본은 여기서 해제
        }

        // 2단계: wbio에서 암호문을 꺼내서 새 CMessage에 담기
        int pending = BIO_pending(session._wbio);
        while (pending > 0)
        {
            CMessage *cipherMsg = static_cast<CMessage *>(_MsgPool.Alloc());
            int readSize = min(pending, (int)CMessage::bufferSize);
            int n = BIO_read(session._wbio, cipherMsg->_frontPtr, readSize);
            cipherMsg->_rearPtr = cipherMsg->_frontPtr + n;

            wsaBuf[bufCnt].buf = cipherMsg->_frontPtr;
            wsaBuf[bufCnt].len = (ULONG)n;
            session.m_sendOverlapped.msgs[bufCnt++] = cipherMsg;

            if (bufCnt == 500)
                break;
            pending = BIO_pending(session._wbio);
        }
    }

    session.m_sendOverlapped.msgCnt = bufCnt;
    // arrTPS[SendTPSidx] += bufCnt;

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