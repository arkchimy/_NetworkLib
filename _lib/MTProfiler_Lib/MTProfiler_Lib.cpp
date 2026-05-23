// MTProfiler_Lib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//
#include "MTProfiler_Lib.h"
#include <conio.h>
#include <strsafe.h>


void ExampleMtProfilerLib()
{
    {
        Profile profile(L"newAllocTime");
    }

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
    std::wstring fullFileName;
    fullFileName += fileName;
    fullFileName += TEXT(__DATE__);
    fullFileName += L".txt";

    if (_wfopen_s(&hFile, fullFileName.c_str(), L"w+ ,ccs = UTF-16LE") != 0)
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

    _Acquires_exclusive_lock_(&mSrwManager);
    for (auto &element : mManagerList)
    {
        element->CreateProfile(fullFileName.c_str());
    }
    _Releases_exclusive_lock_(&mSrwManager);
}

void CProfileRegistry::ResetEntry()
{
    _Acquires_exclusive_lock_(&mSrwManager);
    for (auto &element : mManagerList)
    {
        element->ResetEntry();
    }
    _Releases_exclusive_lock_(&mSrwManager);
}

CProfileManager::CProfileManager()
{
    CProfileRegistry::GetInstance().RegistProfiler(this);
    QueryPerformanceFrequency(&mQpcFrequency);

    for (int32_t i = (int32_t)eConfig::EntryMaxSize - 1; i >= 0; i--)
    {
        mFreeIndex.push(i);
    }
    mThreadId = GetCurrentThreadId();
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

    for (int i = 0; i < (int)eConfig::EntryMaxSize; i++)
    {
        ZeroMemory(buffer, sizeof(buffer));
        if (mEntrys[i].mTotalCnt > (int)eConfig::AbnormalBufferSize * 2)
        {
            int64_t totalTime   = mEntrys[i].mTotalTime;
            int64_t abnormalSum = 0;
            for (int j = 0; j < (int)eConfig::AbnormalBufferSize; j++)
            {
                abnormalSum += mEntrys[i].mMin[j];
                abnormalSum += mEntrys[i].mMax[j];
            }

            totalTime = mEntrys[i].mTotalTime;
            if (totalTime < abnormalSum)
            {
                __debugbreak();
            }
            totalTime -= abnormalSum;

            mEntrys[i].mAvg = (static_cast<double>(totalTime) / (mEntrys[i].mTotalCnt - (int)eConfig::AbnormalBufferSize * 2)) * 1e6 / mQpcFrequency.QuadPart;

            StringCchPrintf(buffer, sizeof(buffer) / sizeof(wchar_t), format[0], mThreadId, mEntrys[i].mTag, mEntrys[i].mTotalCnt, mEntrys[i].mAvg);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), hFile);
        }
    }

    fwrite(captionFormat[1], sizeof(wchar_t), wcslen(captionFormat[1]), hFile);
    fclose(hFile);
}

void CProfileManager::ResetEntry()
{
    for (int i = 0; i < (int)eConfig::EntryMaxSize; i++)
    {
        mEntrys[i].InitEntry();
    }
}

void Profile::profileEnd(const wchar_t *tag, LARGE_INTEGER startTime)
{
    int64_t distance;
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    distance = currentTime.QuadPart - startTime.QuadPart;

    manager.UpdateEntry(tag, distance);
}
