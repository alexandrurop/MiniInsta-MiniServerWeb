#ifndef POST_SERVICE_H
#define POST_SERVICE_H

#include <stdio.h>

typedef struct {
    char image_path[256];
    char upload_date[64]; 
    char description[256];} Post;

char* generate_insta_feed_html();


void update_user_json(const char* username, const char* image_path, const char* description);

//void collect_all_posts(Post* post_array, int* count);

void delete_post_local(const char* username, const char* image_path);

//void send_insta_feed_to_socket(SOCKET client_socket);

#endif