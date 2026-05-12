#include "Session.h"

namespace network
{
    Session::Session()
        : mSock(INVALID_SOCKET),
          mSessionID(0),
          mIOcnt(0),
          mLive(0)
    {
        mAcceptOv = new AcceptOv();
        mRecvOv = new RecvOv();
        mSendOv = new SendOv();
        mReleaseOv = new ReleaseOv();
    }

    Session::~Session()
    {

    }
}; // namespace network
