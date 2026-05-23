// CSystemLog_lib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//

#include "CSystemLog.h"

#include <strsafe.h>
#include <thread>

constexpr size_t DebugVectorSize = 500000;

LONG64 m_seqNumber = 0;
// Debug용도
struct stDebugInfo
{
    const WCHAR *szType = nullptr;
    en_LOG_LEVEL LogLevel = en_LOG_LEVEL::MAX;
    const WCHAR *szStringFormat = nullptr;
    std::wstring LogWstring;
};

std::vector<stDebugInfo> m_TargetDebugVector(DebugVectorSize);

unsigned LogThread(void *arg)
{
    //argArr[0] = &m_DebugQ;
    //argArr[1] = m_LogThreadEvent;
    HANDLE *hArg = (HANDLE *)arg;

   CRingBuffer *ptrDebugQ = (CRingBuffer *)hArg[0];

   m_TargetDebugVector.resize(DebugVectorSize);

    static int idx = 0;

    while (1)
    {
        
        DeBugHeader header;
        while (ptrDebugQ->Peek(&header, sizeof(DeBugHeader)) == sizeof(DeBugHeader))
        {

            std::wstring str;
            wchar_t buffer[1000];
            ringBufferSize useSize = ptrDebugQ->GetUseSize();
            if (useSize < LONG64(header.len + sizeof(DeBugHeader)))
                break;

            ptrDebugQ->Dequeue(&header, sizeof(DeBugHeader));
            ptrDebugQ->Dequeue(buffer, header.len);

            //m_TargetDebugVector[idx % 100] = buffer;
            idx++;

        }

    }

}
void CSystemLog::SaveAsLog()
{
    FILE *debugFile;
reWfopen:
    _wfopen_s(&debugFile, L"DebugFile.txt", L"a+ , ccs = UTF-16LE");
    if (debugFile == nullptr)
    {
        goto reWfopen;
    }

    for (int i = 0; i < DebugVectorSize; i++)
    {
        fwrite(m_TargetDebugVector[i].LogWstring.c_str(), 2, wcslen(m_TargetDebugVector[i].LogWstring.c_str()), debugFile);
    }
    fclose(debugFile);
}

CSystemLog::CSystemLog()
{
    // 소멸자에서 제거하기. 하기싫음 말고

    m_LogThreadEvent = CreateEvent(nullptr, 0, 0, nullptr);

    hArgArr[0] = &m_DebugQ;
    hArgArr[1] = m_LogThreadEvent;
    
    //m_LogThread = (HANDLE)_beginthreadex(nullptr, 0, LogThread, hArgArr, 0, nullptr);

}

// 에러 발생 시 이중으로 Log를 작성하여 에러상황을 알려보는 시도.
BOOL CSystemLog::GetLogFileName(const wchar_t *const filename, size_t strlen, SYSTEMTIME &stNowTime, __out wchar_t *const out)
{

    HRESULT cch_retval;

    cch_retval = StringCchPrintfW(out, strlen, L"%d_%d_%s.txt", stNowTime.wYear, stNowTime.wMonth, filename);
    if (cch_retval != S_OK)
    {
        CSystemLog::GetInstance()->Log(L"StringCchPrintf_Error.txt", en_LOG_LEVEL::ERROR_Mode, L"StringCchPrintfW_Error %d", GetLastError());
        __debugbreak();
        return false;
    }

    return true;
}

BOOL CSystemLog::SetDirectory(const wchar_t *directoryFileRoute)
{
    return SetCurrentDirectoryW(directoryFileRoute);
}

void CSystemLog::SetLogLevel(const en_LOG_LEVEL &Log_Level)
{
    m_Log_Level = Log_Level;
}

CSystemLog *CSystemLog::GetInstance()
{
    static CSystemLog instance;
    return &instance;
}

