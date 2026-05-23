#pragma once

#define WIN32_LEAN_AND_MEAN // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#include <Windows.h>

#pragma pack(1)

struct stHeader
{
  public:
    BYTE byCode;
    USHORT sDataLen;
    BYTE byRandKey;
    BYTE byCheckSum;
};
#pragma pack()
