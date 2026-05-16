// IOCP_NetworkProject.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//
#include <iostream>
#include <vector>

#include "Common.h"

struct A
{
    A()
    {
        memset(ch, 0xFF, sizeof(ch));
    }
    ~A()
    {
        memset(ch, 0, sizeof(ch));
    }
    char ch[5000];
};
int main()
{
    std::vector<A *> vec;
    srand(3);
    vec.reserve(100000);
    while (1)
    {
        int loopCnt = rand() % 100000;

        A *a = MY_NEW A[loopCnt]();
        MY_DELETE[] a;
    }
}
