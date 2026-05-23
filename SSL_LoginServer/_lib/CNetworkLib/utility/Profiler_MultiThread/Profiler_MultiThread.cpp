// Profiler_MultiThread.cpp : 정적 라이브러리를 위한 함수를 정의합니다.

#include "Profiler_MultiThread.h"


void Profiler::SaveAsLog(const wchar_t *const lpFileName)
{
    if (lpFileName == nullptr)
        return;
    s_FileName = lpFileName;
    stRecordSet *pTlsRecordInfo;
    FILE *pFileLog;
    errno_t retFileOpen;
    size_t threadIndex;

    unsigned int recordIndex;
    wchar_t recordString[CCH_RECORD_CAPACITY];
    static const wchar_t *PRINT_FORMATS[] = {
        L" Tid   | No  | Name                       | Calls    | Avg w/o           | Max         #1    | Min         #1    | Max         #2    | Min         #2    |\n",
        L"-------+-----+----------------------------+----------+-------------------+-------------------+-------------------+-------------------+-------------------+\n",
        L" %06u | %3u | %24s | %10llu | %14s μs | %14s μs | %14s μs | %14s μs | %14s μs |\n",
        L" %06u | %3u | %24s | %10llu | %14s μs | %14.4lf μs | %14.4lf μs | %14s μs | %14s μs |\n",
        L" %06u | %3u | %24s | %10llu | %14s μs | %14.4lf μs | %14.4lf μs | %14.4lf μs | %14.4lf μs |\n",
        L" %06u | %3u | %24s | %10llu | %14.4lf μs | %14.4lf μs | %14.4lf μs | %14.4lf μs | %14.4lf μs |\n",
    };
    pFileLog = nullptr;
    retFileOpen = _wfopen_s(&pFileLog, lpFileName, L"w,ccs=UTF-8");
    if (retFileOpen != 0 || pFileLog == nullptr)
    {
        return;
    }

    fwrite(PRINT_FORMATS[FORMAT_HEADER], sizeof(wchar_t), wcslen(PRINT_FORMATS[FORMAT_HEADER]), pFileLog);
    fwrite(PRINT_FORMATS[FORMAT_BORDER], sizeof(wchar_t), wcslen(PRINT_FORMATS[FORMAT_BORDER]), pFileLog);
    HRESULT cchRetval;

    for (threadIndex = 0; threadIndex < sThreadCount; ++threadIndex)
    {
        pTlsRecordInfo = sRecordSetPtrs[threadIndex];

        for (recordIndex = 0; recordIndex < pTlsRecordInfo->RecordCount; recordIndex++)
        {
            const stRecord &refProfileRecord = pTlsRecordInfo->Records[recordIndex];

            switch (refProfileRecord.CountOfCall)
            {
            case 0:
                cchRetval = StringCchPrintfW(recordString, CCH_RECORD_CAPACITY, PRINT_FORMATS[FORMAT_NO_RECORD],
                                 pTlsRecordInfo->ThreadId,
                                 recordIndex,
                                 refProfileRecord.Tag.Name,
                                 refProfileRecord.CountOfCall,
                                 NO_RECORD,
                                 NO_RECORD,
                                 NO_RECORD,
                                 NO_RECORD,
                                 NO_RECORD);
                break;

            case 1:
                cchRetval = StringCchPrintfW(recordString, CCH_RECORD_CAPACITY, PRINT_FORMATS[FORMAT_ONCE_RECORD],
                                 pTlsRecordInfo->ThreadId,
                                 recordIndex,
                                 refProfileRecord.Tag.Name,
                                 refProfileRecord.CountOfCall,
                                 NO_RECORD,
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MaxAbnormal[NOISE_1ST]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MinAbnormal[NOISE_2ND]),
                                 NO_RECORD,
                                 NO_RECORD);
                break;

            case 2:
                __fallthrough;
            case 3:
                __fallthrough;
            case 4:
                cchRetval = StringCchPrintfW(recordString, CCH_RECORD_CAPACITY, PRINT_FORMATS[FORMAT_NO_AVG_RECORD],
                                 pTlsRecordInfo->ThreadId,
                                 recordIndex,
                                 refProfileRecord.Tag.Name,
                                 refProfileRecord.CountOfCall,
                                 NO_RECORD,
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MaxAbnormal[NOISE_1ST]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MinAbnormal[NOISE_1ST]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MaxAbnormal[NOISE_2ND]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MinAbnormal[NOISE_2ND]));
                break;

            default:
                cchRetval = StringCchPrintfW(recordString, CCH_RECORD_CAPACITY, PRINT_FORMATS[FORMAT_VALID_RECORD],
                                 pTlsRecordInfo->ThreadId,
                                 recordIndex,
                                 refProfileRecord.Tag.Name,
                                 refProfileRecord.CountOfCall,
                                 ConvertFrequencyToMicroseconds(refProfileRecord.TotalElapsed - refProfileRecord.MaxAbnormal[NOISE_1ST] - refProfileRecord.MinAbnormal[NOISE_1ST] - refProfileRecord.MaxAbnormal[NOISE_2ND] - refProfileRecord.MinAbnormal[NOISE_2ND]) / (refProfileRecord.CountOfCall - ABNORMAL_COUNT * 2),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MaxAbnormal[NOISE_1ST]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MinAbnormal[NOISE_1ST]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MaxAbnormal[NOISE_2ND]),
                                 ConvertFrequencyToMicroseconds(refProfileRecord.MinAbnormal[NOISE_2ND]));
                break;
            }
            if (cchRetval != S_OK)
                __debugbreak();
            fwrite(recordString, sizeof(wchar_t), wcslen(recordString), pFileLog);
        }
        fwrite(PRINT_FORMATS[FORMAT_BORDER], sizeof(wchar_t), wcslen(PRINT_FORMATS[FORMAT_BORDER]), pFileLog);
    }

    fclose(pFileLog);
}
double Profiler::ConvertFrequencyToMicroseconds(const LONGLONG frequency)
{
    return frequency * 1e6 / s_Frequency.QuadPart;
}
bool Profiler::Initialization()
{
    BOOL retval_QueryPerformanceFrequency;

    s_Tlsidx = TlsAlloc();
    if (s_Tlsidx == TLS_OUT_OF_INDEXES)
    {
        return false;
    }
    retval_QueryPerformanceFrequency = QueryPerformanceFrequency(&s_Frequency);
    if (retval_QueryPerformanceFrequency == 0)
    {
        return false;
    }

    return true;
}

