#include "ChatDummyClient.h"
#include <iostream>

int main()
{
    int cnt;
    std::cin >> cnt;
    ChatDummyClient client(cnt);
    client.Start("127.0.0.1", 21350); // LoginServer 포트
    return 0;
}
