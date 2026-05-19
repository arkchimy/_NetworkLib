#include "pch.h"
#include "Common.h"

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

void operator delete(void *ptr, const char *file, int line)
{
}

void operator delete[](void *ptr, const char *file, int line)
{
}



MyHeap::MyHeap()
    : sMyHeap(INVALID_HANDLE_VALUE)
{
    ULONG info = 2;
    sMyHeap = HeapCreate(HEAP_GENERATE_EXCEPTIONS, 0, 0);
    RT_ASSERT(sMyHeap != nullptr);
    HeapSetInformation(sMyHeap, HeapCompatibilityInformation, &info, sizeof(info));
}


MyHeap::~MyHeap() 
{
    RT_ASSERT(sMyHeap != INVALID_HANDLE_VALUE);
    RT_ASSERT(sMyHeap != nullptr);
    HeapDestroy(sMyHeap);
}
