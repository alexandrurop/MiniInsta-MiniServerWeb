#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <windows.h>
#include <sqlext.h>

typedef struct {
    int authenticated;
    char user_handle[50];
} AuthResult;


int verify_user_in_json(const char* user_handle, const char* password);

int user_exists_in_users_json(const char* user_handle);

void register_user_local(const char* user_handle, const char* password);



#endif