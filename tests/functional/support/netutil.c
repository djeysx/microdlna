#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netutil.h"

int ftest_alloc_port(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        abort();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        abort();

    socklen_t len = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &len) != 0)
        abort();

    int port = ntohs(addr.sin_port);
    close(s);
    return port;
}
