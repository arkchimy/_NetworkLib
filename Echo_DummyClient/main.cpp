#include "DummyClient.h"
#include <iostream>

int main()
{
    int cnt;
    std::cin >> cnt;
    DummyClient *dummy = new DummyClient(cnt);
    dummy->Start("127.0.0.1", 32000);

    while (1)
    {

    }
}