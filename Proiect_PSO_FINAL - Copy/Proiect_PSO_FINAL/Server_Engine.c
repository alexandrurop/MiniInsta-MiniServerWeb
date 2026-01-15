#define _CRT_SECURE_NO_WARNINGS
#include "Server_Engine.h"
#include "Response.h"
#include "Post_Service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")


#define POOL_SIZE 10  // nr max de thr
#define MAX_QUEUE 100 // nr max de clienti care asteapta

SOCKET client_queue[MAX_QUEUE];
int queue_front = 0;
int queue_back = 0;
int queue_count = 0;

// Sincronizare
CRITICAL_SECTION queue_cs; 
HANDLE semaphore;

void send_login_page_with_error(SOCKET s, const char* msg) {

    const char* err_start =
        "<!DOCTYPE html><html lang='ro'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Eroare autentificare</title>"
        "<link rel='stylesheet' href='/static/index.css'>"
        "</head><body>"
        "<div style='max-width:420px;margin:24px auto;'>"
        "<p class='auth-error' style='text-align:center;'>";

    send(s, err_start, (int)strlen(err_start), 0);
    send(s, msg, (int)strlen(msg), 0);

    const char* err_mid =
        "</p>"
        "</div>";

    send(s, err_mid, (int)strlen(err_mid), 0);

    FILE* f = fopen("static/login.html", "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            send(s, buf, (int)n, 0);
        }
        fclose(f);
    }

    const char* end = "</body></html>";
    send(s, end, (int)strlen(end), 0);
}

static void* memmem_local(const void* haystack, size_t haystack_len,
    const void* needle, size_t needle_len)
{
    if (!haystack || !needle || needle_len == 0 || haystack_len < needle_len) return NULL;

    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;

    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0) {
            return (void*)(h + i);
        }
    }
    return NULL;
}

static int extract_boundary(const char* http_header, char* out_boundary, size_t out_sz)
{
    if (!http_header || !out_boundary || out_sz == 0) return 0;
    out_boundary[0] = '\0';

    const char* ct = strstr(http_header, "Content-Type:");
    if (!ct) return 0;

    const char* b = strstr(ct, "boundary=");
    if (!b) return 0;

    b += 9;
    size_t i = 0;
    while (b[i] && b[i] != '\r' && b[i] != '\n' && b[i] != ';' && i + 1 < out_sz) {
        out_boundary[i] = b[i];
        i++;
    }
    out_boundary[i] = '\0';

    return (out_boundary[0] != '\0');
}

static void ensure_user_upload_dir(const char* user)
{
    _mkdir("static");
    _mkdir("static/uploads");

    if (user && user[0]) {
        char user_dir[256];
        sprintf(user_dir, "static/uploads/%s", user);
        _mkdir(user_dir);
    }
}

char* replace_tag(const char* source, const char* tag, const char* replacement) {
    if (!source || !tag || !replacement) return NULL; 

    int len_tag = (int)strlen(tag);
    int len_repl = (int)strlen(replacement);
    int count = 0;
    const char* tmp_src = source;

    while ((tmp_src = strstr(tmp_src, tag))) {
        count++;
        tmp_src += len_tag;
    }

    if (count == 0) return _strdup(source);

    size_t new_len = strlen(source) + (size_t)count * (len_repl - len_tag) + 1;
    char* result = (char*)malloc(new_len);
    if (!result) return NULL;

    char* dst = result;
    while ((tmp_src = strstr(source, tag))) {
        int len_front = (int)(tmp_src - source);
        memcpy(dst, source, len_front);
        dst += len_front;
        memcpy(dst, replacement, len_repl);
        dst += len_repl;
        source = tmp_src + len_tag;
    }
    strcpy(dst, source);
    return result;
}

void start_server_engine(SOCKET server_socket) {
    //initializam mutex ul si semaforul
    InitializeCriticalSection(&queue_cs);
    semaphore = CreateSemaphore(NULL, 0, MAX_QUEUE, NULL);

    for (int i = 0; i < POOL_SIZE; i++) {
        CreateThread(NULL, 0, worker_thread,NULL, 0, NULL);
    }

    printf("Thread Pool initializat cu %d workeri.\n", POOL_SIZE);

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == INVALID_SOCKET) {
            printf("accept() failed: %d\n", WSAGetLastError());
            continue;
        }

        char ipstr[INET_ADDRSTRLEN] = { 0 };
        InetNtopA(AF_INET, &client_addr.sin_addr, ipstr, INET_ADDRSTRLEN);

        printf("Client connected: %s:%d\n", ipstr, ntohs(client_addr.sin_port));

        EnterCriticalSection(&queue_cs);

        if (queue_count < MAX_QUEUE) {
            client_queue[queue_back] = client_socket;
            queue_back = (queue_back + 1) % MAX_QUEUE;
            queue_count++;

            ReleaseSemaphore(semaphore, 1, NULL);
        }
        else {
            printf("Coada plina, refuzam conexiunea.\n");
            closesocket(client_socket);
        }

        LeaveCriticalSection(&queue_cs);
    }
}

