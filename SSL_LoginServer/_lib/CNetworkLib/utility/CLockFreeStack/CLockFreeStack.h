#pragma once
#include <Windows.h>
#include "../CLeakDetectPool/CLeakDetectPool.h"

#define INFO_BUFFER_MAX 100
#define DELETE_MODE 0xDDDDDDDD
#define PUSH_MODE 0xEEEEEEEE

#ifndef ONCE_DEFINE
    #define ONCE_DEFINE
    #define ADDR_MASK 0x00007FFFFFFFFFFF
#endif

template <typename T>
class CLockFreeStack
{
    struct stNode
    {
        T data;
        stNode *_next;
    };
    struct stDebug
    {
        LONG64 seqNumber; // 0 + 8 = 8
        DWORD threadID;   // 8 + (4) + 4 = 16
        DWORD mode;
        stNode *targetNode;          // 16 + 8 = 24
        stNode *targetNode_nextNode; // 24 + 8 = 32

        stNode *newTop;          // 32 + 8 = 40
        stNode *newTop_nextNode; // 40 + 8 = 48
    }; // 48

  public:
    CLockFreeStack()
    {
        dummy = reinterpret_cast<stNode *>(m_pool.Alloc());
        m_top = dummy;
        ZeroMemory(infos, sizeof(infos));

    }
    ~CLockFreeStack()
    {
        stNode *newTop;

        while (m_size > 0)
        {
            newTop = reinterpret_cast<stNode *>((LONG64)m_top & ADDR_MASK)->_next;
            m_pool.Release(reinterpret_cast<stNode *>((LONG64)m_top & ADDR_MASK));
            m_top = newTop;

            m_size--;
        }

        if (m_top != dummy)
        {
            __debugbreak();
        }

        m_pool.Release(reinterpret_cast<stNode *>((LONG64)dummy & ADDR_MASK));
    }
    void Push(T data)
    {
        stNode *oldTop;
        stNode *newNode = reinterpret_cast<stNode *>(m_pool.Alloc());
        LONG64 local_seqNumber;
        newNode->data = data;
        local_seqNumber = _interlockedincrement64(&m_seqNumber);
        newNode = reinterpret_cast<stNode *>((LONG64)newNode | ((LONG64)local_seqNumber << 47));

        do
        {
            oldTop = m_top;
            reinterpret_cast<stNode *>((LONG64)newNode & ADDR_MASK)->_next = oldTop;
        } while (InterlockedCompareExchangePointer((void **)&m_top, newNode, oldTop) != oldTop);

        _interlockedincrement64(&m_size);
        LONG64 temp;
        {
             local_seqNumber = _interlockedincrement64(&m_debugSeqNumber);

            infos[local_seqNumber % INFO_BUFFER_MAX].seqNumber = local_seqNumber;
            infos[local_seqNumber % INFO_BUFFER_MAX].threadID = GetCurrentThreadId();
            infos[local_seqNumber % INFO_BUFFER_MAX].mode = PUSH_MODE;
    
            infos[local_seqNumber % INFO_BUFFER_MAX].targetNode = reinterpret_cast<stNode *>((LONG64)newNode & ADDR_MASK);
            temp = (LONG64) reinterpret_cast<stNode *>((LONG64)newNode & ADDR_MASK)->_next;
            temp = temp & ADDR_MASK;

            infos[local_seqNumber % INFO_BUFFER_MAX].targetNode_nextNode = reinterpret_cast<stNode *>(temp);

            infos[local_seqNumber % INFO_BUFFER_MAX].newTop = reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK);

            temp = (LONG64) reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK)->_next;
            temp = temp & ADDR_MASK;

            infos[local_seqNumber % INFO_BUFFER_MAX].newTop_nextNode = reinterpret_cast<stNode *>(temp);
        }
    }
    bool Pop(T& outData)
    {
        stNode *newTop;
        stNode *oldTop;
  
        LONG64 local_seqNumber;
        long long size;

        size = _interlockeddecrement64(&m_size);
        if (size < 0)
        {
            _interlockedincrement64(&m_size);
            return false;
        }
        do
        {
            oldTop = m_top;
            if (oldTop == dummy)
            {
               newTop = oldTop ;
               continue;
            }
            newTop = reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK)->_next;
            outData = reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK)->data;

        } while (InterlockedCompareExchangePointer((void **)&m_top, newTop, oldTop) != oldTop);

        

        LONG64 temp;
        {
            local_seqNumber = _interlockedincrement64(&m_debugSeqNumber);
  
            infos[local_seqNumber % INFO_BUFFER_MAX].seqNumber = local_seqNumber;
            infos[local_seqNumber % INFO_BUFFER_MAX].threadID = GetCurrentThreadId();
            infos[local_seqNumber % INFO_BUFFER_MAX].mode = DELETE_MODE;

     
            infos[local_seqNumber % INFO_BUFFER_MAX].targetNode = reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK);
            temp = (LONG64) reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK)->_next;
            temp = temp & ADDR_MASK;
            infos[local_seqNumber % INFO_BUFFER_MAX].targetNode_nextNode = reinterpret_cast<stNode *>(temp);


            infos[local_seqNumber % INFO_BUFFER_MAX].newTop = reinterpret_cast<stNode *>((LONG64)newTop & ADDR_MASK);
            temp = (LONG64)reinterpret_cast<stNode *>((LONG64)newTop & ADDR_MASK)->_next;
            temp = temp & ADDR_MASK;
            infos[local_seqNumber % INFO_BUFFER_MAX].newTop_nextNode = reinterpret_cast<stNode *> (temp);

        }

        m_pool.Release(reinterpret_cast<stNode *>((LONG64)oldTop & ADDR_MASK));
        return true;
    }

    LONG64 m_debugSeqNumber = -1;
    LONG64 m_seqNumber = 0;

    stDebug infos[INFO_BUFFER_MAX];
    LONG64 m_size = 0;

    CLeakDetectPool<T> m_pool;
    stNode *m_top;
    stNode *dummy;
};