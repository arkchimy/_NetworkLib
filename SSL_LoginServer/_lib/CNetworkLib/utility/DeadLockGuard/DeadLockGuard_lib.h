#pragma once

#define WIN32_LEAN_AND_MEAN
#include <iostream>

#include <Windows.h>
#include <conio.h>
#include <shared_mutex>
#include <thread>

#include <map>
#include <strsafe.h>
#include <unordered_set>
struct stTlsLockInfo;

class clsDeadLockManager
{
  private:
    clsDeadLockManager()
        : hMutexLogEvent(nullptr)
    {
        // 만일 재귀 lock이 걸린다면 생성자에서 생성한 이벤트로 Signal이 온다.
        // 여기서 생성한 Thread는 해당 signal을 기다리고있으므로 해당 쓰레드에서 Log를 작성.

        hMutexLogEvent = CreateEvent(nullptr, false, false, nullptr);
        hManagerThread = std::thread(&clsDeadLockManager::MyMutexManagerThread, this);
        // Join 안함.
    };
    void MyMutexManagerThread()
    {

        if (hMutexLogEvent == nullptr)
            __debugbreak();

        SetThreadDescription(hManagerThread.native_handle(), L"MyMutexManagerThread");

        WaitForSingleObject(hMutexLogEvent, INFINITE);

        CreateLogFile_TlsInfo();

        // 에러로인한 Log작성임.
        __debugbreak();
    }

  public:
    static clsDeadLockManager *GetInstance()
    {
        // 함수가 실패하면 반환 값은 NULL.
        static HANDLE hInitEvent = CreateEvent(nullptr, true, false, nullptr);
        if (hInitEvent == nullptr)
            __debugbreak();

        static clsDeadLockManager *instance = nullptr;
        static long Once = false;
        // 단 한번만

        if (InterlockedCompareExchange(&Once, true, false) == false)
        {
            instance = new clsDeadLockManager();
            if (instance == nullptr)
                __debugbreak();
            SetEvent(hInitEvent);
        }
        else
        {
            WaitForSingleObject(hInitEvent, INFINITE);
        }

        return instance;
    }
    void RegisterTlsInfoAndHandle(stTlsLockInfo *info)
    {
        std::lock_guard<std::shared_mutex> m_lock(m);
        LockInfos.insert(info);
    }
    void CreateLogFile_TlsInfo(const wchar_t *filename = L"TlsLockInfo.txt");
    void RequestCreateLogFile_And_Debugbreak();

  private:
    std::unordered_set<stTlsLockInfo *> LockInfos;
    std::shared_mutex m;

    HANDLE hMutexLogEvent;
    std::thread hManagerThread;

    std::mutex Log_m; // ManagerThread에서 접근 And 외부 mainThread에서 접근할 경우가 존재.
};
struct stTlsLockInfo
{
    stTlsLockInfo()
        : _size(0), waitLock(nullptr), _shared_size(0)
    {
        holding.reserve(100);        // 100개의 Lock을 잡을일은 없겠지
        shared_holding.reserve(100); // 100개의 Lock을 잡을일은 없겠지

        HANDLE hThread2 = GetCurrentThread();
        DuplicateHandle(GetCurrentProcess(), hThread2, GetCurrentProcess(), &hThread, 0, false, DUPLICATE_SAME_ACCESS);
    }

    void WriteLog(const wchar_t *filename)
    {
        const wchar_t ThreadStartFormat[] =
            L"┌──────────────────────────────────────────────────────────────┐\n"
            L"│ Thread : %-52s │\n"
            L"├──────────────────────────────────────────────────────────────┤\n";
        const wchar_t ThreadStartIDFormat[] =
            L"┌──────────────────────────────────────────────────────────────┐\n"
            L"│ Thread ID : %-48lu │\n"
            L"├──────────────────────────────────────────────────────────────┤\n";
        const wchar_t WaitStartFormat[] =
            L"│ WAIT  : %016p                                           │\n";

        const wchar_t HoldStartFormat[] =
            L"│ HOLD  : %016p                                           │\n";
        const wchar_t ShardHoldStartFormat[] =
            L"│ SHARED_HOLD  : %016p                                           │\n";

        const wchar_t ThreadCloseFormat[] =
            L"└──────────────────────────────────────────────────────────────┘\n\n";

        wchar_t buffer[200];

        FILE *file;

        _wfopen_s(&file, filename, L"a+, ccs=UTF-16LE");
        if (file == nullptr)
            __debugbreak();
        wchar_t *threadName;

        GetThreadDescription(hThread, &threadName);
        if (wcslen(threadName) == 0)
        {
            StringCchPrintfW(buffer, _countof(buffer), ThreadStartIDFormat, GetThreadId(hThread));
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), file);
        }
        else
        {
            StringCchPrintfW(buffer, _countof(buffer), ThreadStartFormat, threadName);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), file);
        }

        if (waitLock != nullptr)
        {
            StringCchPrintfW(buffer, _countof(buffer), WaitStartFormat, waitLock);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), file);
        }
        for (int i = 0; i < _size; i++)
        {
            StringCchPrintfW(buffer, _countof(buffer), HoldStartFormat, holding[i]);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), file);
        }
        for (int i = 0; i < _shared_size; i++)
        {
            StringCchPrintfW(buffer, _countof(buffer), ShardHoldStartFormat, shared_holding[i]);
            fwrite(buffer, sizeof(wchar_t), wcslen(buffer), file);
        }
        fwrite(ThreadCloseFormat, sizeof(wchar_t), wcslen(ThreadCloseFormat), file);
        fclose(file);
    }
    // tls를 사용하여 동기화없이 접근하기.
    void *waitLock;
    std::vector<void *> holding; // bool 이 false 면 Shared , 1이면 Exclusive
    std::vector<void *> shared_holding;
    int _size; // holding의 길이. 외부에서 Lock없이 size만큼만 읽으려는 용도.
    int _shared_size;
    HANDLE hThread;
};

struct DeadLockGuard
{
    std::shared_mutex m;
    _Acquires_exclusive_lock_(m) void lock();
    _Releases_exclusive_lock_(m) void unlock();

    _Acquires_shared_lock_(m) void lock_shared();
    _Releases_shared_lock_(m) void unlock_shared();
};

#ifdef DEADLOCK_GUARD
    using SharedMutex = DeadLockGuard;
#else
    using SharedMutex = std::shared_mutex ;
#endif
