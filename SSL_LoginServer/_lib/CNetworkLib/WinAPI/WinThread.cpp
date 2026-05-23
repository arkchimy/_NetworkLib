#include "WinThread.h"
#include <iostream>

//void example()
class A
{
  public:
    void WorkerThread()
    {
        std::cout << "WorkerThread\n";
    }
};
void example()
{
    A a;
    WinThread thread(&A::WorkerThread, &a);
}

thread_local stTlsLockInfo tls_LockInfo;

void WinThread::join() 
{
    WaitForSingleObject(_hThread, INFINITE);
    CloseHandle(_hThread);
    _hThread = INVALID_HANDLE_VALUE;
}
