#include <strsafe.h>

#include "Message.h"
#include "../Header.h"

static int g_mode = 0;

static const wchar_t *format[utility::eTag::MAX] =
    {
        L"\n%s\n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
};
static const wchar_t *Stringformat[utility::eTag::MAX] =
    {
        L"==================================================================================================================",
        L"Dummy가 보낸 인코딩 후 데이터  ",
        L"Server가 준 디코딩 후 데이터 ",
        L"Dummy가 보낸 인코딩 전 데이터  ",
        L"Server가 준 디코딩 전 데이터  ",
        L"dif Data",
};

namespace utility
{
Message::Message()
    : mOwnerID(GetCurrentThreadId()), BLastMessage(false)
{
    mEnd = mBegin + mSize;
    mFrontPtr = mBegin;
    mRearPtr = mFrontPtr;
}

Message::~Message()
{
    mSize = (DWORD)eBufferSize::BufferSize;
    mFrontPtr = mBegin;
    mRearPtr = mBegin;
    mEnd = mBegin + mSize;

    _interlockedexchange64(&UseCnt, 1);
}

void Message::InitMessage(ull sessionID, BYTE RandKey)
{
    mOwnerID = sessionID;
    mRandKey = RandKey;
    mSize = (DWORD)eBufferSize::BufferSize;
    mFrontPtr = mBegin;
    mRearPtr = mBegin;
    mEnd = mBegin + mSize;
    BLastMessage = false;
}

void Message::EnCoding()
{

}

bool Message::DeCoding()
{
    return true;
}

SSIZE_T Message::PutData(PVOID src, SerializeBufferSize size)
{
    char *r = mRearPtr;
    if (r + size > mEnd)
        throw MessageException(MessageException::eErrorType::NotEnoughSpace, "Buffer OverFlow\n");
    memcpy(r, src, size);
    mRearPtr += size;
    return mRearPtr - r;
}

SSIZE_T Message::GetData(PVOID desc, SerializeBufferSize size)
{
    char *f = mFrontPtr;
    if (f + size > mRearPtr)
    {
        throw MessageException(MessageException::eErrorType::HasNotData, "buffer has not Data\n");
    }
    memcpy(desc, f, size);
    mFrontPtr += size;
    return mFrontPtr - f;
}

BOOL Message::Resize()
{
    SerializeBufferSize useSize;

    mSize = (DWORD)eBufferSize::MaxSize;

    if (mFrontPtr > mRearPtr)
    {
        useSize = SerializeBufferSize(mRearPtr - mFrontPtr);
        memcpy(mEnd, mBegin, useSize);
    }
    else
        useSize = SerializeBufferSize(mFrontPtr - mRearPtr);

    mEnd = mBegin + mSize;
    mFrontPtr = mBegin;
    mRearPtr = mBegin + useSize;
    printf("Resize\n");
    return TRUE;
}

void Message::Peek(char *out, SerializeBufferSize size) const
{
    char *f = mFrontPtr;
    if (f + size > mRearPtr)
        throw MessageException(MessageException::eErrorType::HasNotData, "buffer has not Data\n");
    memcpy(out, f, size);
}

void Message::HexLog(eTag tag, const wchar_t *filename)
{
    int current = 0;

    wchar_t *hexBuffer = (wchar_t *)malloc((DWORD)eBufferSize::MaxSize * 3 + 4);
    wchar_t *printBuffer = (wchar_t *)malloc((DWORD)eBufferSize::MaxSize * 4);
    if (hexBuffer == nullptr)
        __debugbreak();
    if (printBuffer == nullptr)
        __debugbreak();

    wchar_t wordSet[] = L"0123456789ABCDEF";
    BYTE data;

    for (; &mBegin[current] != mEnd; current++)
    {
        data = mBegin[current];

        hexBuffer[3 * current + 0] = wordSet[data >> 4];
        hexBuffer[3 * current + 1] = wordSet[data & 0xF];
        hexBuffer[3 * current + 2] = L' ';
    }
    hexBuffer[3 * current + 0] = L'\0';

    FILE *file;
    file = nullptr;
    while (file == nullptr)
    {
        _wfopen_s(&file, filename, L"a+ ,ccs=UTF-16LE");
    }
    if ((BYTE)tag == 0)
    {
        StringCchPrintfW(printBuffer, (DWORD)eBufferSize::MaxSize * 4 / sizeof(wchar_t), format[(BYTE)tag], Stringformat[(BYTE)tag]);
    }
    else
        StringCchPrintfW(printBuffer, (DWORD)eBufferSize::MaxSize * 4 / sizeof(wchar_t), format[(BYTE)tag], Stringformat[(BYTE)tag], hexBuffer);
    fwrite(printBuffer, 2, wcslen(printBuffer), file);
    current = 0;
    for (; &mBegin[current] != mEnd; current++)
    {
        hexBuffer[3 * current + 0] = L' ';
        hexBuffer[3 * current + 1] = L' ';
        hexBuffer[3 * current + 2] = L' ';
    }
    hexBuffer[3 * current + 0] = L'\n';
    hexBuffer[3 * current + 1] = L'\0';

    hexBuffer[3 * (mFrontPtr - mBegin)] = L'F';
    hexBuffer[3 * (mRearPtr - mBegin)] = L'R';
    if ((BYTE)tag != 0)
    {
        fwrite(hexBuffer, 2, wcslen(hexBuffer), file);
    }
    fclose(file);

    free(hexBuffer);
    free(printBuffer);
}

size_t Message::GetUseSize()
{
    if (mFrontPtr <= mRearPtr)
    {
        return mRearPtr - mFrontPtr;
    }

    return mEnd - mFrontPtr + mRearPtr - mBegin;
}

}; // namespace utility