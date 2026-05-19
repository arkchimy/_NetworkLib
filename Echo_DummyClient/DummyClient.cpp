#include "DummyClient.h"
#include "../EchoServer/PacketCommon.h"
#include "Message.h"
#include <iomanip>
#include <iostream>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

MyWsaData mywsadata;

constexpr int PACKET_BATCH = 100;  // 한 루프당 패킷 수
constexpr int ECHO_LOOP_CNT = 10;  // N번 루프 후 재연결
constexpr int ATTACK_INTERVAL = 5; // N번 재연결마다 1번 공격
constexpr __int16 MAX_STR_LEN = 1000;

// partial recv 처리
static bool safeRecv(SOCKET sock, char *buf, int size)
{
    int total = 0;
    while (total < size)
    {
        int ret = recv(sock, buf + total, size - total, 0);
        if (ret <= 0)
            return false;
        total += ret;
    }
    return true;
}

DummyClient::DummyClient(int userCnt)
    : mServerIP(nullptr), mServerPort(0), mUserCnt(userCnt)
{
    mStringVec.reserve(PACKET_BATCH);
    for (int i = 0; i < PACKET_BATCH; ++i)
    {
        std::string str;
        int len = rand() % MAX_STR_LEN + 1;
        for (int j = 0; j < len; ++j)
            str += (char)(rand() % 255 + 1);
        mStringVec.push_back(str);
    }
}

void DummyClient::Start(const char *addr, __int16 port)
{
    mServerIP = addr;
    mServerPort = port;

    mMonitorThread = std::thread(&DummyClient::monitorThread, this);

    mThreadVec.resize(mUserCnt);
    for (int i = 0; i < mUserCnt; ++i)
        mThreadVec[i] = std::thread(&DummyClient::clientThread, this);
}

SOCKET DummyClient::connectToServer()
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return INVALID_SOCKET;

    SOCKADDR_IN addr{0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mServerPort);
    inet_pton(AF_INET, mServerIP, &addr.sin_addr);

    if (connect(sock, (const sockaddr *)&addr, sizeof(addr)) != 0)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

bool DummyClient::echoLoop(SOCKET sock)
{
    char recvBuf[MAX_STR_LEN + 1];

    for (int loop = 0; loop < ECHO_LOOP_CNT; ++loop)
    {
        utility::Message *msg[PACKET_BATCH];
        for (int i = 0; i < PACKET_BATCH; i++)
            msg[i] = new utility::Message();

        // 전송
        for (int i = 0; i < PACKET_BATCH; i++)
        {
            __int16 strLen = (__int16)mStringVec[i].size();
            *msg[i] << (__int16)(strLen + 4);
            *msg[i] << (__int8)0xFF;
            *msg[i] << static_cast<__int16>(ePacketType::CS_ECHO_REQ);
            *msg[i] << strLen;
            msg[i]->PutData(&mStringVec[i][0], strLen);
            send(sock, msg[i]->GetFrontPtr(), (int)msg[i]->GetUseSize(), 0);
            mSendCnt.fetch_add(1, std::memory_order_relaxed);
        }

        // 수신 & 검증
        for (int i = 0; i < PACKET_BATCH; i++)
        {
            __int16 recvLen, recvType, recvStrLen;
            __int8 recvRK;

            bool ok = safeRecv(sock, (char *)&recvLen, sizeof(__int16)) && safeRecv(sock, (char *)&recvRK, sizeof(__int8)) && safeRecv(sock, (char *)&recvType, sizeof(__int16)) && safeRecv(sock, (char *)&recvStrLen, sizeof(__int16)) && safeRecv(sock, recvBuf, recvStrLen);

            if (!ok)
            {
                mForcedDisconnectCnt.fetch_add(1, std::memory_order_relaxed);
                for (int j = 0; j < PACKET_BATCH; j++)
                    delete msg[j];
                return false;
            }

            mRecvCnt.fetch_add(1, std::memory_order_relaxed);

            // 원본과 비교
            __int16 sentLen, sentType, sentStrLen;
            __int8 sentRK;
            *msg[i] >> sentLen >> sentRK >> sentType >> sentStrLen;

            recvBuf[mStringVec[i].size()] = '\0';
            if (recvLen != sentLen || recvType != static_cast<__int16>(ePacketType::SC_ECHO_RES) || recvStrLen != sentStrLen || mStringVec[i].compare(recvBuf) != 0)
            {
                mErrCnt.fetch_add(1, std::memory_order_relaxed);
                __debugbreak();
            }
        }

        for (int i = 0; i < PACKET_BATCH; i++)
            delete msg[i];
    }
    return true;
}

void DummyClient::attackLoop(SOCKET sock)
{
    // 공격: Len = -1 (NetworkLib에서 <= 0 →disconnectSession)
    utility::Message msg;
    __int16 badLen = -1;
    __int16 strLen = 4;
    msg << badLen;
    msg << (__int8)0xFF;
    msg << static_cast<__int16>(ePacketType::CS_ECHO_REQ);
    msg << strLen;
    char dummy[4] = "AAA";
    msg.PutData(dummy, strLen);
    send(sock, msg.GetFrontPtr(), (int)msg.GetUseSize(), 0);
    mAttackCnt.fetch_add(1, std::memory_order_relaxed);

    // 서버가 끊어야 정상 — 3초 타임아웃
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    char buf[1];
    int ret = recv(sock, buf, 1, 0);
    if (ret != 0)
    {
        // 서버가 끊지 않음 →공격 감지 실패 (버그)
        mErrCnt.fetch_add(1, std::memory_order_relaxed);
    }
}

void DummyClient::clientThread()
{
    int reconnectCount = 0;

    while (1)
    {
        SOCKET sock = connectToServer();
        if (sock == INVALID_SOCKET)
        {
            Sleep(100);
            continue;
        }

        mReconnectCnt.fetch_add(1, std::memory_order_relaxed);

        if (reconnectCount % ATTACK_INTERVAL == 0)
            attackLoop(sock);
        else
            echoLoop(sock);

        closesocket(sock);
        reconnectCount++;
    }
}

void DummyClient::monitorThread()
{
    timeBeginPeriod(1);
    constexpr int interval = 1000;
    DWORD nextTime = timeGetTime() + interval;

    thread_local __int64 beforeSend = 0;
    thread_local __int64 beforeRecv = 0;

    while (1)
    {
        DWORD now = timeGetTime();
        if (nextTime <= now)
        {
            __int64 curSend = mSendCnt.load();
            __int64 curRecv = mRecvCnt.load();

            std::cout
                << std::setw(22) << "SendTPS          : " << std::setw(8) << curSend - beforeSend << "\n"
                << std::setw(22) << "RecvTPS          : " << std::setw(8) << curRecv - beforeRecv << "\n"
                << std::setw(22) << "Reconnect        : " << std::setw(8) << mReconnectCnt.load() << "\n"
                << std::setw(22) << "ForcedDisconnect : " << std::setw(8) << mForcedDisconnectCnt.load() << "\n"
                << std::setw(22) << "AttackSent       : " << std::setw(8) << mAttackCnt.load() << "\n"
                << std::setw(22) << "ErrCnt           : " << std::setw(8) << mErrCnt.load() << "\n"
                << "----------------------------------------\n";

            beforeSend = curSend;
            beforeRecv = curRecv;
            nextTime += interval;
        }
        else
        {
            Sleep(nextTime - now);
        }
    }
    timeEndPeriod(1);
}