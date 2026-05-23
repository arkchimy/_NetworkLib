#include "DeadLockGuard_lib.h"

// 사용법
void func()
{
    // std::shared_mutex 대신에 SharedMutex  를 사용할 것.
    // 반드시 해당 라이브러리 include 이전에 #define DEADLOCK_GUARD 를 사용할 것.
#ifdef DEADLOCK_GUARD
    using SharedMutex = DeadLockGuard;
#else
    using SharedMutex = std::shared_mutex;
#endif

    //                         사용법 

    SharedMutex m;

    {
        thread_local stTlsLockInfo tls_LockInfo;
        clsDeadLockManager::GetInstance()->RegisterTlsInfoAndHandle(&tls_LockInfo); // 등록을 해야함.

        std::lock_guard<SharedMutex> m_lock(m);
        std::shared_lock<SharedMutex> lock(m);
    }

}

extern thread_local stTlsLockInfo tls_LockInfo;

_Acquires_exclusive_lock_(m) void DeadLockGuard::lock()
{
    auto iter = tls_LockInfo.holding.begin();
    tls_LockInfo.waitLock = &m;

    for (iter; iter != tls_LockInfo.holding.end(); iter++)
    {
        if (*iter == &m)
            break;
    }
    auto s_iter = tls_LockInfo.shared_holding.begin();
    for (s_iter; s_iter != tls_LockInfo.shared_holding.end(); s_iter++)
    {
        if (*s_iter == &m)
            break;
    }
    if (s_iter != tls_LockInfo.shared_holding.end())
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();

    if (iter != tls_LockInfo.holding.end())
    {
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();
    }

    m.lock();
    tls_LockInfo.waitLock = nullptr;
    tls_LockInfo.holding.push_back(&m);
    tls_LockInfo._size++;
}
_Releases_exclusive_lock_(m) void DeadLockGuard::unlock()
{
    auto iter = tls_LockInfo.holding.begin();
    for (iter; iter != tls_LockInfo.holding.end(); iter++)
    {
        if (*iter == &m)
            break;
    }
    if (iter == tls_LockInfo.holding.end())
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();

    tls_LockInfo.holding.erase(iter);
    tls_LockInfo._size--;
    m.unlock();
}

_Acquires_shared_lock_(m) void DeadLockGuard::lock_shared()
{

    
    auto iter = tls_LockInfo.holding.begin();
    tls_LockInfo.waitLock = &m;

    for (iter; iter != tls_LockInfo.holding.end(); iter++)
    {
        if (*iter == &m)
            break;
    }
    auto s_iter = tls_LockInfo.shared_holding.begin();
    for (s_iter; s_iter != tls_LockInfo.shared_holding.end(); s_iter++)
    {
        if (*s_iter == &m)
            break;
    }
    if (s_iter != tls_LockInfo.shared_holding.end())
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();

    if (iter != tls_LockInfo.holding.end())
    {
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();
    }
    


    m.lock_shared();
    tls_LockInfo.waitLock = nullptr;
    // LockShared에서 Holding을 표시해둘까?

    tls_LockInfo.shared_holding.push_back(&m);
    tls_LockInfo._shared_size++;
}
_Releases_shared_lock_(m) void DeadLockGuard::unlock_shared()
{
    auto s_iter = tls_LockInfo.shared_holding.begin();
    for (s_iter; s_iter != tls_LockInfo.shared_holding.end(); s_iter++)
    {
        if (*s_iter == &m)
            break;
    }
    if (s_iter == tls_LockInfo.shared_holding.end())
        clsDeadLockManager::GetInstance()->RequestCreateLogFile_And_Debugbreak();

    tls_LockInfo.shared_holding.erase(s_iter);
    tls_LockInfo._shared_size--;
    m.unlock_shared();
}

void clsDeadLockManager::CreateLogFile_TlsInfo(const wchar_t *filename)
{
    // Log를 위한 Lock
    std::lock_guard<std::mutex> m_lock(Log_m);
    {
        std::lock_guard<std::shared_mutex> m_lock(m);
        for (auto &info : LockInfos)
        {
            SuspendThread(info->hThread);
        }
        FILE *file;

        _wfopen_s(&file, filename, L"w, ccs=UTF-16LE");
        if (file == nullptr)
            __debugbreak();

        fclose(file);

        for (auto &info : LockInfos)
        {
            info->WriteLog(filename);
        }

        for (auto &info : LockInfos)
        {
            ResumeThread(info->hThread);
        }
    }
}

void clsDeadLockManager::RequestCreateLogFile_And_Debugbreak()
{
    SetEvent(hMutexLogEvent);
    Sleep(INFINITE);
}
