#pragma once
#define WIN32_LEAN_MEAN
#include <Windows.h>

#include <list>
#include <vector>

#include <cstddef> // offsetof
#include <cstdint> // uint32_t
#include <iostream>
#include <shared_mutex>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

/*
*	======================== Pool에서 주의할 점. ========================
*
        ■ 두 번이상 Release 되는 경우
        => 반환시에 _bActive를 확인하여 2번 반환되는 경우를 방지.

        ■ 반환된 Node가  나의 Pool에서 할당한 것이 아닌 경우.
        => OwnerPool의 Pointer를 들고 반환 시에 stNode에 저장된 OwnerPool과의 비교.

        ■ 반환되지 않은 Node가 존재하는 경우.
        => Pool의 삭제 시  "ActiveCnt == 0 " 체크.

        ■ 반환과 할당 과정에서 요소가 소실되는 경우.
        => Pool의 삭제 시 반환된 Node를 Delete 후 "AllocCnt == 0 " 체크.

        ■ 반환을 하지않는 요소에대한 추적.
        => 할당을 할때마다 Lock을 걸고 ActicveNodes List 를 작성.
                => Touch 함수를 만들고 해당 객체에 대해 접근할떄 호출. __Line__ 정보를 남기기
                        => cap을 걸어두고, 도달 시에 가장 오래된 순으로 정렬


*/
#ifdef POOLTRACE
#define POOL_TOUCH(type, p) reinterpret_cast<decltype(type)::stNode *>(((char *)(p) - offsetof(decltype(type)::stNode, _data)))->Touch(__FILE__, __LINE__)
#else
#define POOL_TOUCH(type, p)
#endif
// POOLTRACE

template <typename T>
class CObjectPool final
{
  private:
    enum : uint64_t
    {
        GuardValue = 0xfdfdfdfdfdfdfdfd,
        AddressMask = 0xffffffff8 << 47,
    };

  public:
    struct stSeqAddress
    {
        // 이 구조체는 8바이트임.
        union
        {
            uint64_t _val; // [seq - 17][ address - 47]
            struct
            {
                uint64_t _address : 47;   // 상위 17비트 사용
                uint64_t _seqNumber : 17; // 하위 47비트를 사용
            };
        };
    };
    struct stLogInfo final
    {
        enum enMode : uint32_t
        {
            Node_Release = 0xdddddddd,
            Node_Alloc = 0xcccccccc,
            None = 0,
        };
        uint64_t _seqNumber = 0;
        void *_address = 0;
        enMode _mode = enMode::None;
        void *_nextAddress = 0;
        DWORD _ThreadID = 0;
    };
    struct stNode final
    {
        explicit stNode(CObjectPool *ownerPool)
            :
#ifdef POOLTRACE
              _bActive(false), _frontGuard(GuardValue), _backGuard(GuardValue), _file(nullptr), _line(0), _lastTime(0),
#endif
              _next(nullptr), _ownerPool(ownerPool)
        {
            _seqAddress._val = (uint64_t)this;
        }
        stNode(const stNode &other) = delete;
        stNode(stNode &&other) = delete;

        stNode &operator=(const stNode &other) = delete;
        stNode &operator=(stNode &&other) = delete;

        bool operator<(const stNode &other)
        {
            return this->_lastTime < other._lastTime;
        }

        void Touch(const char *file, int line)
        {
#ifdef POOLTRACE
            _file = file;
            _line = line;
            _lastTime = timeGetTime();
#endif
        }

#ifdef POOLTRACE
        uint64_t _frontGuard;
        T _data{};
        uint64_t _backGuard;
        stNode *_next;
        const char *_file;
        int _line;
        int32_t _lastTime;
        bool _bActive;
#else
        T _data{};
        stNode *_next;
#endif // POOLTRACE
       // [seqNumber : 17][Address : 47 ] 구조체
        stSeqAddress _seqAddress{0};
        CObjectPool *_ownerPool;
    };

