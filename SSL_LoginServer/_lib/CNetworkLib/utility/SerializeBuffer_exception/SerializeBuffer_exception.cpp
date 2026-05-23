// SerializeBuffer_MT.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//
#include <strsafe.h>
#include "../Parser/Parser.h"
#include "../SerializeBuffer_exception/SerializeBuffer_exception.h"

static int g_mode = 0;

static const wchar_t *format[(BYTE)CMessage::en_Tag::MAX] =
    {
        L"\n%s\n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
        L"\n  %-15s  \n%s   \n",
};
static const wchar_t *Stringformat[(BYTE)CMessage::en_Tag::MAX] = 
{
    L"==================================================================================================================",
    L"Dummy가 보낸 인코딩 후 데이터  ",
    L"Server가 준 디코딩 후 데이터 ",
    L"Dummy가 보낸 인코딩 전 데이터  ",
    L"Server가 준 디코딩 전 데이터  ",
    L"dif Data",

};


CMessage::CMessage()
    : ownerID(GetCurrentThreadId()), _bLastMessage(false)
{

    _end = _begin + _size;
    _frontPtr = _begin ;
    _rearPtr = _frontPtr;
}

CMessage::~CMessage()
{
    _size = en_BufferSize::bufferSize;

    _frontPtr = _begin;
    _rearPtr = _begin;
    _end = _begin + _size;

    _interlockedexchange64(&iUseCnt, 1);
}

void CMessage::InitMessage()
{
    _size = en_BufferSize::bufferSize;

    _frontPtr = _begin;
    _rearPtr = _begin;
    _end = _begin + _size;
    _bLastMessage = false;

    _interlockedexchange64(&iUseCnt, 1);
}

void CMessage::EnCoding( )
{
    SerializeBufferSize len;
    BYTE RK;
    BYTE total = 0;
    int current = 1;

    BYTE P = 0;
    BYTE E = 0;
    char *local_Front;

    bool bDebug = false;

    RK = rand() % UCHAR_MAX;

    local_Front = _begin + offsetof(stHeader, byCheckSum);
    len = SerializeBufferSize(_rearPtr - local_Front);

    //struct stHeader
    //{
    //  public:
    //    BYTE byCode;
    //    SHORT sDataLen;
    //    BYTE byRandKey;
    //    BYTE byCheckSum;
    //};
    memcpy(_begin + offsetof(stHeader, byRandKey), &RK, sizeof(BYTE));

    //if (local_Front[1] == 0x06)
    //    bDebug = true;

    for (SerializeBufferSize i = 1; i < len; i++)
    {
        total += local_Front[i];
    }
    memcpy(local_Front, &total, sizeof(total));
    //if (bDebug)
    //    HexLog(en_Tag::ENCODE_BEFORE);

    for (; &local_Front[current - 1] != _rearPtr; current++)
    {
        BYTE D1 = local_Front[current - 1];
        BYTE b = (P + RK + current);

        P = D1 ^ b;
        E = P ^ (E + K + current);
        local_Front[current - 1] = E;
    }
    //if (bDebug)
     //   HexLog(en_Tag::ENCODE);
}

bool CMessage::DeCoding( )
{
    BYTE P1 = 0, P2;
    BYTE E1 = 0, E2;
    BYTE D1 = 0, D2;
    char total = 0;
    BYTE RK;
    char *local_Front;

    // 디코딩의 msg는 링버퍼에서 꺼낸 데이터로 내가 작성하는
    SerializeBufferSize len;
    int current = 1;
    //HexLog(en_Tag::DECODE_BEFORE);
    // struct stHeader
    //{
    //  public:
    //    BYTE byCode;
    //    BYTE byType; //
    //    SHORT sDataLen;
    //    BYTE byRandKey;
    //    BYTE byCheckSum;

    //};
    //HexLog(en_Tag::DECODE_BEFORE);
    RK = *(_begin + offsetof(stHeader, byRandKey));

    //HexLog();
    local_Front = _begin + offsetof(stHeader, byCheckSum);
    len = (SerializeBufferSize)(_rearPtr - local_Front);
    if (len > _size)
        return false;
    // 2기준
    // D2 ^ (P1 + RK + 2) = P2
    // P2 ^ (E1 + K + 2) = E2

    // E2 ^ (E1 + K + 2) = P2
    // P2 ^ (P1 + RK + 2) = D2
    for (; &local_Front[current - 1] != _rearPtr; current++)
    {
        E2 = local_Front[current - 1];
        P2 = E2 ^ (E1 + K + current);
        E1 = E2;
        D2 = P2 ^ (P1 + RK + current);
        P1 = P2;
        local_Front[current - 1] = D2;
    }

    for (SerializeBufferSize i = 1; i < len; i++)
    {
        total += local_Front[i];
    }
  
    if (local_Front[0] != total)
    {
        // Attack : 내가 만든 패킷이 아닐경우.
        return false;
    }
    //HexLog(en_Tag::DECODE);
    return true;
}

