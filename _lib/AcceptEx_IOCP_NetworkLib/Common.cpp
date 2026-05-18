#include "pch.h"
#include "Common.h"
#include <Windows.h>

struct MyHeap
{
    MyHeap()
        : sMyHeap(INVALID_HANDLE_VALUE)
    {
        ULONG info = 2;
        sMyHeap = HeapCreate(HEAP_GENERATE_EXCEPTIONS, 0, 0);
        RT_ASSERT(sMyHeap != nullptr);
        HeapSetInformation(sMyHeap, HeapCompatibilityInformation, &info, sizeof(info));
    }
    ~MyHeap()
    {
        RT_ASSERT(sMyHeap != INVALID_HANDLE_VALUE);
        RT_ASSERT(sMyHeap != nullptr);
        HeapDestroy(sMyHeap);
    }
    HANDLE sMyHeap;
};

MyHeap gHeap;

void *operator new(size_t size, const char *file, int line)
{
    void *ptr = HeapAlloc(gHeap.sMyHeap, HEAP_GENERATE_EXCEPTIONS, size);
    RT_ASSERT(ptr != nullptr);

    return ptr;
}

void *operator new[](size_t size, const char *file, int line)
{
    void *ptr = HeapAlloc(gHeap.sMyHeap, HEAP_GENERATE_EXCEPTIONS, size);
    return ptr;
}

void operator delete(void *ptr)
{
    // HeapFree 함수가 성공하면 반환 값이 0이 아닙니다.
    BOOL bSuccess = HeapFree(gHeap.sMyHeap, 0, ptr);
    RT_ASSERT(bSuccess != 0);

}
void operator delete(void *ptr, const char *file, int line)
{
    // 경고 C4291 'void *operator new(size_t,const char *,int)' : 일치하는 operator delete가 없습니다.
    //  초기화할 때 예외가 Throw되지 않으면 메모리가 확보되지 않습니다.
    //  경고 제거용

    BOOL bSuccess = HeapFree(gHeap.sMyHeap, 0, ptr);
    RT_ASSERT(bSuccess != 0);
}

void operator delete[](void *ptr, const char *file, int line)
{
    BOOL bSuccess = HeapFree(gHeap.sMyHeap, 0, ptr);
    RT_ASSERT(bSuccess != 0);
}

void operator delete[](void *ptr)
{
    BOOL bSuccess = HeapFree(gHeap.sMyHeap, 0, ptr);
    RT_ASSERT(bSuccess != 0);

}