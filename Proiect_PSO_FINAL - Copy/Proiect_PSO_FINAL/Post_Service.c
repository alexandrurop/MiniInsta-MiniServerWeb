#define _CRT_SECURE_NO_WARNINGS
#include "Post_Service.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <io.h> 
#include <winsock2.h>

int comparePosts(const void* a, const void* b) {
    return strcmp(((Post*)b)->upload_date, ((Post*)a)->upload_date);
}

void update_user_json(const char* username, const char* image_path, const char* description) {
    char json_path[256];
    sprintf(json_path, "static/%s.json", username);

    FILE* f1 = fopen(json_path, "rb");
    if (!f1) return;

    fseek(f1, 0, SEEK_END);
    long size = ftell(f1);
    fseek(f1, 0, SEEK_SET);

    char* buffer = (char*)malloc(size + 1);
    if (buffer) {
        fread(buffer, 1, size, f1);
        buffer[size] = '\0'; 
    }
    fclose(f1);

    int len = (int)strlen(buffer);
    int last_bracket_index = -1;

    for (int i = len - 1; i >= 0; i--) {
        if (buffer[i] == ']') {
            last_bracket_index = i;
            break;
        }
    }

    if (last_bracket_index != -1) {
        while (last_bracket_index > 0 &&
            (buffer[last_bracket_index - 1] == ' ' ||
                buffer[last_bracket_index - 1] == '\n' ||
                buffer[last_bracket_index - 1] == '\r' ||
                buffer[last_bracket_index - 1] == '\t')) {
            last_bracket_index--;
        }

        FILE* f2 = fopen(json_path, "wb");
        if (f2) {
            fwrite(buffer, 1, last_bracket_index, f2);

            time_t t = time(NULL);
            struct tm tm = *localtime(&t);
            char date_str[64];
            sprintf(date_str, "%04d-%02d-%02d %02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);

            fprintf(f2, ",\n    {\n");
            fprintf(f2, "      \"image_path\": \"%s\",\n", image_path);
            fprintf(f2, "      \"description\": \"%s\",\n", description);
            fprintf(f2, "      \"upload_date\": \"%s\"\n", date_str);
            fprintf(f2, "    }\n  ]\n}");

            fclose(f2);
            printf("JSON actualizat cu succes pentru %s\n", username);
        }
    }
    else {
        printf("Eroare: Nu am gasit caracterul ']' in JSON.\n");
    }
    free(buffer);
}

void send_insta_feed_to_socket(SOCKET client_socket) {
    Post all_posts[500];
    memset(all_posts, 0, sizeof(all_posts));
    int count = 0;

    struct _finddata_t file_data; //structura pentru a stoca info despre un fisier gasit intr un dir
    intptr_t handle;

    if ((handle = _findfirst("static/*.json", &file_data)) != -1) {
        do {
            if (strcmp(file_data.name, "users.json") == 0) continue;

            char full_path[256];
            sprintf(full_path, "static/%s", file_data.name);

            FILE* f = fopen(full_path, "r");
            if (f) {
                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, "\"image_path\":")) {
                        sscanf(line, " \"image_path\": \"%255[^\"]\"", all_posts[count].image_path);
                    }
                    else if (strstr(line, "\"description\":")) {
                        sscanf(line, " \"description\": \"%255[^\"]\"", all_posts[count].description);
                    }
                    else if (strstr(line, "\"upload_date\":")) {
                        sscanf(line, " \"upload_date\": \"%63[^\"]\"", all_posts[count].upload_date);
                        count++; 
                    }
                }
                fclose(f);
            }
        } while (_findnext(handle, &file_data) == 0);
        _findclose(handle);
    }

   

    if (count > 0) {
        qsort(all_posts, count, sizeof(Post), comparePosts);
    }

   
    const char* grid_start = "<div class='insta-grid'>";
    send(client_socket, grid_start, (int)strlen(grid_start), 0);

    for (int i = 0; i < count; i++) {
        char buffer[2048];

        char author[64] = "utilizator";
        char path_copy[256];
        strcpy(path_copy, all_posts[i].image_path);
        char* token = strtok(path_copy, "/");
        token = strtok(NULL, "/");            
        token = strtok(NULL, "/");            
        if (token) strcpy(author, token);

        snprintf(buffer, sizeof(buffer),
            "<article class='post-container'>"
            "  <div class='post-header'>@%s</div>"   
            "  <div class='post-image-wrapper'>"
            "    <img src='/%s' class='post-main-img' alt='Imagine'>"
            "  </div>"
            "  <div class='post-content-area'>"
            "    <p class='post-caption'>%s</p>"     
            "    <div class='post-timestamp'>%s</div>"
            "  </div>"
            "</article>",
            author,
            all_posts[i].image_path,
            all_posts[i].description,   
            all_posts[i].upload_date
        );
        send(client_socket, buffer, (int)strlen(buffer), 0);
    }


    const char* grid_end = "</div>";
    send(client_socket, grid_end, (int)strlen(grid_end), 0);
}