//DWORD = double word
//WINAPI=macro 
//LPVOID = Long Pointer to Void

DWORD WINAPI handle_client(LPVOID lpParam) {
    ClientContext* ctx = (ClientContext*)lpParam;
    char client_msg[4096] = "";



    int recv_len = 0;
    client_msg[0] = '\0';

    while (recv_len < (int)sizeof(client_msg) - 1) {
        int r = recv(ctx->client_socket,
            client_msg + recv_len,
            (int)sizeof(client_msg) - 1 - recv_len,
            0);
        if (r <= 0) break;
        recv_len += r;
        client_msg[recv_len] = '\0';

        if (strstr(client_msg, "\r\n\r\n")) break;
    }

    client_msg[recv_len] = '\0';
    printf("\t%s", client_msg);
    
    char logged_in_user[50] = "Guest";
    char* cookie_ptr = strstr(client_msg, "Cookie: ");
    if (cookie_ptr) {
        char* user_cookie = strstr(cookie_ptr, "user=");
        if (user_cookie) {
            sscanf(user_cookie, "user=%49[^; \r\n]", logged_in_user);
        }
    }

    char* body = strstr(client_msg, "\r\n\r\n");
    if (body) body += 4;

    char* method = NULL;
    char* urlRoute = NULL;

    char msg_copy[4096];
    strcpy(msg_copy, client_msg);

    char* client_http_header = strtok(msg_copy, "\n");
    if (client_http_header == NULL) {
        closesocket(ctx->client_socket);
        free(ctx);
        return 0;
    }

    char* header_token = strtok(client_http_header, " ");
    int header_parse_counter = 0;

    while (header_token != NULL) {
        if (header_parse_counter == 0) method = header_token;
        else if (header_parse_counter == 1) urlRoute = header_token;
        header_token = strtok(NULL, " ");
        header_parse_counter++;
    }

    if (method != NULL && urlRoute != NULL) {
        printf("Thread %d: %s %s\n", GetCurrentThreadId(), method, urlRoute);

        //printf("RAW method=[%s] url=[%s]\n", method, urlRoute);

        if (method) {
            char* cr = strchr(method, '\r');
            if (cr) *cr = '\0';
        }
        if (urlRoute) {
            char* cr = strchr(urlRoute, '\r');
            if (cr) *cr = '\0';
        }

        if (strcmp(method, "POST") == 0 && strcmp(urlRoute, "/verify") == 0) {

            int login_ok = 0;
            char user[50] = { 0 }, pass[50] = { 0 };

            if (body) {
                if (sscanf(body, "username=%49[^&]&password=%49s", user, pass) == 2) {
                    login_ok = verify_user_in_json(user, pass);
                }
            }

            if (login_ok) {
                char success_header[512];
                snprintf(success_header, sizeof(success_header),
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /insta\r\n"
                    "Set-Cookie: user=%s; Path=/; HttpOnly\r\n"
                    "Connection: close\r\n\r\n",
                    user
                );

                send(ctx->client_socket, success_header, (int)strlen(success_header), 0);

                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }
            else {
                const char* hdr =
                    "HTTP/1.1 401 Unauthorized\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Connection: close\r\n\r\n";
                send(ctx->client_socket, hdr, (int)strlen(hdr), 0);

                send_login_page_with_error(ctx->client_socket, "User sau parola incorecte!");

                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }
        }
        else if (strcmp(method, "POST") == 0 && strcmp(urlRoute, "/signup") == 0) {
            char user[50], pass[50];
            if (body && sscanf(body, "username=%[^&]&password=%s", user, pass) == 2) {

                int rc = register_user_local(user, pass);
                if (rc == -1) {
                    // username deja folosit
                    const char* hdr =
                        "HTTP/1.1 409 Conflict\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Connection: close\r\n\r\n";
                    send(ctx->client_socket, hdr, (int)strlen(hdr), 0);

                    send_login_page_with_error(ctx->client_socket,
                        "Acest username este deja folosit!");
                }
                else {//succes
                    const char* success =
                        "HTTP/1.1 302 Found\r\n"
                        "Location: /\r\n"
                        "Connection: close\r\n\r\n";
                    send(ctx->client_socket, success, (int)strlen(success), 0);
                }
            } else {//invalid
                const char* bad =
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Connection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
            }
            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else if (strcmp(method, "POST") == 0 && strcmp(urlRoute, "/delete") == 0) {
            char raw_path[256] = "";
            if (body) {
                printf("%s", body);
                char* val = strstr(body, "image_path=");
                if (val) {
                    val += 11; // cautam dupa "image_path=" si sarim
                    sscanf(val, "%255[^& \r\n]", raw_path);//copiem pana la &
                }
            }

            char decoded_path[256];
            int j = 0;
            for (int i = 0; raw_path[i] != '\0'; i++) {
                // se trimite "codificat" pentru ca aceste simboluri pot insemna altceva, si se pune valoarea loc in ASCII
                if (raw_path[i] == '%' && raw_path[i + 1] == '2' && (raw_path[i + 2] == 'F' || raw_path[i + 2] == 'f')) {
                    decoded_path[j++] = '/';
                    i += 2;
                }
                else {
                    decoded_path[j++] = raw_path[i];
                }
            }
            decoded_path[j] = '\0';

            if (strlen(decoded_path) > 0) {
                delete_post_local(logged_in_user, decoded_path);
            }

            char* redir = "HTTP/1.1 303 See Other\r\nLocation: /profile\r\nConnection: close\r\n\r\n";
            send(ctx->client_socket, redir, (int)strlen(redir), 0);
            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else if (strcmp(urlRoute, "/") == 0) {
            char* response_data = render_static_file("static/login.html");//citeste tot
            const char* http_header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"; 

            send(ctx->client_socket, http_header, (int)strlen(http_header), 0);
            if (response_data) {
                send(ctx->client_socket, response_data, (int)strlen(response_data), 0);
                free(response_data);
            }

            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else if (strcmp(urlRoute, "/insta") == 0) {
            const char* header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: close\r\n\r\n";
            send(ctx->client_socket, header, (int)strlen(header), 0);

            char* template_h = render_static_file("static/insta.html");
            if (template_h) {
                char* placeholder = strstr(template_h, "{{FEED_CONTENT}}");

                if (placeholder) {
                    int front_len = (int)(placeholder - template_h);
                    send(ctx->client_socket, template_h, front_len, 0);//se trimite excat pana la FEED_CONTENT

                    send_insta_feed_to_socket(ctx->client_socket);

                    char* back_part = placeholder + strlen("{{FEED_CONTENT}}");
                    send(ctx->client_socket, back_part, (int)strlen(back_part), 0);
                }
                else {
                    send(ctx->client_socket, template_h, (int)strlen(template_h), 0);
                }
                free(template_h); 
            }

            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else if (strcmp(urlRoute, "/profile") == 0) {
            char* template_p = render_static_file("static/profile.html");

            if (template_p) {
                const char* header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, header, (int)strlen(header), 0);

                char* placeholder = strstr(template_p, "{{MY_FEED}}");
                if (placeholder) {
                    int front_len = (int)(placeholder - template_p);
                    send(ctx->client_socket, template_p, front_len, 0);

                    send_my_profile_to_socket(ctx->client_socket, logged_in_user);

                    char* back_part = placeholder + strlen("{{MY_FEED}}");
                    send(ctx->client_socket, back_part, (int)strlen(back_part), 0);
                }
                free(template_p); 
            }

            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else if (strcmp(method, "POST") != 0 && strcmp(urlRoute, "/upload") == 0) {
           

            if (!body) {
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            char* content_len_ptr = strstr(client_msg, "Content-Length:");
            long total_len = 0;
            if (!content_len_ptr || sscanf(content_len_ptr, "Content-Length: %ld", &total_len) != 1 || total_len <= 0) {
                const char* bad = "HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            char boundary_val[200];
            if (!extract_boundary(client_msg, boundary_val, sizeof(boundary_val))) {
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            unsigned char* full_body = (unsigned char*)malloc((size_t)total_len);
            if (!full_body) {
                const char* err = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, err, (int)strlen(err), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            int header_len = (int)(body - client_msg);
            int already = recv_len - header_len;
            if (already < 0) already = 0;
            if (already > total_len) already = (int)total_len;

            memcpy(full_body, body, (size_t)already);

            int got = already;
            while (got < total_len) {
                int r = recv(ctx->client_socket, (char*)full_body + got, (int)total_len - got, 0);
                if (r <= 0) break;
                got += r;
            }

            if (got < total_len) {
                free(full_body);
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            char final_description[256] = "Fara descriere";

            const char* desc_key = "name=\"description\"";
            unsigned char* desc_part = (unsigned char*)memmem_local(full_body, (size_t)total_len,
                desc_key, strlen(desc_key));
            if (desc_part) {
                const char* sep = "\r\n\r\n";
                unsigned char* ds = (unsigned char*)memmem_local(desc_part,
                    (size_t)(full_body + total_len - desc_part),
                    sep, 4);
                if (ds) {
                    ds += 4;
                    unsigned char* de = (unsigned char*)memmem_local(ds,
                        (size_t)(full_body + total_len - ds),
                        "\r\n", 2);
                    if (de && de > ds) {
                        int len = (int)(de - ds);
                        if (len > 255) len = 255;
                        memcpy(final_description, ds, (size_t)len);
                        final_description[len] = '\0';
                    }
                }
            }

            const char* file_key = "name=\"fileToUpload\"";
            unsigned char* file_part = (unsigned char*)memmem_local(full_body, (size_t)total_len,
                file_key, strlen(file_key));
            if (!file_part) {
                free(full_body);
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            unsigned char* file_data_start = (unsigned char*)memmem_local(file_part,
                (size_t)(full_body + total_len - file_part),
                "\r\n\r\n", 4);
            if (!file_data_start) {
                free(full_body);
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }
            file_data_start += 4;

            char end_marker[256];
            sprintf(end_marker, "\r\n--%s", boundary_val);
            size_t end_marker_len = strlen(end_marker);

            size_t file_start_off = (size_t)(file_data_start - full_body);
            size_t search_len = (size_t)total_len - file_start_off;

            unsigned char* file_data_end = (unsigned char*)memmem_local(file_data_start,
                search_len,
                end_marker,
                end_marker_len);
            if (!file_data_end) {
                free(full_body);
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            size_t file_size = (size_t)(file_data_end - file_data_start);
            if (file_size == 0) {
                free(full_body);
                const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, bad, (int)strlen(bad), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            ensure_user_upload_dir(logged_in_user);

            char dest_path[256];
            sprintf(dest_path, "static/uploads/%s/post_%lld_%lu.jpg",
                logged_in_user, (long long)time(NULL), GetCurrentThreadId());

            FILE* out = fopen(dest_path, "wb");
            if (!out) {
                free(full_body);
                const char* err = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, err, (int)strlen(err), 0);
                closesocket(ctx->client_socket);
                free(ctx);
                return 0;
            }

            fwrite(file_data_start, 1, file_size, out);
            fclose(out);

            update_user_json(logged_in_user, dest_path, final_description);

            free(full_body);

            const char* redirect =
                "HTTP/1.1 303 See Other\r\n"
                "Location: /profile\r\n"
                "Connection: close\r\n\r\n";
            send(ctx->client_socket, redirect, (int)strlen(redirect), 0);

            closesocket(ctx->client_socket);
            free(ctx);
            return 0;
        }
        else {
            char* file_data = render_static_file(urlRoute + 1);

            if (file_data) {
                const char* type = "text/plain";

                if (strstr(urlRoute, ".css")) type = "text/css";
                else if (strstr(urlRoute, ".jpg") || strstr(urlRoute, ".jpeg")) type = "image/jpeg";
                else if (strstr(urlRoute, ".png")) type = "image/png";
                else if (strstr(urlRoute, ".html")) type = "text/html; charset=UTF-8";

                FILE* f_size = fopen(urlRoute + 1, "rb");
                long actual_size = 0;
                if (f_size) {
                    fseek(f_size, 0, SEEK_END);
                    actual_size = ftell(f_size);
                    fclose(f_size);
                }

                char header[512];
                sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", type, actual_size);

                send(ctx->client_socket, header, (int)strlen(header), 0);
                send(ctx->client_socket, file_data, (int)actual_size, 0); 

                free(file_data);
            }
            else {
                const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(ctx->client_socket, nf, (int)strlen(nf), 0);
            }
        }
    }

    closesocket(ctx->client_socket);
    free(ctx);
    return 0;
}

DWORD WINAPI worker_thread(LPVOID lpParam) {
    struct Route* route_tree = (struct Route*)lpParam;

    while (1) {
        //ast semaforul, -1 la semafor
        WaitForSingleObject(semaphore, INFINITE);

        SOCKET client_sock = INVALID_SOCKET;

        //elimin din coada
        EnterCriticalSection(&queue_cs);
        if (queue_count > 0) {
            client_sock = client_queue[queue_front];
            queue_front = (queue_front + 1) % MAX_QUEUE;
            queue_count--;
        }
        LeaveCriticalSection(&queue_cs);

        if (client_sock != INVALID_SOCKET) {
            ClientContext* ctx = (ClientContext*)malloc(sizeof(ClientContext));
            if (ctx) {
                ctx->client_socket = client_sock;
                ctx->route_tree = route_tree;
                handle_client(ctx);
            }
        }
    }
    return 0;
}
