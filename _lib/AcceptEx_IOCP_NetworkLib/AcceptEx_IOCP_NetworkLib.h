#pragma once

#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#pragma once

#include <shared_mutex>
#include <stack>
#include <thread>

#include "Session.h"
#include "utility/Message.h"

namespace network
{
constexpr short RELEASE_IOCOUNT = static_cast<short>(1 << 14);
class NetworkLib
{
  public:
    NetworkLib();
    virtual ~NetworkLib() = default;

  protected:
    void start();
    virtual void onAccept(SOCKADDR_IN& addr, const SeqAndIdx &sessionID) = 0;
    virtual void onRecv(utility::Message *msg) = 0;
    virtual void onSend(utility::Message *msg) = 0;
    virtual void onRelease(const SeqAndIdx &sessionID) = 0;

    void sendPost(const SeqAndIdx& sessionID, utility::Message &msg);
  public:
    __int64 GetAcceptCount() const { return mAcceptCnt; }
    __int64 GetSendCount() const { return mSendCnt; }
    __int64 GetRecvCount() const { return mRecvCnt; }
    __int64 GetDisConnectCount() const { return mDisConnect; }
    __int64 GetSessionCount() const { return mSessionCnt; }
  protected:
    void disconnectSession(const SeqAndIdx& sessionID);

  private:
    void workerThread();

    void registerAcceptEx();
    void completeAcceptEx(Session &session);

    void registerRecv(Session &session);
    void completeRecv(Session &session, DWORD transferred);

    void registerSend(Session &session);
    void completeSend(Session &session);

    void completeRelease(Session &session);

    void checkAndHandleIoError(Session &session, const int lastError);

    bool stackSessionIdx_Pop(ull &out);
    void stackSessionIdx_Push(const ull &input);

    bool sessionLock(const SeqAndIdx& sessionID);
    void sessionUnLock(const SeqAndIdx& sessionID);

  private:
    std::thread mWorkerThreads[CONFIG_WORKER_THREAD_CNT];
    SOCKET mListenSock;

    HANDLE mHcp; // iocpHandle
    Session mSessions[CONFIG_SESSION_MAX];

    std::stack<seqAddrType> mStackSessionIdx;
    std::shared_mutex mStackMutex;

    seqAddrType mSeqID = -1;

    __int64 mAcceptCnt;
    __int64 mSendCnt;
    __int64 mRecvCnt;
    __int64 mDisConnect;
    __int64 mSessionCnt;
};

} // namespace network