  public:
    CObjectPool()
        : _AllocNodeCnt(0), _ActiveNodeCnt(0), _capacity(INT_MAX), _seqNumber(0)
    {
#ifdef POOLTRACE
        InitializeSRWLock(&_srw_lock);
#endif
        _top = _dummy._seqAddress;
#ifdef POOLTEST
        _loginfos.resize(20);
        memset(&_logFront, 0xfd, sizeof(stLogInfo));
        memset(&_logBack, 0xfd, sizeof(stLogInfo));
#endif
    }
    ~CObjectPool()
    {
        stNode *oldTop;
        stNode *node = reinterpret_cast<stNode *>(_top._address);
        if (_ActiveNodeCnt != 0)
        {
            // 반환되지 않은 노드가 존재
#ifdef POOLTRACE
            CatchLeak();
#endif
            __debugbreak();
        }

        while (node != &_dummy)
        {
            oldTop = node;
            node = oldTop->_next;
            delete oldTop;
            _AllocNodeCnt--;
        }

        if (_AllocNodeCnt != 0)
            __debugbreak();
    }

  public:
    void *Alloc();
    void Release(void *ptr);
    void SetCapacity(uint32_t capacity) { _capacity = capacity; }
#ifdef POOLTRACE
    void CatchLeak();
#endif // DEBUG

    uint64_t GetActiveNodeCnt() const
    {
        return _ActiveNodeCnt;
    }

    __int64 GetAllocNodeCnt() const
    {
        return _AllocNodeCnt;
    }

  private:
    stSeqAddress _top;
    // 해당 ObjectPool 에서 할당한 Node수
    uint64_t _AllocNodeCnt;
    // 아직 반환되지않은 Node의 Cnt
    uint64_t _ActiveNodeCnt;

    stNode _dummy{this};

    uint32_t _capacity;
    uint32_t _seqNumber;
#ifdef POOLTRACE
    std::list<stNode *> _ActiveNodes;
    SRWLOCK _srw_lock;
#endif

#ifdef POOLTEST
    uint64_t _logIdx = -1;
    stLogInfo _logFront;
    std::vector<stLogInfo> _loginfos;
    stLogInfo _logBack;
#endif
};

template <typename T>
inline void *CObjectPool<T>::Alloc()
{
    stNode *retNode;
    stNode *newNode;
    stNode *newTopNode;

    stSeqAddress newTop;
    stSeqAddress oldTop;

    do
    {
        oldTop = _top;

        if (reinterpret_cast<stNode *>(oldTop._address) == &_dummy)
        {
            uint64_t local_AllocNodeCnt;
            newNode = new stNode(this);
            local_AllocNodeCnt = _interlockedincrement64((long long *)&_AllocNodeCnt);
#ifdef POOLTRACE
            if (_capacity == _AllocNodeCnt)
            {
                CatchLeak();
            }
            newNode->_bActive = true;
            AcquireSRWLockExclusive(&_srw_lock);
            _ActiveNodes.push_back(newNode);
            ReleaseSRWLockExclusive(&_srw_lock);
            newNode->_lastTime = INT_MAX;
#endif
            _interlockedincrement64((long long *)&_ActiveNodeCnt);

            return (char *)newNode + offsetof(stNode, _data);
        }

        retNode = reinterpret_cast<stNode *>((uint64_t)(oldTop._address));
        newTopNode = retNode->_next;
        // uint64_t local_seqNumber = _InterlockedIncrement(&_seqNumber);
        // newTopNode->_seqAddress._seqNumber = local_seqNumber;
        newTop = newTopNode->_seqAddress;

    } while (_InterlockedCompareExchange64((volatile LONG64 *)&_top._val, (LONG64)newTop._val, (LONG64)oldTop._val) != (LONG64)oldTop._val);

#ifdef POOLTRACE
    if (_capacity == _AllocNodeCnt)
    {
        CatchLeak();
    }
    retNode->_bActive = true;
    if (_dummy._bActive)
        __debugbreak();
    AcquireSRWLockExclusive(&_srw_lock);
    _ActiveNodes.push_back(retNode);
    ReleaseSRWLockExclusive(&_srw_lock);
    retNode->_lastTime = INT_MAX;
#endif

#ifdef POOLTEST
    uint64_t idx;
    idx = _interlockedincrement64((volatile long long *)&_logIdx);
    newTopNode = retNode->_next;

    _loginfos[idx % _loginfos.size()]._ThreadID = GetCurrentThreadId();
    _loginfos[idx % _loginfos.size()]._mode = stLogInfo::enMode::Node_Alloc;

    _loginfos[idx % _loginfos.size()]._nextAddress = (char *)newTopNode + offsetof(stNode, _data);
    _loginfos[idx % _loginfos.size()]._seqNumber = idx;
    _loginfos[idx % _loginfos.size()]._address = (char *)retNode + offsetof(stNode, _data);

#endif
    // uint64_t local_seqNumber = _InterlockedIncrement(&_seqNumber);
    // newTopNode->_seqAddress._seqNumber = local_seqNumber;
    _interlockedincrement64((long long *)&_ActiveNodeCnt);
    return (char *)retNode + offsetof(stNode, _data);
}

