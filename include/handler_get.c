#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "handlers.h"
#include "constants.h"
#include "utils.h"

void handle_get_head(int client_fd,
                     const char *root_dir,
                     const char *url_path,
                     int is_head)
{
    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0)
    {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    struct stat st;
    if (stat(full_path, &st) < 0)
    {
        const char *msg = "Not Found\r\n";
        send_simple_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        return;
    }

    if (!S_ISREG(st.st_mode))
    {
        const char *msg = "Forbidden\r\n";
        send_simple_response(client_fd, 403, "Forbidden", "text/plain", msg, strlen(msg));
        return;
    }

    int file_fd = open(full_path, O_RDONLY);
    if (file_fd < 0)
    {
        const char *msg = "Internal Server Error\r\n";
        send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        return;
    }

    const char *mime = get_mime_type(full_path);

    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Server: SimpleCServer/0.1\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %ld\r\n"
                     "Content-Type: %s\r\n"
                     "\r\n",
                     (long)st.st_size,
                     mime);
    send(client_fd, header, n, 0);

    if (!is_head)
    {
        char buf[IO_BUF_SIZE];
        ssize_t r;
        while ((r = read(file_fd, buf, sizeof(buf))) > 0)
        {
            ssize_t off = 0;
            while (off < r)
            {
                ssize_t s = send(client_fd, buf + off, r - off, 0);
                if (s <= 0)
                {
                    close(file_fd);
                    return;
                }
                off += s;
            }
        }
    }

    close(file_fd);
}
