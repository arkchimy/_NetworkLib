#include "DummyClient.h"
#include "../EchoServer/PacketCommon.h"
#include "Message.h"

MyWsaData mywsadata;

constexpr __int16 max_len = 2000 - 7;

DummyClient::DummyClient(int userCnt)
    : mServerIP(nullptr),
      mServerPort(0),
      mUserCnt(userCnt)
{

    mString_vec.reserve(100);
    for (int strSet = 0; strSet < 100; ++strSet)
    {
        std::string str;
        int len =rand() % 1000;
        for (int i = 0; i < max_len; ++i)
        {
            str += rand() % 255 + 1;
        }
        mString_vec.push_back(str);
    }
}

void DummyClient::Start(const char *addr, __int16 port)
{
    mServerIP = addr;
    mServerPort = port;

    mThread_vec.resize(mUserCnt);

    for (__int8 idx = 0; idx < mUserCnt; ++idx)
    {
        mThread_vec[idx] = std::thread(&DummyClient::clientThread, this);
    }
}

void DummyClient::clientThread()
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    RT_ASSERT(sock != INVALID_SOCKET);

    SOCKADDR_IN addr{0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(mServerPort);
    inet_pton(AF_INET, mServerIP, &addr.sin_addr);
    // 오류가 발생하지 않으면 connect 는 0을 반환합니다.

    DWORD retval = connect(sock, (const sockaddr *)&addr, sizeof(addr));
    RT_ASSERT(retval == 0);

    char buffer[max_len + 1];

    while (1)
    {

        utility::Message *msg[100];
        for (int i = 0; i < 100; i++)
        {
            msg[i] = new utility::Message();
        }

        for (int i = 0; i < 100; i++)
        {
            *msg[i] << static_cast<__int16>(mString_vec[i].size() + 4);
            *msg[i] << static_cast<__int8>(0xFF);
            *msg[i] << static_cast<__int16>(ePacketType::CS_ECHO_REQ); // TYPE
            *msg[i] << static_cast<__int16>(mString_vec[i].size());
            msg[i]->PutData(&mString_vec[i][0], (int)mString_vec[i].size());
        }

        // 수신 버퍼에 복사를 하고 리턴.
        for (int i = 0; i < 100; i++)
        {
            send(sock, msg[i]->GetFrontPtr(), (int)msg[i]->GetUseSize(), 0);
        }
        for (int i = 0; i < 100; i++)
        {
            __int16 recvLen;
            __int8 recvRK;
            __int16 recvType;
            __int16 recvStrlen;


            recv(sock, (char *)&recvLen, sizeof(__int16), 0); // header
            recv(sock, (char *)&recvRK, sizeof(__int8), 0);   // header
            recv(sock, (char *)&recvType, sizeof(__int16), 0); // header
            recv(sock, (char *)&recvStrlen, sizeof(__int16), 0); // header

            recv(sock, buffer, (int)recvStrlen, 0);
        
            {
                __int16 len;
                __int8 RK;
                __int16 type;
                __int16 strLen;

                *msg[i] >> len;
                *msg[i] >> RK;
                *msg[i] >> type;
                *msg[i] >> strLen;
                if (len != recvLen)
                {
                    __debugbreak();
                }
                if (recvType != static_cast<__int16>(ePacketType::SC_ECHO_RES))
                {
                    __debugbreak();
                }
                if (strLen != recvStrlen)
                {
                    __debugbreak();
                }
            }
            buffer[mString_vec[i].size()] = '\0';
            // 문자열이 다른지 체크.
            if (mString_vec[i].compare(buffer) != 0)
            {
                __debugbreak();
            }

        }
        for (int i = 0; i < 100; i++)
        {
            delete msg[i];
        }
    }
}
