#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

typedef struct HTTP_Server {
	SOCKET socket;
	int port;
} HTTP_Server;

void init_server(HTTP_Server* http_server, int port);

#endif
