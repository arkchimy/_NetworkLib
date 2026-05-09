#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WS2tcpip.h>
#include <Windows.h>



namespace network
{
    using ull = unsigned long long;
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
        inline complete GetMode() const {  return mMode; }
      private:
        complete mMode = NONE;
    };  
    class AcceptOv : public MyOverlapped
    {

    };
    class RecvOv : public MyOverlapped
    {
  
    };
    class SendOv : public MyOverlapped
    {
     
    };
    class ReleaseOv : public MyOverlapped
    {
       
    };
    class Session
    {
      friend class NetworkLib;

      private:
        SOCKET mSock;
        ull mSessionID;

        char mAcceptBuf[(sizeof(SOCKADDR_IN) + 16) * 2];

        AcceptOv mAcceptOv;
        RecvOv mRecvOv;
        SendOv mSendOv;
        ReleaseOv mReleaseOv;

        BYTE mIOcnt;
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