#pragma once

#include <iostream>
#include <map>
#include <queue>
#include <unordered_map>

#include "AcceptEx_IOCP_NetworkLib/AcceptEx_IOCP_NetworkLib.h"
#include "ChattingServerConfig.h"

namespace network
{
using namespace utility;
enum class eRedisResult : __int8
{
    Redis_No_Data,
    Redis_Success,
};
struct Player
{
    Player()
        : Addr{0},
          SessionID{0},
          SeqNumber(0xfdfdfdfd),
          SessionFK('\xFF'),
          sectorX(0),
          sectorY(0),
          bAuth(false),
          lastTime(0),
          Nickname{0},
          tokenKey{0}
    {
    }
    SOCKADDR_IN Addr;
    SeqAndIdx SessionID;
    __int32 SeqNumber;
    __int8 SessionFK;
    __int8 sectorX;
    __int8 sectorY;
    bool bAuth;
    wchar_t Nickname[20];
    char tokenKey[20];
    DWORD lastTime;
    __int64 accountNo;
};
struct Sector
{
    std::shared_mutex mMutex;
    std::unordered_map<__int64, Player *> mPlayers;
};
class ChattingServer : public NetworkLib
{
  public:
    ChattingServer();
    friend std::ostream &operator<<(std::ostream &out, const ChattingServer &server);

  private:
    virtual void onAccept(const SOCKADDR_IN &addr, const SeqAndIdx &sessionID);
    virtual void onRecv(Message *msg);
    virtual void onSend(Message *msg);
    virtual void onRelease(const SeqAndIdx &sessionID);

    bool popContentsQ(Message **msg);
    void pushDeferredQ(Message &msg);
    bool popDeferredQ(Message **msg);

    Player *validatePlayerOrNull(Message &msg);
    bool isValidateSeqNumber(Message &msg, Player &player);

    void packetProc(Message &msg, Player &player);

    eRedisResult getFixedKeyFromRedis(const __int64 accountNo, Player &player) const;
    void moveSector(__int64 sessionID, const __int8 beforeX, const __int8 beforeY, const __int8 x, const __int8 y);
    void aroundLockAndSendMsg(__int16 x, __int16 y, wchar_t Nickname[20], __int16 MessageLen, wchar_t *const buffer);

    void makeLoginMessage(const __int8 FK, const eRedisResult &result, const __int32 seqNumber, const __int64 sessionID, Message &msg);
    void makeAuthMessage(const __int8 FK, const __int64 sessionID, const __int8 SectorX, const __int8 SectorY, Message &msg);
    void makeChatMessage(const __int8 FK, const __int64 sessionID, wchar_t Nickname[20], __int16 MessageLen, wchar_t *const buffer, Message &msg);

    void loginProc(Message &msg, Player &player);
    void authProc(Message &msg, Player &player);
    void moveSectorProc(Message &msg, Player &player);
    void chatMessageProc(Message &msg, Player &player);

  private:
    void contentsThread();
    void monitorThread();

  private:
    std::thread mContentsThread;
    std::thread mMonitorThread;

    HANDLE hEchoEvent;

    std::shared_mutex mQLock;
    std::queue<Message *> mContentsQ;

    std::shared_mutex mDeferredQLock;
    std::queue<Message *> mDeferredReleaseQ;

    std::shared_mutex mPlayerMapLock;
    std::map<__int64, Player *> mPlayerMap;

    size_t mUserCnt;
    Sector sectors[50][50];

    __int64 mContentsTPS;
    size_t mDeferredReleaseQSize;
    size_t mContentsQSize;
};
} // namespace network
