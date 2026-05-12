#include "Session.h"

namespace network
{
    Session::Session()
        : mSock(INVALID_SOCKET),
          mSessionID(0),
          mIOcnt(0),
          mLive(0)
    {
        mAcceptBuf = new char[(sizeof(SOCKADDR_IN) + 16) * 2]();
        mAcceptOv = new AcceptOv(*this);
        mRecvOv = new RecvOv();
        mSendOv = new SendOv();
        mReleaseOv = new ReleaseOv();
        mRecvBuffer = new utility::RingBuffer(2048);
    }

    Session::~Session()
    {
        delete[] mAcceptBuf;

        delete mAcceptOv;
        delete mRecvOv;
        delete mSendOv;
        delete mReleaseOv;

        delete mRecvBuffer;
    }
}; // namespace network
