#pragma once
#include <iostream>
#include <strsafe.h>

#define ERROR_BUFFER_SIZE 100
// str은 const wchar_t* 문자열 상수 일 것!
#if 0
void ERROR_FILE_LOG(const wchar_t *LogFilename, const wchar_t *str);
#else
#define ERROR_FILE_LOG(LogFilename, str)                                          \
    do                                                                            \
    {                                                                             \
        FILE *file = nullptr;                                                     \
        DWORD lastError = GetLastError();                                         \
        const WCHAR *format = L"str : %s \t GetLastError : %d\n";                 \
        WCHAR errorBuffer[ERROR_BUFFER_SIZE];                                     \
        StringCchPrintfW(errorBuffer, ERROR_BUFFER_SIZE, format, str, lastError); \
        _wfopen_s(&file, LogFilename, L"a+,ccs=UTF-16LE");                        \
        if (file != nullptr)                                                      \
        {                                                                         \
            __assume(file != nullptr);                                            \
            fwrite(errorBuffer, 2, wcslen(errorBuffer), file);                    \
            fclose(file);                                                         \
        }                                                                         \
    } while (0)
#endif

void HEX_FILE_LOG(const wchar_t *LogFilename, void *ptr, size_t size)
{
    static const wchar_t *m = L"0123456789ABCDEF";

    FILE *file = nullptr;
    DWORD lastError = GetLastError();
    wchar_t buffer[2000 * 4];

    DWORD current = 0;
    char *begin = (char *)ptr;
    char *end = begin + size;

    BYTE idx;

    for (; &begin[current] != end; ++current)
    {
        idx = begin[current];
        buffer[current * 3 + 0] = m[idx >> 4];
        buffer[current * 3 + 1] = m[idx & 0xF];
        buffer[current * 3 + 2] = L' ';
    }
    buffer[current * 3] = L'\n';
    buffer[current * 3 + 1] = L'\n';
    _wfopen_s(&file, LogFilename, L"a+,ccs=UTF-16LE");
    if (file != nullptr)
    {
        __assume(file != nullptr);
        fwrite(buffer, sizeof(WCHAR), size * 3 + 2, file);
        fclose(file);
    }
}
