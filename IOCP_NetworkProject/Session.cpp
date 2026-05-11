#include "Session.h"

network::Session::Session()
    : mSock(INVALID_SOCKET),
      mSessionID(0),
      mIOcnt(0)
{
}
