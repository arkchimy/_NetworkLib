#pragma once

#include <mutex>
#include <stack>
#include <thread>

#include "Session.h"
#include "utility/Message.h"

namespace network
{
class NetworkLib
{
  public:
    NetworkLib();
    virtual ~NetworkLib() = default;

  protected:
    virtual void onAccept(const ull &sessionID) = 0;
    virtual void onRecv(utility::Message* msg) = 0;
    virtual void onSend() = 0;
    virtual void onRelease() = 0;

  private:
    void workerThread();

    void registerAcceptEx();
    void completeAcceptEx(Session &session);

    void registerRecv(Session &session);
    void completeRecv(Session &session, DWORD transferred);

    void registerSend(Session& session);
    void completeSend(Session &session);

    void completeRelease(Session &session);

    void checkAndHandleIoError(Session &session, const int lastError);

    bool stackSessionIdx_Pop(ull &out);
    void stackSessionIdx_Push(const ull &input);

    void disconnectSession() {}; // 악의적인 Session 발견
  private:
    std::thread mWorkerThreads[CONFIG_WORKER_THREAD_CNT];
    SOCKET mListenSock;

    HANDLE mHcp; // iocpHandle
    Session mSessions[CONFIG_SESSION_MAX];

    std::stack<seqAddrType> mStackSessionIdx;
    std::mutex mStackMutex;

    seqAddrType mSeqID = -1;

    WsadataRAII wsadata;
};

} // namespace network
