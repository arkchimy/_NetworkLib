// MTProfiler_Lib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//
#include "MTProfiler_Lib.h"
#include <conio.h>
#include <strsafe.h>


// TODO: 라이브러리 함수의 예제입니다.
void fnMTProfilerLib()
{
    // RAII 패턴
    {
        stProfile profile(L"newAllocTime");
    }

    // 특정 쓰레드에서
#ifdef PROFILE
    if (_kbhit())
    {
        char ch = _getch();
        if (ch == 'A' || ch == 'a')
        {
            CProfileRegistry::GetInstance().CreateProfile(L"Profile");
        }
        else if (ch == 'D' || ch == 'd')
        {
            CProfileRegistry::GetInstance().ResetEntry();
        }
    }
#endif
}
// Single_Profiler_lib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//


thread_local CProfileManager manager;



const wchar_t *captionFormat[2] =
    {
        L"+--------------+--------------+------------+------------------------+\n"
        L"| %-12ls | %-12ls | %10ls | %20ls   |\n"
        L"+--------------+--------------+------------+------------------------+\n",
        L"+--------------+--------------+------------+------------------------+\n"};
const wchar_t *format[1] = {
    L"| %-12d | %-12ls | %10d | %20.5f us|\n",
};
void CProfileRegistry::CreateProfile(const wchar_t *fileName)
{
    FILE *hFile;
    wchar_t buffer[1000];
    std::wstring _fileName;
    _fileName += fileName;
    _fileName += TEXT(__DATE__);
    _fileName += L".txt";

    // 0이면 성공
    if (_wfopen_s(&hFile, _fileName.c_str(), L"w+ ,ccs = UTF-16LE") != 0)
    {
        return;
    }
    ZeroMemory(buffer, sizeof(buffer));
    StringCchPrintf(buffer, sizeof(buffer) / sizeof(wchar_t), captionFormat[0], L"ThreadID",
                    L"Tag",
                    L"Called", L"Avg");

    _ASSERT(hFile != nullptr);
    fwrite(buffer, sizeof(wchar_t), wcslen(buffer), hFile);

    fclose(hFile);

    _Acquires_exclusive_lock_(&_srwManager);
    for (auto &element : _manager_list)
    {
        element->CreateProfile(_fileName.c_str());
    }
    _Releases_exclusive_lock_(&_srwManager);
}

void CProfileRegistry::ResetEntry()
{
    _Acquires_exclusive_lock_(&_srwManager);
    for (auto &element : _manager_list)
    {
        element->ResetEntry();
    }
    _Releases_exclusive_lock_(&_srwManager);
}

CProfileManager::CProfileManager()
{

    CProfileRegistry::GetInstance().RegistProfiler(this);
    QueryPerformanceFrequency(&_QPC_frequency);

    for (int32_t i = EnTryMaxSize - 1; i >= 0; i--)
    {
        _freeIndex.push(i);
    }
    _ThreadID = GetCurrentThreadId();
}
void CProfileManager::CreateProfile(const wchar_t *fileName)
{
    FILE *hFile;

    wchar_t buffer[1000];
    _wfopen_s(&hFile, fileName, L"a+ ,ccs = UTF-16LE");

    if (hFile == nullptr)
    {
        return;
    }

    for (int i = 0; i < EnTryMaxSize; i++)
    {
        ZeroMemory(buffer, sizeof(buffer));
        if (_entrys[i]._totalCnt > AbnormalBufferSize * 2)
        {
            int64_t totalTIme = _entrys[i]._totalTime;
            int64_t abnormalSum = 0;
            for (int j = 0; j < AbnormalBufferSize; j++)
            {
                abnormalSum += _entrys[i]._min[j];
                abnormalSum += _entrys[i]._max[j];
            }

            totalTIme = _entrys[i]._totalTime;
            if (totalTIme < abnormalSum)
                __debugbreak();
            totalTIme -= abnormalSum;

            _entrys[i]._avg = (static_cast<double>(totalTIme) / (_entrys[i]._totalCnt - AbnormalBufferSize * 2)) * 1e6 / _QPC_frequency.QuadPart;

            StringCchPrintf(buffer, sizeof(buffer) / sizeof(wchar_t), format[0], _ThreadID, _entrys[i]._tag, _entrys[i]._totalCnt, _entrys[i]._avg);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), hFile);
        }
    }

    fwrite(captionFormat[1], sizeof(wchar_t), wcslen(captionFormat[1]), hFile);
    fclose(hFile);
}

void CProfileManager::ResetEntry()
{
    for (int i = 0; i < EnTryMaxSize; i++)
    {
        _entrys[i].InitEntry();
    }
}

void stProfile::Profile_End(const wchar_t *tag, LARGE_INTEGER startTime)
{
    int64_t distance;
    LARGE_INTEGER currentTime;
    // 미리 시간 측정
    QueryPerformanceCounter(&currentTime);
    distance = currentTime.QuadPart - startTime.QuadPart;

    manager.UpdateEntry(tag, distance);
}