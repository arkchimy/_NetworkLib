#include "pch.h"
#include "Session.h"

namespace network
{
Session::Session()
    : mSock(INVALID_SOCKET),
      mSessionID(0),
      mIOcnt(0),
      mLive(0),
      mSendFlag(0),
      mSenqQSize(0)
{
    mAcceptBuf = MY_NEW char[(sizeof(SOCKADDR_IN) + 16) * 2]();
    mAcceptOv = MY_NEW AcceptOv(this);
    mRecvOv = MY_NEW RecvOv();
    mSendOv = MY_NEW SendOv();
    mReleaseOv = MY_NEW ReleaseOv();
    mRecvBuffer = MY_NEW utility::RingBuffer(CONFIG_RINGBUFFER_SIZE);
}

Session::~Session()
{
    MY_DELETE mAcceptBuf;

    MY_DELETE mAcceptOv;
    MY_DELETE mRecvOv;
    MY_DELETE mSendOv;
    MY_DELETE mReleaseOv;

    MY_DELETE mRecvBuffer;
}
void Session::EnQueueMsg(utility::Message &msg)
{
    std::lock_guard<std::shared_mutex> lock(mSendQlock);
    mSendQ.push(&msg);
    _InterlockedIncrement16(&mSenqQSize);
}
utility::Message *Session::DeQueueMsgOrNull()
{
    std::lock_guard<std::shared_mutex> lock(mSendQlock);

    if (mSendQ.empty())
    {
        return nullptr;
    }
    utility::Message * msg = mSendQ.front();
    mSendQ.pop();
    _InterlockedDecrement16(&mSenqQSize);

    return msg;
}
void Session::ReleaseSession()
{
    RT_ASSERT(InterlockedExchange8(&mLive, 0) == 0);
    utility::Message *msg = DeQueueMsgOrNull();
    while (msg != nullptr)
    {
        MY_DELETE msg;
        msg = DeQueueMsgOrNull();
    }
    RT_ASSERT(mSenqQSize == 0);

    for (int cnt = 0; cnt < mSendOv->mMsgCnt; ++cnt)
    {
        utility::Message *msg = static_cast<utility::Message *>(mSendOv->mSendMsgs[cnt]);
        MY_DELETE msg;
    }
    mSendOv->mMsgCnt = 0;
    mSendFlag = false;
    mRecvBuffer->ClearBuffer();

    closesocket(mSock);
}
}; // namespace network
