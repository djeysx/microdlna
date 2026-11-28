#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "eventsink.h"

int eventsink_open(int *port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        abort();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0)
        abort();

    socklen_t len = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &len) != 0)
        abort();

    *port = ntohs(addr.sin_port);
    return s;
}

int eventsink_accept(int listen_fd, int timeout_ms, int reply, struct http_response *msg)
{
    memset(msg, 0, sizeof(*msg));

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    if (select(listen_fd + 1, &rfds, NULL, NULL, &tv) <= 0)
        return -1;

    int c = accept(listen_fd, NULL, NULL);
    if (c < 0)
        return -1;

    /* never hang the test on a request that doesn't complete */
    struct timeval rcv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));

    int rc = http_read_message(c, 1, msg);

    if (rc == 0 && reply)
    {
        static const char ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        if (write(c, ok, sizeof(ok) - 1) < 0)
            rc = -1;
    }

    close(c);
    return rc;
}
