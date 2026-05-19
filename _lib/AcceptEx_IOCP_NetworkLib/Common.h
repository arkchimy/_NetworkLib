#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef RT_ASSERT
#define RT_ASSERT(x) \
    if (!(x))        \
        __debugbreak();
#endif
#include <Windows.h>

void *operator new(size_t size,const char* file, int line);
void *operator new[](size_t size, const char *file, int line);
void operator delete(void *ptr, const char *file, int line);
void operator delete[](void *ptr, const char *file, int line);

struct MyHeap
{
    MyHeap();
    ~MyHeap();
    HANDLE sMyHeap;
};

extern MyHeap gHeap;
struct MyDeleteHelper
{
    template<typename  T>
    void operator,(T* ptr)
    {
        ptr->~T();
        BOOL bSuccess = HeapFree(gHeap.sMyHeap, 0, ptr);
        RT_ASSERT(bSuccess != 0);
    }
};
#define MY_NEW new (__FILE__,__LINE__)
#define MY_DELETE MyDeleteHelper {}, 