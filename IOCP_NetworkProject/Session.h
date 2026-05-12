#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WS2tcpip.h>
#include <Windows.h>

#include "utility/RingBuffer.h"

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

    enum complete
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
        MyOverlapped(complete mode) : mMode(mode) {}
        inline complete GetMode() const {  return mMode; }
      protected:
        complete mMode = NONE;
    };  
    class Session;
    class AcceptOv : public MyOverlapped
    {
    friend class NetworkLib;
      public:
        AcceptOv(Session &session) : mSession(session), MyOverlapped(COMPLETE_ACCEPT) {}
      private:
        Session& mSession;
    };
    class RecvOv : public MyOverlapped
    {
      public:
        RecvOv() : MyOverlapped(COMPLETE_RECV) {}
    };
    class SendOv : public MyOverlapped
    {
      public:
        SendOv() : MyOverlapped(COMPLETE_SEND) {}
    };
    class ReleaseOv : public MyOverlapped
    {
      public:
        ReleaseOv() : MyOverlapped(COMPLETE_RELEASE) {}
    };



    class Session
    {
      friend class NetworkLib;

      public:
        Session();
        ~Session();
      private:
        SOCKET mSock;
        SeqAndIdx mSessionID;

        char* mAcceptBuf;

        AcceptOv* mAcceptOv;
        RecvOv* mRecvOv;
        SendOv* mSendOv;
        ReleaseOv* mReleaseOv;

        //TODO : Interlock계열의 크기에따른 성능변화 측정.
        short mIOcnt;
        char mLive;
        utility::RingBuffer* mRecvBuffer;
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