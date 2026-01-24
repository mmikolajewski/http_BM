#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "handlers.h"
#include "constants.h"
#include "utils.h"

void handle_delete(int client_fd,
                   const char *root_dir,
                   const char *url_path)
{
    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0)
    {
        const char *msg = "Bad Request\r\n";
        send_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    if (unlink(full_path) == 0)
    {
        // 200 OK (można też 204 No Content)
        send_response(client_fd, 200, "OK", "text/plain", "Deleted\r\n", 9);
    }
    else
    {
        if (errno == ENOENT)
        {
            const char *msg = "Not Found\r\n";
            send_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        }
        else
        {
            const char *msg = "Internal Server Error\r\n";
            send_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        }
    }
}