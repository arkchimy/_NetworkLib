#pragma once
#include <Windows.h>
#include <thread>
#include "../utility/DeadLockGuard/DeadLockGuard_lib.h"

extern thread_local stTlsLockInfo tls_LockInfo;
class WinThread
{
  public:
    WinThread& operator=(const WinThread &) = delete;
    WinThread(const WinThread &) = delete;

    WinThread(WinThread&& other) noexcept
    {
        if (this == &other)
            return ;
        _hThread = other._hThread;
        other._hThread = INVALID_HANDLE_VALUE;
    }
    WinThread &operator=(WinThread &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (_hThread != INVALID_HANDLE_VALUE)
            __debugbreak();// this가 갖고있던 값이 있었음. 

        _hThread = other._hThread;
        other._hThread = INVALID_HANDLE_VALUE;
        return *this;
    }
    template <typename T>
    static unsigned StartRoutine(void *arg);

    WinThread() = default;

    template <typename T>
    WinThread(void (T::*pmf)(), T *instance);

    ~WinThread()
    {
        if (_hThread == INVALID_HANDLE_VALUE)
            return;
        __debugbreak(); //Join 안 함.
    }
    void join()  ;
    HANDLE native_handle() const { return _hThread; }

  private:
    HANDLE _hThread = INVALID_HANDLE_VALUE;
};

template <typename T>
inline unsigned WinThread::StartRoutine(void *arg)
{
    clsDeadLockManager::GetInstance()->RegisterTlsInfoAndHandle(&tls_LockInfo);

    std::pair<void (T::*)(), T *> *data = static_cast<std::pair<void (T::*)(), T *> *>(arg);
    (data->second->*data->first)();
    delete data;
    return 0;
}

template <typename T>
inline WinThread::WinThread(void (T::*pmf)(), T *instance)
{
    std::pair<void (T::*)(), T *> *arg = new std::pair<void (T::*)(), T *>({pmf, instance});
    _hThread = (HANDLE)_beginthreadex(nullptr, 0, &StartRoutine<T>, arg, 0, nullptr);
}
