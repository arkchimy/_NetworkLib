#pragma once
enum contentsConfig
{
    CONTENTS_MSG_MAX_SIZE = 2000,
};
//------------------------------------------------------
// Chatting Server
//------------------------------------------------------
enum class ePacketType : __int16
{
    CS_LOGIN = 0,
    //{
    //  __int16    Type
    // 
    //  __int32 SeqNumber // 첫 메세지는 0xfdfdfdfd
    //  __int64 AccountNo
    //} 첫 메세지는 초기 고정키로 암호화 
    //  서버에서 Redis에서 AccountNo를 통해SessionKey 탐색 시도

    SC_LOGIN = 1,
    //{
    //  __int16    Type
    //
    //   __int8  Result
    //  __int32 SeqNumber 
    //}// 이때 랜덤값으로 클라이언트에게 송신


    CS_AUTH = 2,
    //{
    //  __int16    Type
    // 
    //  __int32 SeqNumber
	//	wchar_t	Nickname[20]		// null 포함
    //  char_t TokenKey[20]         // LoginServer에서 받아온 것
    //} 이때 부터 SessionKey로 암호화를 통한 세션 인증과

    SC_AUTH = 3,
    //{
    //  __int16 Type
    // 
    //  __int8 SectorX
    //  __int8 SectorY
    //}  초기 생성 위치. 그리고 실패시 server에서는 보낸것 확인 후 끊기

    CS_MOVE = 4,
    //{
    //  __int16    Type
    // 
    //  __int32 SeqNumber
    //  __int8 SectorX
    //  __int8 SectorY
    //}

    CS_CHAT = 5,
    //{
    //  __int16    Type
    // 
    //  __int32 SeqNumber
    //  __int16 MessageLen
    //  wchar_t Message[MessageLen]
    //}
    SC_CHAT = 6,
    //{
    //  __int16    Type
    // 
    //  wchar_t Nickname[20]
    //  __int16 MessageLen
    //  wchar_t Message[MessageLen]
    //}
};