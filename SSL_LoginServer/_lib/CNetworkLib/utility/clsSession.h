#pragma once
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include <list>

#pragma comment(lib, "winmm")
#pragma comment(lib, "ws2_32")

#include "../utility/MT_CRingBuffer/MT_CRingBuffer.h"
#include "../utility//CLockFreeQueue/CLockFreeQueue.h"

  #include "SerializeBuffer_exception/SerializeBuffer_exception.h"  // CMessage
  #include "CLeakDetectPool/CLeakDetectPool.h"                      // CLeakDetectPool
  #include "DeadLockGuard/DeadLockGuard_lib.h"                      // SharedMutex

#include <openssl/err.h>
#include <openssl/ssl.h>
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#define SENDWSABUF_MAX 100
using ull = unsigned long long;

enum class Job_Type : BYTE
{
    Recv,
    Send,
    // 해당 msg의 완료통지로 세션 끊김 절차.
    ReleasePost,
    Post,
    DBPost,
    MAX,
};
// _mode 판단을 stOverlapped 기준으로 하므로 첫 멤버변수 _mode 로 할것. 
struct stOverlapped : OVERLAPPED
{
    Job_Type _mode;
    stOverlapped(Job_Type m) : _mode(m) {}
};

struct stSendOverlapped : stOverlapped
{
    DWORD msgCnt = 0;
    CMessage *msgs[SENDWSABUF_MAX]{};
    stSendOverlapped() : stOverlapped(Job_Type::Send) {}
};

struct stPostOverlapped : stOverlapped
{
    CMessage *msg = nullptr;
    stPostOverlapped() : stOverlapped(Job_Type::Post) {}
};

struct stReleaseOverlapped : stOverlapped
{
    stReleaseOverlapped() : stOverlapped(Job_Type::ReleasePost) {}
};

struct stDBOverlapped : stOverlapped
{
    CMessage *msg = nullptr;
    stDBOverlapped() : stOverlapped(Job_Type::DBPost) {}
};


enum class SSLState
{
    None,
    Established,
    MAX,
};

class clsSession
{
  public:
    clsSession() = default;
    clsSession(SOCKET sock);
    ~clsSession();

    void Release(CLeakDetectPool<CMessage>& pool);

    SOCKET m_sock = 0;

    stOverlapped m_recvOverlapped = stOverlapped(Job_Type::Recv);
    stSendOverlapped m_sendOverlapped;
    stReleaseOverlapped m_releaseOverlapped;

    CLockFreeQueue<CMessage *> m_sendBuffer;
    CRingBuffer m_recvBuffer; 

    ull m_SeqID = 0;
    ull m_ioCount = 0;
    ull m_blive = 0;
    ull m_flag = 0; // SendFlag

    SSL *_ssl = nullptr;
    BIO *_rbio = nullptr;
    BIO *_wbio = nullptr;
    SSLState _sslState = SSLState::None;
    SharedMutex _srwSSL;
};
