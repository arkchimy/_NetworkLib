#pragma once
#include <memory>
#include <Windows.h>   
#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.

using ringBufferSize = LONG64;


class CRingBuffer
{
  public:
    CRingBuffer();
    CRingBuffer(ringBufferSize iBufferSize);
    CRingBuffer(ringBufferSize iBufferSize, bool ContentsQBuffer);

    ~CRingBuffer();

    ringBufferSize GetUseSize();
    ringBufferSize GetUseSize(const char *f, const char *r);
    ringBufferSize GetFreeSize();
    ringBufferSize GetFreeSize(const char *f, const char *r);

    ringBufferSize Enqueue(const void *pSrc, ringBufferSize iSize);
    ringBufferSize Dequeue(void *chpDest, ringBufferSize iSize);


    ringBufferSize Peek(void *chpDest, ringBufferSize iSize);
    ringBufferSize Peek(void *pDest, ringBufferSize iSize, char *f, char *r);

    void ClearBuffer();

    ringBufferSize DirectEnqueueSize();
    ringBufferSize DirectEnqueueSize(const char* f, const char* r);

    ringBufferSize DirectDequeueSize();
    ringBufferSize DirectDequeueSize(const char *f, const char *r);

    void MoveRear(ringBufferSize iSize);
    void MoveFront(ringBufferSize iSize);


  public:
    char *_begin;
    char *_end;

    char *_frontPtr;
    char *_rearPtr;
    ringBufferSize _size;
    inline static ringBufferSize s_BufferSize = 2048;
};
