#pragma once
#include "Common.h"
#include "NetConfig.h"
namespace network
{
enum class eComplete
{
    COMPLETE_ACCEPT,
    COMPLETE_RECV,
    COMPLETE_SEND,
    COMPLETE_RELEASE,
    NONE,
};

class MyOverlapped : public OVERLAPPED
{
  public:
    MyOverlapped(const eComplete mode) : mMode(mode) {}
    const eComplete GetMode() const { return mMode; }

  private:
    const eComplete mMode;
};
class AcceptOv : public MyOverlapped
{
    friend class NetworkLib;
  public:
    AcceptOv(void *session)
        : MyOverlapped(eComplete::COMPLETE_ACCEPT),
          mSession(session) {}

  private:
    void *mSession;
};
class RecvOv : public MyOverlapped
{
  public:
    RecvOv()
        : MyOverlapped(eComplete::COMPLETE_RECV) {}
};
class SendOv : public MyOverlapped
{
    friend class NetworkLib;
    friend class Session;

  public:
    SendOv()
        : MyOverlapped(eComplete::COMPLETE_SEND) ,
          mSendMsgs{0},
          mMsgCnt(0)
    {}

  private:
    void *mSendMsgs[CONFIG_SEND_MESSAGE_MAXCOUNT];
    __int16 mMsgCnt;
};
class ReleaseOv : public MyOverlapped
{
  public:
    ReleaseOv()
        : MyOverlapped(eComplete::COMPLETE_RELEASE) {}
};
} // namespace network