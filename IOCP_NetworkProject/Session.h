#pragma once
#include <queue>
#include <mutex>

#include "NetConfig.h"
#include "MyOverlapped.h"
#include "utility/RingBuffer.h"
#include "utility/Message.h"

namespace network
{
using ull = unsigned long long;
using seqAddrType = long long;

struct SeqAndIdx
{
    union
    {
        struct
        {
            LONG64 Idx : 17; // sessions 의 idx
            LONG64 Seq : 47; // session의 고유성을 보장하기위한 seqNumber
        };
        LONG64 Value;
    };
};

class Session
{
    friend class NetworkLib;

  public:
    Session();
    ~Session();
    void ReleaseSession();
  private:
    SOCKET mSock;
    SeqAndIdx mSessionID;

    char *mAcceptBuf;

    AcceptOv *mAcceptOv;
    RecvOv *mRecvOv;
    SendOv *mSendOv;
    ReleaseOv *mReleaseOv;

    // TODO : Interlock계열의 크기에따른 성능변화 측정.
    short mIOcnt;
    char mLive;
    utility::RingBuffer *mRecvBuffer;
    std::queue<utility::Message *> mSendQ;
    std::mutex mSendLock;
};

} // namespace network

/*
   // INFO : AcceptBuffer의 설명
   -------------   char mAcceptBuf[(sizeof(SOCKADDR_IN) + 16)  * 2];      -------------
  //AcceptEx의 buffer는 두 역할입니다:                                                                                      - 로컬 주소 저장
  //- 원격(클라이언트) 주소 저장
  //크기는 sizeof(SOCKADDR_IN) 만으로는 부족합니다. AcceptEx가 내부적으로 +16바이트 여유를 요구합니다

  //// 필요한 버퍼 크기
  //// dwReceiveDataLength = 0 이므로 주소 두 개만
  //(sizeof(SOCKADDR_IN) + 16) * 2   // = 64바이트
*/