template <typename T>
inline void CObjectPool<T>::Release(void *ptr)
{
    void *Ptr = (char *)ptr - offsetof(stNode, _data);
    stNode *retNode = static_cast<stNode *>(Ptr);
    stSeqAddress oldTop;
    stSeqAddress newTop;
    stNode *oldTopNode;

    uint64_t local_seqNumber;
    local_seqNumber = _InterlockedIncrement(&_seqNumber);

#ifdef POOLTRACE
    // 할당한 Pool이 아닐 경우
    if (retNode->_ownerPool != this)
        __debugbreak();
    // 2번 Release
    if (retNode->_bActive == false)
        __debugbreak();
    // 버퍼 오버런
    if (retNode->_frontGuard != GuardValue)
        __debugbreak();
    if (retNode->_backGuard != GuardValue)
        __debugbreak();
#endif

#ifdef POOLTRACE
    retNode->_bActive = false;
    AcquireSRWLockExclusive(&_srw_lock);
    for (auto iter = _ActiveNodes.begin(); iter != _ActiveNodes.end();)
    {
        if (*iter == retNode)
        {
            _ActiveNodes.erase(iter);
            break;
        }
        iter++;
    }
    ReleaseSRWLockExclusive(&_srw_lock);
#endif
    do
    {
        oldTop = _top;
        oldTopNode = reinterpret_cast<stNode *>(oldTop._address);
        retNode->_next = oldTopNode;

        retNode->_seqAddress._seqNumber = local_seqNumber;
        newTop = retNode->_seqAddress;
    } while (_InterlockedCompareExchange64((volatile LONG64 *)&_top._val,
                                           (LONG64)newTop._val, (LONG64)oldTop._val) != (LONG64)oldTop._val);

    _InterlockedDecrement64((long long *)&_ActiveNodeCnt);
#ifdef POOLTEST
    uint64_t idx;
    idx = _interlockedincrement64((volatile long long *)&_logIdx);

    _loginfos[idx % _loginfos.size()]._ThreadID = GetCurrentThreadId();
    _loginfos[idx % _loginfos.size()]._mode = stLogInfo::enMode::Node_Release;
    _loginfos[idx % _loginfos.size()]._nextAddress = (char *)oldTopNode + offsetof(stNode, _data);

    _loginfos[idx % _loginfos.size()]._seqNumber = idx;
    _loginfos[idx % _loginfos.size()]._address = ptr;

#endif
}
#ifdef POOLTRACE
template <typename T>
inline void CObjectPool<T>::CatchLeak()
{
    AcquireSRWLockExclusive(&_srw_lock);
    _ActiveNodes.sort([](const CObjectPool<T>::stNode *a, const CObjectPool<T>::stNode *b)
                      { return a->_lastTime < b->_lastTime; });
    __debugbreak();
    ReleaseSRWLockExclusive(&_srw_lock);
}
#endif // DEBUG
