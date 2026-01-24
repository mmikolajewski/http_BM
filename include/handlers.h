#ifndef HANDLERS_H
#define HANDLERS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void handle_get_head(int client_fd,
                            const char *root_dir,
                            const char *url_path,
                            int is_head);

void handle_put(int client_fd,
                       const char *root_dir,
                       const char *url_path,
                       const char *body_start,
                       size_t body_in_buf,
                       long content_length);

void handle_delete(int client_fd,
                          const char *root_dir,
                          const char *url_path);

#endif