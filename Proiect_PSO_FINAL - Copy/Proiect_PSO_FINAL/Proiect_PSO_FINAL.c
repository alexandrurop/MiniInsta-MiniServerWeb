#define _CRT_SECURE_NO_WARNINGS 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

#pragma comment(lib, "ws2_32.lib")

#include "HTTP_Server.h"
#include "Server_Engine.h"


int main() {
    HTTP_Server http_server;
    init_server(&http_server, 1234);

    printf("Serverul ruleaza in mod Multi-threaded...\n");

    start_server_engine(http_server.socket);

    return 0;
}
