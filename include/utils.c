#include "utils.h"

#include <string.h>
#include <limits.h>

void send_response(int fd,
                          int status_code,
                          const char *reason,
                          const char *content_type,
                          const char *body,
                          size_t body_len)
{
    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Server: SimpleCServer/0.1\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %zu\r\n"
                     "%s%s\r\n",
                     status_code, reason,
                     body_len,
                     content_type ? "Content-Type: " : "",
                     content_type ? content_type : "");
    send(fd, header, n, 0);
    if (body && body_len > 0)
    {
        send(fd, body, body_len, 0);
    }
}

int build_full_path(const char *root_dir,
                    const char *url_path,
                    char *out, size_t out_size)
{
    // Zakazujemy ".." w ścieżce (bardzo prymitywne "zabezpieczenie").
    if (strstr(url_path, "..") != NULL)
    {
        return -1;
    }

    char rel[PATH_MAX];

    if (strcmp(url_path, "/") == 0)
    {
        // Domyślnie serwujemy index.html
        snprintf(rel, sizeof(rel), "/index.html");
    }
    else
    {
        snprintf(rel, sizeof(rel), "%s", url_path);
    }

    // Sklej root_dir + rel
    int n = snprintf(out, out_size, "%s%s", root_dir, rel);
    if (n < 0 || (size_t)n >= out_size)
    {
        return -1;
    }
    return 0;
}

const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    ext++;
    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm"))
        return "text/html";
    if (!strcasecmp(ext, "txt") || !strcasecmp(ext, "log"))
        return "text/plain";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg"))
        return "image/jpeg";
    if (!strcasecmp(ext, "png"))
        return "image/png";
    if (!strcasecmp(ext, "gif"))
        return "image/gif";
    if (!strcasecmp(ext, "css"))
        return "text/css";
    if (!strcasecmp(ext, "js"))
        return "application/javascript";
    return "application/octet-stream";
}

long parse_content_length(const char *headers, const char *headers_end)
{
    const char *p = headers;
    while (p < headers_end)
    {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end || line_end > headers_end)
        {
            break;
        }
        if (!strncasecmp(p, "Content-Length:", 15))
        {
            // p + 15 -> dalej będzie liczba
            const char *num_start = p + 15;
            while (num_start < line_end && (*num_start == ' ' || *num_start == '\t'))
                num_start++;
            char tmp[64];
            size_t len = (size_t)(line_end - num_start);
            if (len >= sizeof(tmp))
                len = sizeof(tmp) - 1;
            memcpy(tmp, num_start, len);
            tmp[len] = '\0';
            char *endptr = NULL;
            long val = strtol(tmp, &endptr, 10);
            if (endptr == tmp)
            {
                return -1;
            }
            return val;
        }
        p = line_end + 2;
    }
    return -1; // nie znaleziono
}
