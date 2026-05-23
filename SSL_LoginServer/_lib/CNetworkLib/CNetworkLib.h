#pragma once

#include "RPC/Proxy.h"
#include "RPC/Stub.h"

#include "utility/clsSession.h"
#include "utility/stHeader.h"

#pragma comment(lib, "winmm")
#pragma comment(lib, "ws2_32")

#include <vector>
#include "utility/SerializeBuffer_exception/SerializeBuffer_exception.h"

#include "utility/CLockFreeQueue/CLockFreeQueue.h"
#include "utility/CLockFreeStack/CLockFreeStack.h"
#include "utility/CSystemLog/CSystemLog.h"

#include "utility/CLeakDetectPool/CLeakDetectPool.h"

#include "utility/Parser/Parser.h"


#include "WinAPI/Atomic.h"
#include "WinAPI/WinThread.h"
#include "utility/DeadLockGuard/DeadLockGuard_lib.h"
#include "utility/Profiler_MultiThread/Profiler_MultiThread.h"

using ull = unsigned long long;
enum 
{
    MAX_SESSION = 7000,
    MAX,
};

struct stWSAData
{
    // main에서 선언
  public:
    stWSAData()
    {
        WSAData wsa;
        DWORD wsaStartRetval;

        wsaStartRetval = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (wsaStartRetval != 0)
        {
            __debugbreak();
        }
    }
    ~stWSAData()
    {
        WSACleanup();
    }
};

class CLanServer : public Stub, public Proxy
{
    friend class Stub;
    friend class Proxy;

  private:
    void WorkerThread();
    void AcceptThread();

  public:
    CLanServer(bool EnCoding = false);
    virtual ~CLanServer();

    // 오픈 IP / 포트 / 제로카피 여부 /워커스레드 수 (생성수, 러닝수) / 나글옵션 / 최대접속자 수
    virtual BOOL Start(const wchar_t *bindAddress, short port, int ZeroCopy, int WorkerCreateCnt, int maxConcurrency, int useNagle, int maxSessions);

    // SignalOnForStop의 이벤트를 대기함.
    virtual void Stop();

    // Player가 0으로 떨어졌을때 반드시 호출 해줘야 함.
    virtual void SignalOnForStop();

    bool Disconnect(const ull SessionID);
    void SendPacket(ull SessionID, struct CMessage *msg, BYTE SendType,
                    std::vector<ull> *pIDVector = nullptr, size_t wVecLen = 0);

    clsSession &GetSession(ull SessionID)
    {
        if (int(SessionID >> 47) > (_MaxSessions - 1))
        {
            // sessionID의 오염.  메모리 오염된 경우 임.
            __debugbreak();
        }
        return sessions_vec[SessionID >> 47];
    }

    // 호출자가 1초마다 호출을 책임져야함.
    int getAcceptTPS();
    // 호출자가 1초마다 호출을 책임져야함.
    int getRecvMessageTPS();
    // 호출자가 1초마다 호출을 책임져야함.
    int getSendMessageTPS();
    int getMaxSessionCnt() { return _MaxSessions; }

    LONG64 GetSessionCount() const;
    virtual LONG64 GetPlayerCount() { return 0; } // Contents에서 구현하기.
    LONG64 Get_IdxStack() const;

    ull getTotalAccept() const { return m_TotalAccept; }
    ull getNetworkMsgCount() const { return m_NetworkMsgCount; }
    bool getServerisOn() { return bOn; }
    bool getDisConnectCnt() { return iDisCounnectCount; }

  private:
    void CancelIO_Routine(const ull SessionID);
    void DecrementIoCountAndMaybeDeleteSession(clsSession &session);
    CMessage *CreateMessage(class clsSession &session, struct stHeader &header) ;

    // void RecvComplete(class clsSession *const session, DWORD transferred);
    virtual void RecvComplete(class clsSession &session, DWORD transferred);
    virtual void SendComplete(class clsSession &session, DWORD transferred);
    virtual void ReleaseComplete(clsSession &session);

    bool SessionLock(ull SessionID);   // 내부에서 IO를 증가시켜 안전을 보장함.
    void SessionUnLock(ull SessionID); // 반환형 쓸때가 없음.

    void Unicast(ull SessionID, CMessage *msg, LONG64 Account = 0);
    void BroadCast(ull SessionID, CMessage *msg, std::vector<ull> *pIDVector, size_t wVecLen);

  protected:
    void RecvPacket(class clsSession &session);

  protected:
    void WSASendError(const DWORD LastError, const ull SessionID);
    void WSARecvError(const DWORD LastError, const ull SessionID);

    // 실제로 Release를 하는 Overalpped을 PQCS하는 함수.
    void ReleaseSession(ull SessionID);

  private:
    virtual bool OnAccept(ull SessionID, SOCKADDR_IN &addr) = 0;
    virtual void OnRecv(ull SessionID, struct CMessage *msg) = 0;
    virtual void OnSend(ull SessionID, struct CMessage *msg) = 0;
    virtual void OnRelease(ull SessionID) = 0;

  private:
    clsSession sessions_vec[MAX_SESSION];

    int _MaxSessions = 0;

    // SignalOnForStop에서 사용할 이벤트객체
    HANDLE hReadyForStopEvent = INVALID_HANDLE_VALUE;

    SOCKET m_listen_sock = INVALID_SOCKET;

  protected:
    HANDLE m_hIOCP = INVALID_HANDLE_VALUE;

  private:
    std::vector<WinThread> m_hWorkerThread;

    WinThread m_hAccept;

    bool bOn = false;
    LONG64 m_SessionCount = 0;
    ull iDisCounnectCount = 0;

  protected:
    bool bZeroCopy = false;
    int bNoDelay = false;

    CLockFreeStack<ull> m_SessionIdxStack; // 반환된 Idx를 Stack형식으로
    int m_WorkThreadCnt = 0;               // MonitorThread에서 WorkerThread의 갯수를 알기위한 변수.

    std::vector<LONG64> arrTPS;
    LONG64 m_RecvTPS = 0;
    ull m_TotalAccept = 0;

    LONG64 m_AllocMsgCount = 0;
    LONG64 m_NetworkMsgCount = 0;

    int m_AllocLimitCnt = 10000;

    bool bEnCording = false;
    int headerSize = 0;

    // SendBuffer가 가득차는 역할.
    int SendBufferLimit = 5000;
    // 메세지 하나의 최대크기 제한.
    int max_MsgLen = 5000;

  protected:
    CLeakDetectPool<CMessage> _MsgPool;

  private:
    stWSAData _wsadata;
};
