#pragma once
enum contentsConfig
{
	CONTENTS_MSG_MAX_SIZE = 2000,
};
enum class ePacketType :__int16
{
	CS_ECHO_REQ = 0,
	SC_ECHO_RES = 1,
};