void send_my_profile_to_socket(SOCKET client_socket, const char* logged_user) {
    Post all_posts[500];
    int count = 0;
    char user_json_path[256];
    sprintf(user_json_path, "static/%s.json", logged_user);

    FILE* f = fopen(user_json_path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "\"image_path\":")) sscanf(line, " \"image_path\": \"%255[^\"]\"", all_posts[count].image_path);
            if (strstr(line, "\"description\":")) sscanf(line, " \"description\": \"%255[^\"]\"", all_posts[count].description);
            if (strstr(line, "\"upload_date\":")) {
                sscanf(line, " \"upload_date\": \"%63[^\"]\"", all_posts[count].upload_date);
                count++;
            }
        }
        fclose(f);
    }

    if (count > 0) qsort(all_posts, count, sizeof(Post), comparePosts);

    for (int i = 0; i < count; i++) {
        char buffer[2048];
        snprintf(buffer, sizeof(buffer),
            "<article class='post-card'>"
            "  <img class='post-img' src='/%s' alt='Postare'>"
            "  <div class='post-meta'>"
            "    <div>"
            "      <div class='post-date'>Publicat la: %s</div>"
            "      <p class='post-desc'>%s</p>"
            "    </div>"
            "    <form action='/delete' method='POST' onsubmit='return confirm(\"Stergi definitiv aceasta postare?\");'>"
            "      <input type='hidden' name='image_path' value='%s'>"
            "      <button type='submit' class='btn-delete'>Sterge postarea</button>"
            "    </form>"
            "  </div>"
            "</article>",
            all_posts[i].image_path, all_posts[i].upload_date, all_posts[i].description, all_posts[i].image_path);
        send(client_socket, buffer, (int)strlen(buffer), 0);
    }
}

char* generate_insta_feed_html() {
    Post all_posts[500];
    memset(all_posts, 0, sizeof(all_posts));
    int count = 0;

    struct _finddata_t file_data;
    intptr_t handle;

    if ((handle = _findfirst("static/*.json", &file_data)) != -1) {
        do {
            if (strcmp(file_data.name, "users.json") == 0) continue;

            char full_path[256];
            sprintf(full_path, "static/%s", file_data.name);

            FILE* f = fopen(full_path, "r");
            if (f) {
                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, "\"image_path\":")) {
                        sscanf(line, " \"image_path\": \"%255[^\"]\"", all_posts[count].image_path);
                    }
                    else if (strstr(line, "\"upload_date\":")) {
                        sscanf(line, " \"upload_date\": \"%63[^\"]\"", all_posts[count].upload_date);
                        count++;
                    }
                }
                fclose(f);
            }
        } while (_findnext(handle, &file_data) == 0);
        _findclose(handle);
    }

    if (count > 0) {
        qsort(all_posts, count, sizeof(Post), comparePosts);
    }

    char* html = (char*)malloc(1024);
    if (!html) return NULL;
    memset(html, 0, 1024);
    strcpy(html, "<div class='insta-grid'>");

    for (int i = 0; i < count; i++) {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));

        snprintf(buffer, sizeof(buffer),
            "<div class='grid-item' style='border: 1px solid #ccc; padding: 10px; margin: 10px; border-radius: 8px;'>"
            "  <img src='/%s' style='width: 100%%; max-width: 300px; border-radius: 5px;' />"
            "  <div class='post-info' style='margin-top: 10px;'>"
            "    <p><strong>Descriere:</strong> %s</p>"
            "    <p><small>Încărcat la: %s</small></p>"
            "    "
            "    "
            "    <form action='/delete' method='POST' onsubmit='return confirm(\"Sigur vrei să ștergi această poză?\");'>"
            "      <input type='hidden' name='image_path' value='%s'>"
            "      <button type='submit' style='background-color: #ff4d4d; color: white; border: none; padding: 5px 10px; cursor: pointer; border-radius: 3px;'>"
            "        Șterge"
            "      </button>"
            "    </form>"
            "  </div>"
            "</div>",
            all_posts[i].image_path,
            all_posts[i].description,
            all_posts[i].upload_date,
            all_posts[i].image_path 
        );

        if (strlen(html) + strlen(buffer) < 499900) {
            strcat(html, buffer);
        }
    }

    strcat(html, "</div>");
    return html;
}

void delete_post_local(const char* username, const char* image_path) {
    char json_path[256];
    sprintf(json_path, "static/%s.json", username);

    const char* search_path = image_path;
    if (image_path[0] == '/') search_path++;

    FILE* f = fopen(json_path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) { fclose(f); return; }
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);

    char* target = strstr(buffer, search_path);
    if (target) {
        char* start_obj = target;
        while (start_obj > buffer && *start_obj != '{') start_obj--;

        char* end_obj = strstr(target, "}");
        if (end_obj) {
            end_obj++; 

            if (start_obj > buffer && *(start_obj - 1) == ',') {
                start_obj--;
            }
            else if (*end_obj == ',') {
                end_obj++;
            }

            FILE* out = fopen(json_path, "wb");
            if (out) {
                fwrite(buffer, 1, (size_t)(start_obj - buffer), out);
                fwrite(end_obj, 1, (size_t)(buffer + size - end_obj), out);
                fclose(out);

                remove(search_path);
                printf("Succes: Postare eliminata din %s\n", json_path);
            }
        }
    }
    else {
        printf("Eroare: Nu am gasit calea %s in JSON\n", search_path);
    }
    free(buffer);
}