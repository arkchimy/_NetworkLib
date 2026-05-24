#pragma once
#include <openssl/err.h>
#include <openssl/ssl.h>

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#include "../_lib/CNetworkLib/CNetworkLib.h"
#include <unordered_map>

// 로그인 패킷이 와서 확인된 유저.
struct stAuthSession
{
    void Init()
    {
        ZeroMemory(&_addr, sizeof(SOCKADDR_IN));
        ZeroMemory(&_sessionID, sizeof(ull));
        ZeroMemory(&_lastTime, sizeof(DWORD));
    }

    DWORD _lastTime;
    SOCKADDR_IN _addr;
    ull _sessionID;

};

// 연결만 된 Session 
struct stWaitSession
{
    void Init() 
    {
        ZeroMemory(&_addr, sizeof(SOCKADDR_IN));
        ZeroMemory(&_sessionID, sizeof(ull));

        ZeroMemory(&_lastTime, sizeof(DWORD));
    }

    DWORD _lastTime;
    ull _sessionID;
    SOCKADDR_IN _addr;

};

// DB연동서버
enum
{
    IP_LEN = 16,
    DBName_LEN = 16,
    schema_LEN = 16,
    ID_LEN = 20,
    Password_LEN = 20,
};
enum en_PACKET_CS_LOGIN_RES_LOGIN : BYTE
{
    dfLOGIN_SESSION_FAIL = 0, // 세션오류
    dfLOGIN_DB_OK = 1,        // 성공
    dfLOGIN_SESSION_GAME = 2, // 게임중

    dfLOGIN_DB_ACCOUNT_MISS = 3,  // account 테이블에 AccountNo 없음
    dfLOGIN_DB_ID_MISS = 4,       // account 테이블 ID 불일치
    dfLOGIN_DB_PASSWORD_MISS = 5, // account 테이블 PW 불일치

    dfLOGIN_STATUS_NOSERVER = 6, // 서비스중인 서버가 없음.

};
class SSL_CLoginServer : public CLanServer
{
  public:
    SSL_CLoginServer();
    void Start();

  public:
      // 컨텐츠
    virtual void REQ_LOGIN(ull SessionID, CMessage *msg, WCHAR *ID, WCHAR *PW,
        WORD wType = en_PACKET_CS_LOGIN_REQ_AUTH, BYTE bBroadCast = false, std::vector<ull> *pIDVector = nullptr, size_t wVectorLen = 0);
    
    virtual void REQ_ECHO(ull SessionID, CMessage *msg, WCHAR *str, int strLen,
        WORD wType = en_PACKET_RES_ECHO, BYTE bBroadCast = false, std::vector<ull> *pIDVector = nullptr, size_t wVectorLen = 0); 
  private:
    void AuthThread();
    void DBworkerThread();
    void MonitorThread();

    void HeartBeat();
    void DB_PacketProc(CMessage *msg);
    void PacketProc_LoginAuth(ull SessionID, CMessage *msg);
    BYTE WaitDB(WCHAR *ID, WCHAR *Password, int& outAccountNo);
    void makeToken(char* buffer);
  private:
    virtual bool OnAccept(ull SessionID, SOCKADDR_IN &addr);
    virtual void OnRecv(ull SessionID, struct CMessage *msg);
    virtual void OnSend(ull SessionID, struct CMessage * msg);
    virtual void OnRelease(ull SessionID);

    // network에서 언마샤랑 하기때문에 이를 회피.
  private:
    virtual void RecvComplete(class clsSession &session, DWORD transferred) override;
    virtual void SendComplete(class clsSession &session, DWORD transferred) override;
  

  private:
    // ObjectPool
    CLeakDetectPool<stWaitSession> _waitSessionPool;
    CLeakDetectPool<stAuthSession> _authSessionPool;
    CLeakDetectPool<stDBOverlapped> dbOverlapped_pool;

    // 인증을 대기중인 세션
    std::unordered_map<ull, stWaitSession *> _SessionID_wait_hash;
    SharedMutex _srw_Wait;

    // 인증된 유저
    std::unordered_map<ull, stAuthSession *> _SessionID_auth_hash;
    SharedMutex _srw_Auth;

    // SSL
    SSL_CTX *_ctx;

  private:
    // AuthThread
    WinThread _hAuthThread;
    HANDLE _hAuthEvent;
    CLockFreeQueue<CMessage *> _AuthThreadQueue;

    // MonitorThread
    WinThread _hMonitorThread;
    LONG64 _acceptTPS = 0;
     LONG64 _loginTryTPS = 0;
    LONG64 _loginOK = 0;
    LONG64 _loginFail = 0;
     LONG64 _recvTPS = 0;
     LONG64 _sendTPS = 0;
     LONG64 _DB_ReqCnt = 0;



  private:
    // Paraer
    WCHAR GameServerIP[16] = L"0.0.0.0";
    USHORT GameServerPort = 0;

    WCHAR ChatServerIP[16]{};
    USHORT ChatServerPort = 6000;

    // DB연동서버
    HANDLE _hDBIocp;
    std::vector<WinThread> hDBThread_vec;

    char AccountDB_IPAddress[IP_LEN];
    USHORT DBPort;

    char DBName[DBName_LEN];
    char schema[schema_LEN];

    char DBuser[ID_LEN];
    char password[Password_LEN];

    WCHAR RedisIpAddress[IP_LEN];

  private:
    bool _bOn;
};
