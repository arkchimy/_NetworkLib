#pragma once
#include <cstdint>


std::uint16_t Increment16(std::uint16_t *p) noexcept;
std::uint32_t Increment32(std::uint32_t *p) noexcept;
std::uint64_t Increment64(std::uint64_t *p) noexcept;


std::uint16_t Decrement16(std::uint16_t *p) noexcept;
std::uint32_t Decrement32(std::uint32_t *p) noexcept;
std::uint64_t Decrement64(std::uint64_t *p) noexcept;

namespace Win32
{
	template <typename T>
	T AtomicIncrement(T &Target);
	template <typename T>
	T AtomicDecrement(T &Target);

template <typename T>
        T AtomicIncrement(T &Target) 
        {
            // 해당 크기의 경계에 섯는지 체크
            if (size_t(&Target) % alignof(T) != 0)
                __debugbreak();

            T retval;
            if constexpr (sizeof(T) == 2)
            {
                retval = Increment16((std::uint16_t *)&Target);
            }
            else if constexpr (sizeof(T) == 4)
            {
                retval = Increment32((std::uint32_t *)&Target);
            }
            else if constexpr (sizeof(T) == 8)
            {
                retval = Increment64((std::uint64_t *)&Target);
            }
            else
            {
                __debugbreak();
            }

            return retval;
        }
template <typename T>
        T AtomicDecrement(T &Target) 
        {
            // 해당 크기의 경계에 섯는지 체크
            if (size_t(&Target) % alignof(T) != 0)
            {
                __debugbreak();
            }

            T retval;
            if constexpr (sizeof(T) == 2)
            {
                retval = Decrement16((std::uint16_t *)&Target);
            }
            else if constexpr (sizeof(T) == 4)
            {
                retval = Decrement32((std::uint32_t *)&Target);
            }
            else if constexpr (sizeof(T) == 8)
            {
                retval = Decrement64((std::uint64_t *)&Target);
            }
            else
            {
                __debugbreak();
            }

            return retval;
        }


} // namespace Win32