SSIZE_T CMessage::PutData(PVOID src, SerializeBufferSize size)
{
    char *r = _rearPtr;
    if (r + size > _end)
        throw MessageException(MessageException::NotEnoughSpace, "Buffer OverFlow\n");
    memcpy(r, src, size);
    _rearPtr += size;
    return _rearPtr - r;
}

SSIZE_T CMessage::GetData(PVOID desc, SerializeBufferSize size)
{
    char *f = _frontPtr;
    if (f + size > _rearPtr)
    {
        throw MessageException(MessageException::HasNotData, "buffer has not Data\n");
    }
    memcpy(desc, f, size);
    _frontPtr += size;
    return _frontPtr - f;
}

// 지금 까지의 모든 데이터를 새로 할당받은 메모리에 복사후 그대로 진행해야 함.
BOOL CMessage::ReSize()
{
    // 직렬화 버퍼는 넣고 뺴고는 하나의 쓰레드에서 할 것으로 예상이 된다.
    SerializeBufferSize UseSize;

    _size = en_BufferSize::MaxSize;

    // TODO : 복사 범위 생각해보기.
    //    f     =>    r 인 경우
    // case : _frontPtr < _rearPtr  옮길 데이터가 없는 상황.
    //    r       f   인 경우 데이터를 옮겨야 함.
    if (_frontPtr > _rearPtr)
    {
        UseSize = SerializeBufferSize(_rearPtr - _frontPtr);
        memcpy(_end, _begin, UseSize);
    }
    else
        UseSize = SerializeBufferSize(_frontPtr - _rearPtr);

    _end = _begin + _size;
    _frontPtr = _begin;
    _rearPtr = _begin + UseSize;
    printf("ReSize\n");
    return TRUE;
}

void CMessage::Peek(char *out, SerializeBufferSize size)
{
    char *f = _frontPtr;
    if (f + size > _rearPtr)
        throw MessageException(MessageException::HasNotData, "buffer has not Data\n");
    memcpy(out, f, size);
}


void CMessage::HexLog(en_Tag tag , const wchar_t * filename)
{
    int current = 0;

    wchar_t *hexBuffer = (wchar_t*)malloc(MaxSize * 3 + 4); // 최대 바이트와 띄어쓰기, 널문자 까지 포함.
    wchar_t *printBuffer = (wchar_t *)malloc(MaxSize * 4);
    if (hexBuffer == nullptr)
        __debugbreak();
    if (printBuffer == nullptr)
        __debugbreak();

    wchar_t wordSet[] = L"0123456789ABCDEF";
    BYTE data;

    for (; &_begin[current] != _end; current++)
    {
        data = _begin[current];

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
        StringCchPrintfW(printBuffer, MaxSize * 4 / sizeof(wchar_t), format[(BYTE)tag], Stringformat[(BYTE)tag]);
    }
    else
        StringCchPrintfW(printBuffer, MaxSize * 4 / sizeof(wchar_t), format[(BYTE)tag], Stringformat[(BYTE)tag], hexBuffer);
    fwrite(printBuffer, 2, wcslen(printBuffer), file);
    current = 0;
    for (; &_begin[current] != _end; current++)
    {
        hexBuffer[3 * current + 0] = L' ';
        hexBuffer[3 * current + 1] = L' ';
        hexBuffer[3 * current + 2] = L' ';
    }
    hexBuffer[3 * current + 0] = L'\n';
    hexBuffer[3 * current + 1] = L'\0';

    hexBuffer[3 * (_frontPtr - _begin)] = L'F';
    hexBuffer[3 * (_rearPtr - _begin)] = L'R';
    if ((BYTE)tag != 0)
    {
        fwrite(hexBuffer, 2, wcslen(hexBuffer), file);
    }
    fclose(file);

    free(hexBuffer);
    free(printBuffer);
}

