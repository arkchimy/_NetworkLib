#include "clsSession.h"

#include "../utility/CSystemLog/CSystemLog.h"
#include "../utility/SerializeBuffer_exception/SerializeBuffer_exception.h"


clsSession::clsSession(SOCKET sock)
    : m_sock(sock), _sslState(SSLState::None), m_blive(true)
{
}

clsSession::~clsSession()
{
    closesocket(m_sock);
    __debugbreak();
}

void clsSession::Release(CLeakDetectPool<CMessage>& pool)
{
    CMessage *msg;
    while (m_sendBuffer.Pop(msg))
    {
        pool.Release(msg);
    }

    for (DWORD i = 0; i < m_sendOverlapped.msgCnt; i++)
    {
        pool.Release(m_sendOverlapped.msgs[i]);
    }
    m_sendOverlapped.msgCnt = 0;

    {
        ZeroMemory(&m_recvOverlapped, sizeof(OVERLAPPED));
        ZeroMemory(&m_sendOverlapped, sizeof(OVERLAPPED));
    }
    m_recvBuffer.ClearBuffer();
    _sslState = SSLState::None;
    SSL_free(_ssl);
    // free에서 알아서 해줌
 /*   BIO_free(_rbio);
    BIO_free(_wbio);*/
}
