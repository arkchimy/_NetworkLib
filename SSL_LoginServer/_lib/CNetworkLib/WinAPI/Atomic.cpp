#include "Atomic.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

std::uint16_t Increment16(std::uint16_t *p) noexcept
{
    return _InterlockedIncrement16((SHORT*)p);
}

std::uint32_t Increment32(std::uint32_t *p) noexcept
{
    return InterlockedIncrement((LONG*)p);
}

std::uint64_t Increment64(std::uint64_t *p) noexcept
{
    return _InterlockedIncrement64((LONGLONG*)p);
}

std::uint16_t Decrement16(std::uint16_t *p) noexcept
{
    return _InterlockedDecrement16((SHORT *)p);
}

std::uint32_t Decrement32(std::uint32_t *p) noexcept
{
    return InterlockedDecrement((LONG *)p);
}

std::uint64_t Decrement64(std::uint64_t *p) noexcept
{
    return _InterlockedDecrement64((LONGLONG *)p);
}
