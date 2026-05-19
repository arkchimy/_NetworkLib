#pragma once

#define RT_ASSERT(x)        \
    do                      \
    {                       \
        if (!(x))           \
        {                   \
            __debugbreak(); \
        }                   \
    } while (0)

