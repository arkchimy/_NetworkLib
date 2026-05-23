#pragma once
#define WIN32_LEAN_AND_MEAN // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.

#include <Windows.h>
#include <exception>
#include <iostream>

#include <concepts>
#include <type_traits>
#include <string>

#include "../CLeakDetectPool/CLeakDetectPool.h"
#include "../../utility/stHeader.h"

#ifndef RT_ASSERT
#define RT_ASSERT(x) \
    if (!(x))        \
    __debugbreak();
    #endif
using SerializeBufferSize = DWORD;
using ull = unsigned long long;

class MessageException : public std::exception
{
  public:
    enum ErrorType
    {
        HasNotData,
        NotEnoughSpace
    };

    MessageException(ErrorType type, const std::string &msg)
        : _type(type), _msg(msg) {}

    virtual const char *what() const noexcept override
    {
        return _msg.c_str();
    }

    ErrorType type() const noexcept { return _type; }

  private:
    ErrorType _type;
    std::string _msg;
};

template <typename T>
concept Fundamental = std::is_fundamental_v<T>;
struct CMessage
{
    enum en_BufferSize : DWORD
    {
        bufferSize = 2000,
        MaxSize = 3000,
    };
    enum class en_Tag : BYTE
    {
        NORMAL,
        ENCODE,
        DECODE,
 
        ENCODE_BEFORE,
        DECODE_BEFORE,
        _ERROR,
        MAX,
    };

    CMessage();
    ~CMessage();
    void InitMessage();
    CMessage(const CMessage &) = delete;
    CMessage &operator=(const CMessage &) = delete;
    CMessage(CMessage &&) = delete;
    CMessage &operator=(CMessage &&) = delete;

    //void* operator new(size_t size)
    //{
    //    const DWORD PAGE = 4096; // 또는 GetSystemInfo로 취득
    //    size_t dataBytes = ((size + PAGE - 1) / PAGE) * PAGE;
    //    size_t totalBytes = dataBytes + PAGE;

    //    LPVOID base = VirtualAlloc(nullptr, totalBytes,
    //                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    //    RT_ASSERT(base != nullptr);

    //    DWORD oldProtect;
    //    BOOL bRet = VirtualProtect((char *)base + dataBytes, PAGE,
    //                               PAGE_NOACCESS, &oldProtect);
    //    RT_ASSERT(bRet != 0);

    //    return (char *)base + dataBytes - size;
    //}
    //void operator delete(void* ptr)
    //{
    //    size_t base = size_t(ptr) & (~size_t(4095));
    //    VirtualFree((LPVOID)base, 0, MEM_RELEASE);
    //}
    template <Fundamental T>
    CMessage &operator<<(const T data)
    {
        if (_end < _rearPtr + sizeof(data))
        {
            if (_size == en_BufferSize::bufferSize && (size_t)_rearPtr + sizeof(data) < en_BufferSize::MaxSize)
            {
                ReSize();
                HexLog(en_Tag::_ERROR);
            }
            else
            {
                HexLog(en_Tag::_ERROR);
                throw MessageException(MessageException::NotEnoughSpace, "Buffer is fulled\n");
            }
        }

        memcpy(_rearPtr, &data, sizeof(data));
        _rearPtr = _rearPtr + sizeof(data);

        return *this;
    }
    // std::string
    CMessage &operator << (char* const str)
    {
        size_t len = strlen(str);
        if (_end < _rearPtr + len)
        {
            if (_size == en_BufferSize::bufferSize && size_t(_rearPtr + len) < en_BufferSize::MaxSize)
            {
                ReSize();
                HexLog(en_Tag::_ERROR);
            }
            else
            {
                HexLog(en_Tag::_ERROR);
                throw MessageException(MessageException::NotEnoughSpace, "Buffer is fulled\n");
            }
        }
        memcpy(_rearPtr, str, len);
        _rearPtr = _rearPtr + len;

        return *this;
    }
    template <Fundamental T>
    CMessage &operator>>(T &data)
    {
        size_t len = sizeof(T);
        if (_frontPtr + len > _rearPtr)
        {
            throw MessageException(MessageException::HasNotData, "false Packet \n");
        }

        memcpy(&data, _frontPtr, sizeof(data));
        _frontPtr = _frontPtr + sizeof(data);
        return *this;
    }
    CMessage& operator >> (char* const str)
    {
        //뒤에 모든 데이터를 한번에 긁자.

        size_t len = _rearPtr - _frontPtr;
        if (_frontPtr > _rearPtr)
        {
            throw MessageException(MessageException::HasNotData, "false Packet \n");
        }

        memcpy(str, _frontPtr, len);
        _frontPtr = _frontPtr + len;
        return *this;
    }
    void EnCoding();
    bool DeCoding();


    SSIZE_T PutData(PVOID src, SerializeBufferSize size);
    SSIZE_T GetData(PVOID desc, SerializeBufferSize size);

    BOOL ReSize();
    void Peek(char *out, SerializeBufferSize size);

    void HexLog(en_Tag tag = en_Tag::NORMAL, const wchar_t *filename = L"SerializeBuffer_hex.txt");
    SerializeBufferSize _size = en_BufferSize::bufferSize;

    char _begin[MaxSize]{0};
    char *_end = nullptr;

    char *_frontPtr = nullptr;
    char *_rearPtr = nullptr;

    ull ownerID;
    LONG64 iUseCnt = 0;

    BYTE K = 0x32; // 고정 키 
    bool _bLastMessage;
    inline static HANDLE s_BufferHeap;
};
