#include "MT_CRingBuffer.h"

CRingBuffer::CRingBuffer()
    : CRingBuffer(s_BufferSize) {}

CRingBuffer::CRingBuffer(ringBufferSize iBufferSize)
{
    _begin = (char*)malloc(iBufferSize);
    if (_begin == nullptr)
    {
        __debugbreak();
    }
    _end = _begin + iBufferSize;
    ClearBuffer();
}
CRingBuffer::CRingBuffer(ringBufferSize iBufferSize, bool ContensQBuffer)
{
    _begin = (char *)malloc(iBufferSize);
    if (_begin == nullptr)
    {
        __debugbreak();
    }
    _end = _begin + iBufferSize;
    _size = iBufferSize;
    ClearBuffer();
}

CRingBuffer::~CRingBuffer()
{
    free(_begin);
    //_aligned_free(_begin);
}

ringBufferSize CRingBuffer::GetUseSize()
{
    return GetUseSize(_frontPtr, _rearPtr);
}

ringBufferSize CRingBuffer::GetUseSize(const char *f, const char *r)
{
    return f <= r ? ringBufferSize(r - f)
                  : ringBufferSize(_end - f + r - _begin);
}

ringBufferSize CRingBuffer::GetFreeSize()
{
    return GetFreeSize(_frontPtr, _rearPtr);
}

ringBufferSize CRingBuffer::GetFreeSize(const char *f, const char *r)
{
    if (f == _begin && f <= r)
        return ringBufferSize(_end - r - 1);
    return f <= r ? ringBufferSize((_end - r) + (f - _begin) - 1)
                  : ringBufferSize(f - r - 1);
}

ringBufferSize CRingBuffer::Enqueue(const void *pSrc, ringBufferSize iSize)
{
    ringBufferSize directEnQSize, freeSize;
    ringBufferSize local_size = iSize;

    const char *chpSrc;
    char *f, *r;
    f = _frontPtr;
    r = _rearPtr;

    chpSrc = reinterpret_cast<const char *>(pSrc);

    directEnQSize = DirectEnqueueSize(f, r);
    freeSize = GetFreeSize(f, r);

    if (freeSize < local_size)
    {
        // TODO : 링버퍼가 가득차버림의 경우
        //HEX_FILE_LOG(L"RingbufferFulled_Error.txt", _begin, s_BufferSize);

        return false;
    }

    if (local_size <= directEnQSize)
    {
        memcpy(r, chpSrc, local_size);
    }
    else
    {
        memcpy(r, chpSrc, directEnQSize);
        memcpy(_begin, chpSrc + directEnQSize, local_size - directEnQSize);
    }

    MoveRear(local_size);
    return local_size;
}

ringBufferSize CRingBuffer::Dequeue(void *pDest, ringBufferSize iSize)
{
    char *chpDest = reinterpret_cast<char *>(pDest);
    char *f, *r;
    ringBufferSize DirectDeqSize;
    ringBufferSize useSize;
    ringBufferSize local_size;

    local_size = iSize;
    f = _frontPtr;
    r = _rearPtr;

    useSize = GetUseSize(f, r);

    if (useSize < local_size)
        return false;

    DirectDeqSize = DirectDequeueSize(f, r);
    if (local_size <= DirectDeqSize)
    {
        memcpy(chpDest, f, local_size);
    }
    else
    {
        memcpy(chpDest, f, DirectDeqSize);
        memcpy(chpDest + DirectDeqSize, _begin, local_size - DirectDeqSize);
    }

    MoveFront(local_size);

    return local_size;
}

ringBufferSize CRingBuffer::Peek(void *pDest, ringBufferSize iSize)
{
    return Peek(pDest, iSize, _frontPtr, _rearPtr);
}
ringBufferSize CRingBuffer::Peek(void *pDest, ringBufferSize iSize, char *f, char *r)
{
    char *chpDest = reinterpret_cast<char *>(pDest);
    ringBufferSize useSize, DirectDeqSize;

    useSize = GetUseSize(f, r);
    if (iSize > useSize)
        return useSize;
    DirectDeqSize = DirectDequeueSize(f, r);

    if (iSize <= DirectDeqSize)
    {
        memcpy(chpDest, f, iSize);
    }
    else
    {
        memcpy(chpDest, f, DirectDeqSize);
        memcpy(chpDest + DirectDeqSize, _begin, iSize - DirectDeqSize);
    }
    return iSize;
}
void CRingBuffer::ClearBuffer()
{
    _rearPtr = _begin;
    _frontPtr = _begin;
}

ringBufferSize CRingBuffer::DirectEnqueueSize()
{
    return DirectEnqueueSize(_frontPtr, _rearPtr);
}

ringBufferSize CRingBuffer::DirectEnqueueSize(const char *f, const char *r)
{
    if (f <= r && f == _begin)
    {
        return ringBufferSize(_end - r - 1);
    }

    return f <= r ? ringBufferSize(_end - r)
                  : ringBufferSize(f - r - 1);
}

ringBufferSize CRingBuffer::DirectDequeueSize()
{
    return DirectDequeueSize(_frontPtr, _rearPtr);
}

ringBufferSize CRingBuffer::DirectDequeueSize(const char *f, const char *r)
{
    if (f > r && r == _begin)
        return ringBufferSize(_end - f - 1);
    return f <= r ? ringBufferSize(r - f) : ringBufferSize(_end - f);
}

void CRingBuffer::MoveRear(ringBufferSize iSize)
{
    char *pChk;
    char *distance;
    char *oldRear;

    oldRear = _rearPtr;
    pChk = oldRear + iSize;
    distance = reinterpret_cast<char *>(pChk - _end);
    if (_end < pChk)
    {
        pChk = _begin + long long(distance);
    }
    _rearPtr = pChk;

    //InterlockedExchange((unsigned long long *)&_rearPtr, (unsigned long long)pChk);
    // do
    //{
    //     oldRear = _rearPtr;
    //     pChk = oldRear + iSize;
    //     distance = reinterpret_cast<char *>(pChk - _end);
    //     if (_end < pChk)
    //     {
    //         pChk = _begin + long long(distance);
    //     }
    // } while (InterlockedCompareExchange((unsigned long long *)&_rearPtr, (unsigned long long)pChk, (unsigned long long)oldRear) != (unsigned long long)oldRear);
    //// MoveRear(iSize, _rearPtr);
}

void CRingBuffer::MoveFront(ringBufferSize iSize)
{
    char *pChk;
    char *distance;
    char *oldFront;

    oldFront = _frontPtr;
    pChk = oldFront + iSize;
    distance = reinterpret_cast<char *>(pChk - _end);
    if (_end < pChk)
    {
        pChk = _begin + long long(distance);
    }
    // _frontPtr = pChk;
    InterlockedExchange((unsigned long long *)&_frontPtr, (unsigned long long)pChk);
    /* do
     {
         oldFront = _frontPtr;
         pChk = oldFront + iSize;
         distance = reinterpret_cast<char *>(pChk - _end);
         if (_end < pChk)
         {
             pChk = _begin + long long(distance);
         }
     } while (InterlockedCompareExchange((unsigned long long *)&_frontPtr, (unsigned long long)pChk, (unsigned long long)oldFront) != (unsigned long long)oldFront);*/
}
