#include "pch.h"
#include "RingBuffer.h"
#include <Windows.h>
#include "Common.h"

namespace utility
{
    RingBuffer::RingBuffer(ringBufferSize iBufferSize)
    {
        mBegin = MY_NEW char[iBufferSize];
        if (mBegin == nullptr)
        {
            __debugbreak();
        }
        mEnd = mBegin + iBufferSize;
        ClearBuffer();
    }

    RingBuffer::~RingBuffer()
    {
        MY_DELETE[] mBegin;
    }

    ringBufferSize RingBuffer::GetUseSize() const
    {
        return GetUseSize(mFrontPtr, mRearPtr);
    }

    ringBufferSize RingBuffer::GetUseSize(const char *f, const char *r) const
    {
        return f <= r ? ringBufferSize(r - f)
                      : ringBufferSize(mEnd - f + r - mBegin);
    }

    ringBufferSize RingBuffer::GetFreeSize() const
    {
        return GetFreeSize(mFrontPtr, mRearPtr);
    }

    ringBufferSize RingBuffer::GetFreeSize(const char *f, const char *r) const
    {
        return f <= r ? ringBufferSize((mEnd - r) + (f - mBegin) - 1)
                      : ringBufferSize(f - r - 1);
    }

    ringBufferSize RingBuffer::Enqueue(const void *pSrc, ringBufferSize iSize)
    {
        ringBufferSize directEnQSize;
        ringBufferSize freeSize;
        ringBufferSize localSize = iSize;

        const char *chpSrc;
        char *f;
        char *r;
        f = mFrontPtr;
        r = mRearPtr;

        chpSrc = reinterpret_cast<const char *>(pSrc);

        directEnQSize = DirectEnqueueSize(f, r);
        freeSize = GetFreeSize(f, r);

        if (freeSize < localSize)
        {
            return false;
        }

        if (localSize <= directEnQSize)
        {
            memcpy(r, chpSrc, localSize);
        }
        else
        {
            memcpy(r, chpSrc, directEnQSize);
            memcpy(mBegin, chpSrc + directEnQSize, static_cast<size_t>(localSize - directEnQSize));
        }

        MoveRear(localSize);
        return localSize;
    }

    ringBufferSize RingBuffer::Dequeue(void *pDest, ringBufferSize iSize)
    {
        char *chpDest = reinterpret_cast<char *>(pDest);
        char *f;
        char *r;
        ringBufferSize directDeqSize;
        ringBufferSize useSize;
        ringBufferSize localSize;

        localSize = iSize;
        f = mFrontPtr;
        r = mRearPtr;

        useSize = GetUseSize(f, r);

        if (useSize < localSize)
        {
            return false;
        }

        directDeqSize = DirectDequeueSize(f, r);
        if (localSize <= directDeqSize)
        {
            memcpy(chpDest, f, localSize);
        }
        else
        {
            memcpy(chpDest, f, directDeqSize);
            memcpy(chpDest + directDeqSize, mBegin, static_cast<size_t>(localSize - directDeqSize));
        }

        MoveFront(localSize);

        return localSize;
    }

    ringBufferSize RingBuffer::Peek(void *pDest, ringBufferSize iSize) const
    {
        return Peek(pDest, iSize, mFrontPtr, mRearPtr);
    }

    ringBufferSize RingBuffer::Peek(void *pDest, ringBufferSize iSize, const char *f, const char *r) const
    {
        char *chpDest = reinterpret_cast<char *>(pDest);
        ringBufferSize useSize;
        ringBufferSize directDeqSize;

        useSize = GetUseSize(f, r);
        if (iSize > useSize)
        {
            return useSize;
        }
        directDeqSize = DirectDequeueSize(f, r);

        if (iSize <= directDeqSize)
        {
            memcpy(chpDest, f, iSize);
        }
        else
        {
            memcpy(chpDest, f, directDeqSize);
            memcpy(chpDest + directDeqSize, mBegin, static_cast<size_t>(iSize - directDeqSize));
        }
        return iSize;
    }

    void RingBuffer::ClearBuffer()
    {
        mRearPtr = mBegin;
        mFrontPtr = mBegin;
    }

    ringBufferSize RingBuffer::DirectEnqueueSize() const
    {
        return DirectEnqueueSize(mFrontPtr, mRearPtr);
    }

    ringBufferSize RingBuffer::DirectEnqueueSize(const char *f, const char *r) const
    {
        if (f <= r && f == mBegin)
        {
            return ringBufferSize(mEnd - r - 1);
        }

        return f <= r ? ringBufferSize(mEnd - r)
                      : ringBufferSize(f - r - 1);
    }

    ringBufferSize RingBuffer::DirectDequeueSize() const
    {
        return DirectDequeueSize(mFrontPtr, mRearPtr);
    }

    ringBufferSize RingBuffer::DirectDequeueSize(const char *f, const char *r) const
    {
        return f <= r ? ringBufferSize(r - f) : ringBufferSize(mEnd - f);
    }

    void RingBuffer::MoveRear(ringBufferSize iSize)
    {
        char *pChk;
        long long distance;
        char *oldRear;

        oldRear = mRearPtr;
        pChk = oldRear + iSize;
        distance = pChk - mEnd;
        if (mEnd < pChk)
        {
            pChk = mBegin + distance;
        }
        mRearPtr = pChk;
    }

    void RingBuffer::MoveFront(ringBufferSize iSize)
    {
        char *pChk;
        long long distance;
        char *oldFront;

        oldFront = mFrontPtr;
        pChk = oldFront + iSize;
        distance = pChk - mEnd;
        if (mEnd < pChk)
        {
            pChk = mBegin + distance;
        }
        mFrontPtr = pChk;
    }
}; // namespace utility