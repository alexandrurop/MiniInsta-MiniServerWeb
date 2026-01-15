#define _CRT_SECURE_NO_WARNINGS
#include "Auth_Service.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h> //_mkdir pe Windows

int verify_user_in_json(const char* user_handle, const char* password) {
    FILE* file = fopen("static/users.json", "r");
    if (!file) {
        printf("Eroare: Nu s-a putut deschide users.json\n");
        return 0;
    }

    char line[256];
    char search_user[100];
    char search_pass[100];
    int found = 0;

    sprintf(search_user, "\"user_handle\": \"%s\"", user_handle);
    sprintf(search_pass, "\"password\": \"%s\"", password);

    char current_user_found = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, search_user)) {
            current_user_found = 1;
        }

        if (current_user_found && strstr(line, search_pass)) {
            found = 1;
            break;
        }

        if (strstr(line, "}")) {
            current_user_found = 0;
        }
    }

    fclose(file);
    return found;
}

int user_exists_in_users_json(const char* user_handle) {
    FILE* f = fopen("static/users.json", "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = (char*)malloc(sz + 1);
    if (!data) { fclose(f); return 0; }

    fread(data, 1, sz, f);
    data[sz] = '\0';
    fclose(f);

    char pattern[160];
    snprintf(pattern, sizeof(pattern), "\"user_handle\": \"%s\"", user_handle);

    int exists = (strstr(data, pattern) != NULL);

    free(data);
    return exists;
}

void register_user_local(const char* user_handle, const char* password) {
    if (user_exists_in_users_json(user_handle)) {
        return -1; // deja exista
    }

    FILE* file = fopen("static/users.json", "rb+");
    if (!file) return;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);

    int offset = -1;
    char c;
    while (size + offset > 0) {
        fseek(file, offset, SEEK_END);
        c = fgetc(file);
        if (c == ']') break; 
        offset--;
    }

    fseek(file, offset, SEEK_END);

    fprintf(file, ",\n    {\n      \"user_handle\": \"%s\",\n      \"password\": \"%s\"\n    }\n  ]\n}", user_handle, password);
    fclose(file);

    char json_path[256];
    sprintf(json_path, "static/%s.json", user_handle);
    FILE* f_new = fopen(json_path, "w");
    if (f_new) {
        fprintf(f_new, "{\n  \"posts\": []\n}");
        fclose(f_new);
    }

    char folder_path[256];
    sprintf(folder_path, "static/uploads/%s", user_handle);
    _mkdir(folder_path);
}

