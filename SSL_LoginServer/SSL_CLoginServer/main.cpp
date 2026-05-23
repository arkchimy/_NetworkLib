#include "SSL_CLoginServer.h"

#include <conio.h>
#include "../_lib/CrushDump_lib/CrushDump_lib.h"

int main()
{
    CDump::SetHandlerDump();

    SSL_CLoginServer* server = new SSL_CLoginServer();
    server->Start();
    // TODO : 서버 종료 대기 로직 필요
    while (1)
    {
        if (_kbhit())
        {
            char ch = _getch();
            if (ch == 'p' || ch == 'P')
            {
                clsDeadLockManager::GetInstance()->CreateLogFile_TlsInfo();
            }
        }
    }

    return 0;
    
}