#include "HTTP_Server.h"
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

void init_server(HTTP_Server* http_server, int port) {
	http_server->port = port;

	WSADATA wsaData;
	int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsaResult != 0) {
		fprintf(stderr, "WSAStartup failed: %d\n", wsaResult);
		http_server->socket = INVALID_SOCKET;
		return;
	}

	SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == INVALID_SOCKET) {
		fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
		WSACleanup();
		http_server->socket = INVALID_SOCKET;
		return;
	}

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
		fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
		closesocket(server_socket);
		WSACleanup();
		http_server->socket = INVALID_SOCKET;
		return;
	}

	if (listen(server_socket, 5) == SOCKET_ERROR) {
		fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
		closesocket(server_socket);
		WSACleanup();
		http_server->socket = INVALID_SOCKET;
		return;
	}

	http_server->socket = server_socket;
	printf("HTTP Server Initialized\nPort: %d\n", http_server->port);
}
