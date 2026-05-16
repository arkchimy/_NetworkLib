#include "Session.h"

namespace network
{
    Session::Session()
        : mSock(INVALID_SOCKET),
          mSessionID(0),
          mIOcnt(0),
          mLive(0)
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
        MY_DELETE[] mAcceptBuf;

        MY_DELETE mAcceptOv;
        MY_DELETE mRecvOv;
        MY_DELETE mSendOv;
        MY_DELETE mReleaseOv;

        MY_DELETE mRecvBuffer;
    }
}; // namespace network
