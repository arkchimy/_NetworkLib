#pragma once
namespace utility
{
using ringBufferSize = __int32;

class RingBuffer
{

  public:
    RingBuffer() = delete;
    RingBuffer(ringBufferSize iBufferSize);

    ~RingBuffer();

    char *GetBeginPtr() const { return mBegin; }

    char *GetFrontPtr() const { return mFrontPtr; }
    char *GetRearPtr() const { return mRearPtr; }

    ringBufferSize GetUseSize() const;
    ringBufferSize GetUseSize(const char *f, const char *r) const;
    ringBufferSize GetFreeSize() const;
    ringBufferSize GetFreeSize(const char *f, const char *r) const;

    ringBufferSize Enqueue(const void *pSrc, ringBufferSize iSize);
    ringBufferSize Dequeue(void *chpDest, ringBufferSize iSize);

    ringBufferSize Peek(void *chpDest, ringBufferSize iSize) const;
    ringBufferSize Peek(void *pDest, ringBufferSize iSize, const char *f, const char *r) const;

    void ClearBuffer();

    ringBufferSize DirectEnqueueSize() const;
    ringBufferSize DirectEnqueueSize(const char *f, const char *r) const;

    ringBufferSize DirectDequeueSize() const;
    ringBufferSize DirectDequeueSize(const char *f, const char *r) const;

    void MoveRear(ringBufferSize iSize);
    void MoveFront(ringBufferSize iSize);

  private:
    char *mBegin;
    char *mEnd;

    char *mFrontPtr;
    char *mRearPtr;
};
} // namespace utility
