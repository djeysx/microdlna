#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ssdpclient.h"

int ssdp_msearch(int timeout_ms, char *reply_buf, size_t reply_cap)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return -1;

    struct in_addr mc_if;
    mc_if.s_addr = inet_addr("127.0.0.1");
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &mc_if, sizeof(mc_if)) < 0)
    {
        close(s);
        return -1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr("239.255.255.250");

    static const char msearch[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";

    if (sendto(s, msearch, sizeof(msearch) - 1, 0,
               (struct sockaddr *)&dest, sizeof(dest)) < 0)
    {
        close(s);
        return -1;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rc = select(s + 1, &rfds, NULL, NULL, &tv);
    if (rc <= 0)
    {
        close(s);
        return -1;
    }

    ssize_t n = recv(s, reply_buf, reply_cap - 1, 0);
    close(s);
    if (n < 0)
        return -1;

    reply_buf[n] = '\0';
    return (int)n;
}
