#include "ChatDummyClient.h"

int main()
{
    ChatDummyClient client(100);
    client.Start("127.0.0.1", 32000);
    return 0;
}
