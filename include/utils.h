#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

int build_full_path(const char *root_dir,
                    const char *url_path,
                    char *out, size_t out_size);

void send_simple_response(int fd,
                          int status_code,
                          const char *reason,
                          const char *content_type,
                          const char *body,
                          size_t body_len);

const char *get_mime_type(const char *path);
long parse_content_length(const char *headers, const char *headers_end);

#endif