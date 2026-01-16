#include "Response.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* render_static_file(const char* filename) {
    FILE* f = fopen(filename, "rb"); 
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = (char*)malloc(size + 1);
    if (buffer) {
        fread(buffer, 1, size, f);
        buffer[size] = '\0'; 
    }
    fclose(f);
    return buffer;
}

