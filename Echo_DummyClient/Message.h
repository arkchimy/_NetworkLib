#pragma once

#include <Windows.h>
#include <exception>
#include <iostream>

#include <concepts>
#include <string>
#include <type_traits>

#include "Common.h"

namespace utility
{

using SerializeBufferSize = uint32_t;
using ull = unsigned long long;

enum eBufferSize
{
    BufferSize = 2000,
    MaxSize = 3000,
};
enum eTag
{
    NORMAL,
    ENCODE,
    DECODE,
    ENCODE_BEFORE,
    DECODE_BEFORE,
    Error,
    MAX,
};

class MessageException : public std::exception
{
  public:
    enum class eErrorType
    {
        HasNotData,
        NotEnoughSpace
    };

    MessageException(eErrorType type, const std::string &msg)
        : mType(type), mMsg(msg) {}

    virtual const char *what() const noexcept override
    {
        return mMsg.c_str();
    }

    eErrorType type() const noexcept { return mType; }

  private:
    eErrorType mType;
    std::string mMsg;
};

template <typename T>
concept Fundamental = std::is_fundamental_v<T>;
class Message
{
  public:
    Message();
    Message(const Message &) = delete;
    Message(Message &&) = delete;

    Message &operator=(const Message &) = delete;
    Message &operator=(Message &&) = delete;

    ~Message();

  public:
    void InitMessage(__int64 sessionID, __int8 randKey);

    template <Fundamental T>
    Message &operator<<(const T data)
    {
        if (mEnd < mRearPtr + sizeof(data))
        {
            if (mSize == (DWORD)eBufferSize::BufferSize && (size_t)mRearPtr + sizeof(data) < (size_t)eBufferSize::MaxSize)
            {
                Resize();
                HexLog(eTag::Error);
            }
            else
            {
                HexLog(eTag::Error);
                throw MessageException(MessageException::eErrorType::NotEnoughSpace, "Buffer is fulled\n");
            }
        }

        memcpy(mRearPtr, &data, sizeof(data));
        mRearPtr = mRearPtr + sizeof(data);

        return *this;
    }

    Message &operator<<(char *const str)
    {
        size_t len = strlen(str);
        if (mEnd < mRearPtr + len)
        {
            if (mSize == (DWORD)eBufferSize::BufferSize && size_t(mRearPtr + len) < (size_t)eBufferSize::MaxSize)
            {
                Resize();
                HexLog(eTag::Error);
            }
            else
            {
                HexLog(eTag::Error);
                throw MessageException(MessageException::eErrorType::NotEnoughSpace, "Buffer is fulled\n");
            }
        }
        memcpy(mRearPtr, str, len);
        mRearPtr = mRearPtr + len;

        return *this;
    }

    template <Fundamental T>
    Message &operator>>(T &data)
    {
        size_t len = sizeof(T);
        if (mFrontPtr + len > mRearPtr)
        {
            throw MessageException(MessageException::eErrorType::HasNotData, "false Packet \n");
        }

        memcpy(&data, mFrontPtr, sizeof(data));
        mFrontPtr = mFrontPtr + sizeof(data);
        return *this;
    }

    Message &operator>>(char *const str)
    {
        size_t len = mRearPtr - mFrontPtr;
        if (mFrontPtr > mRearPtr)
        {
            throw MessageException(MessageException::eErrorType::HasNotData, "false Packet \n");
        }

        memcpy(str, mFrontPtr, len);
        mFrontPtr = mFrontPtr + len;
        return *this;
    }

    void EnCoding(char FK);
    bool DeCoding(char FK);

    SSIZE_T PutData(PVOID src, SerializeBufferSize size);
    SSIZE_T GetData(PVOID desc, SerializeBufferSize size);
    __int64 GetOwnerID() { return mOwnerID; }
    __int8 GetRandomKey() { return mRandKey; }
    char *GetFrontPtr() { return mFrontPtr; }

    BOOL Resize();
    void Peek(char *out, SerializeBufferSize size) const;

    void HexLog(eTag tag = eTag::NORMAL, const wchar_t *filename = L"SerializeBuffer_hex.txt");
    size_t GetUseSize();

  private:
    char mBegin[(DWORD)eBufferSize::MaxSize]{0};
    char *mEnd = nullptr;

    char *mFrontPtr = nullptr;
    char *mRearPtr = nullptr;

    __int64 mOwnerID;
    LONG64 UseCnt = 0;

    __int8 mFixedKey = 0x00;
    __int8 mRandKey = 0x00;

    bool BLastMessage;
    DWORD mSize = (DWORD)eBufferSize::BufferSize;
};

} // namespace utility