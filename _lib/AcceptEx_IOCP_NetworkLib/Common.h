#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef RT_ASSERT
#define RT_ASSERT(x) \
    if (!(x))        \
        __debugbreak();
#endif

void *operator new(size_t size,const char* file, int line);
void *operator new[](size_t size, const char *file, int line);

void operator delete(void *ptr, const char *file, int line);
void operator delete[](void *ptr, const char *file, int line);

#define MY_NEW new (__FILE__,__LINE__)
#define MY_DELETE delete