stRecordSet *Profiler::GetTlsValueRecordSetOrNull()
{
    return reinterpret_cast<stRecordSet *>(TlsGetValue(s_Tlsidx));
}

stRecordSet *Profiler::CreateTlsRecordSet()
{
    stRecordSet *instance = new stRecordSet;
    sRecordSetPtrs[sThreadCount++] = instance;
    TlsSetValue(s_Tlsidx, instance);
    return instance;
}

void Profiler::Reset(void)
{
    stRecordSet *pTlsRecordInfoOrNull;
    for (int i = 0; i < MAX_THREAD_COUNT; i++)
    {
        pTlsRecordInfoOrNull = sRecordSetPtrs[i];
        if (pTlsRecordInfoOrNull == nullptr)
        {
            continue;
        }
        pTlsRecordInfoOrNull->Reset();
    }
    //SaveAsLog(s_FileName);
}

Profiler::Profiler(const wchar_t *const lpTagName)
    : m_lpTagName(lpTagName)
{
    if (bOn)
    {
        m_bStarted = true;
        Start(m_lpTagName);
    }
    else
        m_bStarted = false;
}

Profiler::~Profiler()
{
    if (bOn && m_bStarted)
        End(m_lpTagName);
    m_bStarted = false;
}

void Profiler::Start(const wchar_t *const lpTagName)
{
    LPWSTR pStrEnd;
    stRecordSet *pTlsRecordSet;
    stRecord *currentRecord;
    HRESULT StringCchCopyExW_retval;


    pTlsRecordSet = GetTlsValueRecordSetOrNull();

    if (pTlsRecordSet == nullptr)
        pTlsRecordSet = CreateTlsRecordSet();

    pTlsRecordSet->ThreadId = GetCurrentThreadId();
    currentRecord = pTlsRecordSet->SearchRecordOrNull(lpTagName);
    if (currentRecord == nullptr)
    {
        stRecord &record = pTlsRecordSet->Records[pTlsRecordSet->RecordCount++];
        StringCchCopyExW_retval = StringCchCopyExW(record.Tag.Name, _countof(record.Tag.Name), lpTagName, &pStrEnd, nullptr, 0);
        if (StringCchCopyExW_retval != S_OK)
        {
            __debugbreak();
        }
        record.Tag.CchLength = pStrEnd - record.Tag.Name;
        QueryPerformanceCounter(&record.StartAt);
    }
    else
    {
        QueryPerformanceCounter(&currentRecord->StartAt);
    }
}
stRecord *stRecordSet::SearchRecordOrNull(const wchar_t *const lpTagName)
{
    stRecordSet *pTlsRecordSet;
    stRecord *startRecord, *endRecord;

    pTlsRecordSet = Profiler::GetTlsValueRecordSetOrNull();

    startRecord = pTlsRecordSet->Records;
    if (pTlsRecordSet->RecordCount == 0)
        return nullptr;
    endRecord = pTlsRecordSet->Records + pTlsRecordSet->RecordCount;
    while (startRecord != endRecord)
    {
        if (wcsncmp(lpTagName, startRecord->Tag.Name, startRecord->Tag.CchLength) == 0)
            return startRecord;
        startRecord++;
    }
    return nullptr;
}
void Profiler::End(const wchar_t *const lpTagName)
{
    LARGE_INTEGER endTime;
    LONGLONG elapsedTime;
    LONGLONG startTime;

    stRecord *currentRecord;
    stRecordSet *currentRecordSet;

    QueryPerformanceCounter(&endTime);
    currentRecordSet = Profiler::GetTlsValueRecordSetOrNull();

    if (currentRecordSet == nullptr)
        return;
    currentRecord = currentRecordSet->SearchRecordOrNull(lpTagName);
    if (currentRecord == nullptr)
        return;

    startTime = currentRecord->StartAt.QuadPart;

    currentRecord->CountOfCall++;
    elapsedTime = endTime.QuadPart - startTime;
    currentRecord->TotalElapsed += elapsedTime;

    if (currentRecord->CountOfCall <= ABNORMAL_COUNT)
    {

        currentRecord->MaxAbnormal[NOISE_1ST] = max(elapsedTime, currentRecord->MaxAbnormal[NOISE_1ST]);
        currentRecord->MinAbnormal[NOISE_1ST] = min(elapsedTime, currentRecord->MinAbnormal[NOISE_1ST]);
        if (currentRecord->MaxAbnormal[NOISE_1ST] <= elapsedTime)
        {
            currentRecord->MaxAbnormal[NOISE_2ND] = currentRecord->MaxAbnormal[NOISE_1ST];
            currentRecord->MaxAbnormal[NOISE_1ST] = elapsedTime;
        }
        else if (currentRecord->MaxAbnormal[NOISE_2ND] < elapsedTime)
        {
            currentRecord->MaxAbnormal[NOISE_2ND] = elapsedTime;
        }

        if (currentRecord->MinAbnormal[NOISE_1ST] >= elapsedTime)
        {
            currentRecord->MinAbnormal[NOISE_2ND] = currentRecord->MinAbnormal[NOISE_1ST];
            currentRecord->MinAbnormal[NOISE_1ST] = elapsedTime;
        }
        else if (currentRecord->MinAbnormal[NOISE_2ND] > elapsedTime)
        {
            currentRecord->MinAbnormal[NOISE_2ND] = elapsedTime;
        }
    }
    else
    {
        if (currentRecord->MaxAbnormal[NOISE_1ST] <= elapsedTime)
        {
            currentRecord->MaxAbnormal[NOISE_2ND] = currentRecord->MaxAbnormal[NOISE_1ST];
            currentRecord->MaxAbnormal[NOISE_1ST] = elapsedTime;
        }
        else if (currentRecord->MaxAbnormal[NOISE_2ND] <= elapsedTime)
        {
            currentRecord->MaxAbnormal[NOISE_2ND] = elapsedTime;
        }
        else if (currentRecord->MinAbnormal[NOISE_1ST] >= elapsedTime)
        {
            currentRecord->MinAbnormal[NOISE_2ND] = currentRecord->MinAbnormal[NOISE_1ST];
            currentRecord->MinAbnormal[NOISE_1ST] = elapsedTime;
        }
        else if (currentRecord->MinAbnormal[NOISE_2ND] > elapsedTime)
        {
            currentRecord->MinAbnormal[NOISE_2ND] = elapsedTime;
        }
    }
    
}
