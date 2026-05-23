#pragma once

#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#include <Windows.h>
#include <iostream>
#include <strsafe.h>
enum
{
    MAX_THREAD_COUNT = 50,
    MAX_RECORD_COUNT = 30,

    MAX_TAG_NAME_LENGTH = 50 + 1,
    ABNORMAL_COUNT = 2, // 가장 큰 수, 가장 작은 수 noise의 판단 갯수를 정의

    NOISE_1ST = 0,
    NOISE_2ND = 1,

    FORMAT_HEADER = 0,
    FORMAT_BORDER = 1,
    FORMAT_NO_RECORD = 2,
    FORMAT_ONCE_RECORD = 3,
    FORMAT_NO_AVG_RECORD = 4,
    FORMAT_VALID_RECORD = 5,

    CCH_RECORD_CAPACITY = 204,

};
struct stRecord
{
    // bool IsCheckout;
    LARGE_INTEGER StartAt;
    LONGLONG TotalElapsed;

    LONGLONG MaxAbnormal[ABNORMAL_COUNT];
    LONGLONG MinAbnormal[ABNORMAL_COUNT];
    ULONGLONG CountOfCall;

    struct
    {
        WCHAR Name[MAX_TAG_NAME_LENGTH];
        unsigned int CchLength;
    } Tag;
};

struct stRecordSet
{
    DWORD ThreadId;
    stRecord Records[MAX_RECORD_COUNT];
    size_t RecordCount;

    stRecord *SearchRecordOrNull(const wchar_t *const lpTagName);

    stRecordSet(void)
    {
        Reset();
    }

    void Reset(void)
    {
        size_t recordIndex;

        ThreadId = GetCurrentThreadId();
        RecordCount = 0;

        for (recordIndex = 0; recordIndex < MAX_RECORD_COUNT; recordIndex++)
        {
            stRecord &refRecord = Records[recordIndex];
            ZeroMemory(&refRecord, sizeof(stRecord));

            // min은 최대 값
            refRecord.MinAbnormal[NOISE_1ST] = MAXLONGLONG;
            refRecord.MinAbnormal[NOISE_2ND] = MAXLONGLONG;
        }
    }
};
class Profiler
{
    public:
    Profiler(const wchar_t *const lpTagName);
    ~Profiler();

    bool m_bStarted = false; // Start에서 시간을 초기화 하였는지의 여부 판단.
    const wchar_t *m_lpTagName;
    
    inline static bool bOn = false;
    // 외부에서 호출 불가.
  private:
    static void Start(const wchar_t *const lpTagName);
    static void End(const wchar_t *const lpTagName);

  public:
    static void Reset(void);
    static void SaveAsLog(const wchar_t *const lpFileName);

    static double ConvertFrequencyToMicroseconds(const LONGLONG frequency);

    static bool Initialization();

    static stRecordSet *GetTlsValueRecordSetOrNull();
    static stRecordSet *CreateTlsRecordSet();

    inline static const wchar_t *s_FileName;
    inline static bool sbInit = Initialization();
    inline static LARGE_INTEGER s_Frequency;
    inline static DWORD s_Tlsidx;
    inline static const wchar_t NO_RECORD[] = L"-";

    inline static stRecordSet *sRecordSetPtrs[MAX_THREAD_COUNT];
    inline static size_t sThreadCount;
};