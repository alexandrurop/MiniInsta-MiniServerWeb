#ifndef SERVER_ENGINE_H
#define SERVER_ENGINE_H

#include <winsock2.h>

#include "Post_Service.h"

typedef struct {
    SOCKET client_socket;
    struct Route* route_tree;
} ClientContext;

void start_server_engine(SOCKET server_socket);

void send_login_page_with_error(SOCKET s, const char* msg);

static void* memmem_local(const void* haystack, size_t haystack_len,
    const void* needle, size_t needle_len);

static int extract_boundary(const char* http_header, char* out_boundary, size_t out_sz);

static void ensure_user_upload_dir(const char* user);

char* replace_tag(const char* source, const char* tag, const char* replacement);

void start_server_engine(SOCKET server_socket);

DWORD WINAPI handle_client(LPVOID lpParam);

DWORD WINAPI worker_thread(LPVOID lpParam);

#endif