void CSystemLog::Log(const WCHAR *szType, en_LOG_LEVEL LogLevel, const WCHAR *szStringFormat, ...)
{
    // szType 별로 파일이 나옴.
    // szStringFormat 는 Log 시점에 남기고싶은 정보를 남기기 위함.
    // 고정적으로  [szType] [2015-09-11 19:00:00 / LogLevel / 0000seqNumber] 로그문자열.

    static WCHAR format[] = L"[ %12s] [ %04d-%02d-%02d %02d:%02d:%03d  /%-8s/%08lld] \t[Thread ID : %06d] \t";
    static const WCHAR *Logformat[(DWORD)en_LOG_LEVEL::MAX] =
        {
            L"SYSTEM",
            L"ERROR",
            L"Target",
            L"DEBUG",
            };

    WCHAR LogHeaderBuffer[FILENAME_MAX * 2];
    WCHAR LogWstring[FILENAME_MAX];
    WCHAR LogFileName[FILENAME_MAX];

    HRESULT cchRetval;

    va_list va;
    SYSTEMTIME stNowTime;
    LONG64 local_SeqNumber;

    FILE *LogFile;
    static LONG64 TargetDebugSeq = 0;

    
    if (LogLevel > m_Log_Level)
    {
        return;
    }
    
    if (LogLevel == en_LOG_LEVEL::DEBUG_TargetMode)
    {
        // DeBug  가 아닐 때  DeBug 모드가 발생하면, 메모리 Log라도 작성하자.
        //if (LogLevel == en_LOG_LEVEL::DEBUG_TargetMode)
     
        SYSTEMTIME stNowTime;
   
        GetLocalTime(&stNowTime);

        if (GetLogFileName(szType, FILENAME_MAX, stNowTime, LogFileName) == false)
        {
            __debugbreak();
            return;
        }

        StringCchPrintfW(LogHeaderBuffer, sizeof(LogHeaderBuffer) / sizeof(wchar_t), format,
                         szType, stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay,
                         stNowTime.wHour, stNowTime.wMinute, stNowTime.wMilliseconds,
                         Logformat[(DWORD)LogLevel], TargetDebugSeq, GetCurrentThreadId());

        m_TargetDebugVector[TargetDebugSeq % DebugVectorSize].LogLevel = LogLevel;
        m_TargetDebugVector[TargetDebugSeq % DebugVectorSize].szStringFormat = szStringFormat;
        m_TargetDebugVector[TargetDebugSeq % DebugVectorSize].szType = szType;
   
        va_start(va, szStringFormat);
        cchRetval = StringCchVPrintfW(LogWstring, sizeof(LogWstring) / sizeof(wchar_t), szStringFormat, va);
        va_end(va);

        StringCchCatW(LogHeaderBuffer, sizeof(LogHeaderBuffer) / sizeof(wchar_t), LogWstring);

        size_t idx = wcslen(LogHeaderBuffer);
        LogHeaderBuffer[idx] = L'\n';
        LogHeaderBuffer[idx + 1] = 0;

        m_TargetDebugVector[TargetDebugSeq % DebugVectorSize].LogWstring = LogHeaderBuffer;
        TargetDebugSeq++;

        return;
        
    }

    local_SeqNumber = _InterlockedIncrement64(&m_seqNumber);

    GetLocalTime(&stNowTime);

    if (GetLogFileName(szType, (size_t)FILENAME_MAX, stNowTime, LogFileName) == false)
    {
        __debugbreak();
        return;
    }

    StringCchPrintfW(LogHeaderBuffer, sizeof(LogHeaderBuffer) / sizeof(wchar_t), format,
                     szType, stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay,
                     stNowTime.wHour, stNowTime.wMinute, stNowTime.wMilliseconds,
                     Logformat[(DWORD)LogLevel], local_SeqNumber,GetCurrentThreadId());

    //[Battle] [2015-09-11 19:00:00 / DEBUG / 000000001] 로그문자열.

    va_start(va, szStringFormat);
    cchRetval = StringCchVPrintfW(LogWstring, sizeof(LogWstring) / sizeof(wchar_t), szStringFormat, va);
    va_end(va);

    if (cchRetval != S_OK)
    {
        __debugbreak();
        return;
    }
    StringCchCatW(LogHeaderBuffer, sizeof(LogHeaderBuffer) / sizeof(wchar_t), LogWstring);

    size_t idx = wcslen(LogHeaderBuffer);
    LogHeaderBuffer[idx] = L'\n';
    LogHeaderBuffer[idx + 1] = 0;


    // Lock 걸고 들어감.
    {
        std::lock_guard<SharedMutex> lock(srw_Errorlock);

        _wfopen_s(&LogFile, LogFileName, L"a+ , ccs = UTF-16LE");

        if (LogFile == nullptr)
        {

            return;
        }
        fwrite(LogHeaderBuffer, 2, wcslen(LogHeaderBuffer), LogFile);
        fclose(LogFile);
    }
}

void CSystemLog::LogHex(WCHAR *szType, en_LOG_LEVEL LogLevel, WCHAR *szLog, BYTE *pByte, int iByteLen)
{
}
