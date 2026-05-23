#pragma once
enum en_ServerPACKET_TYPE : unsigned short
{
    //------------------------------------------------------------
    // 
    //
    //	{
    //		INT64 AccountNo 
    //	}
    //
    //
    // OnAccept에서 넣을 메세지
    //------------------------------------------------------------

    en_PACKET_Player_Alloc = 200,
    en_PACKET_Player_Delete ,
};