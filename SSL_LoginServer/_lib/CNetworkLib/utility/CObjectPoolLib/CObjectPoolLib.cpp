// CObjectPoolLib.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//

#include "CObjectPoolLib.h"

// TODO: 라이브러리 함수의 예제입니다.
void fnCObjectPoolLib()
{
    //class 
    class A
    {

    };
    CObjectPool<A> objPool;
    A* a = static_cast<A *>(objPool.Alloc());

    // 만일 접근을 하면 해당 매크로를 사용
    POOL_TOUCH(objPool, a);
    objPool.Release(a);
    
}
