#pragma once

#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.

#include <Windows.h>
#include <iostream>
#include <queue>

#include "../MT_CRingBuffer/MT_CRingBuffer.h"
#include "../DeadLockGuard/DeadLockGuard_lib.h"
enum class en_LOG_LEVEL : DWORD
{
    SYSTEM_Mode = 0,
    ERROR_Mode,
    DEBUG_TargetMode,
    DEBUG_Mode,

    MAX,
};

struct DeBugHeader
{
    short len;
};
class CSystemLog
{
  private:
    CSystemLog();
  public:
    static CSystemLog *GetInstance(); // Initalization 되어있는 SRWLOCK 객체를 넘겨 줄것.

    BOOL SetDirectory(const wchar_t *directoryFileRoute);
    void SetLogLevel(const en_LOG_LEVEL &Log_Level);

    void Log(const WCHAR *szType, en_LOG_LEVEL LogLevel, const WCHAR *szStringFormat, ...);
    void LogHex(WCHAR *szType, en_LOG_LEVEL LogLevel, WCHAR *szLog, BYTE *pByte, int iByteLen);

    void SaveAsLog();
  private:
    BOOL GetLogFileName(const wchar_t *const filename, size_t strlen, SYSTEMTIME &stNowTime, __out wchar_t *const out);

  public:
    
    SharedMutex srw_Errorlock;

    //LogThread에게 전해주는 용도
    HANDLE hArgArr[2];
    CRingBuffer m_DebugQ = CRingBuffer(80000);
    HANDLE m_LogThreadEvent;


    HANDLE m_LogThread = INVALID_HANDLE_VALUE;

    en_LOG_LEVEL m_Log_Level = en_LOG_LEVEL::ERROR_Mode;
};
