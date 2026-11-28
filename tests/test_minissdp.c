/* Tests for minissdp.c: M-SEARCH parsing/validation and the format of the
 * replies, NOTIFY alive and byebye messages. minissdp.o is excluded from
 * this binary's link.
 *
 * process_ssdp_request() reads one datagram from the socket it is given,
 * so these tests use plain unicast UDP sockets on 127.0.0.1 -- no
 * multicast needed: a "server" socket bound to 127.0.0.1:0 (with
 * IP_PKTINFO, as open_ssdp_receive_socket() sets it) and a "client" socket
 * that sends the request and receives the unicast reply. */

#include "../minissdp.c"

#include <sys/select.h>

#include "test.h"

struct udp_pair
{
    int server;
    int client;
    struct sockaddr_in server_addr;
};

static void udp_pair_open(struct udp_pair *u)
{
    u->server = socket(AF_INET, SOCK_DGRAM, 0);
    u->client = socket(AF_INET, SOCK_DGRAM, 0);
    if (u->server < 0 || u->client < 0)
        abort();

    int one = 1;
    setsockopt(u->server, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one));

    memset(&u->server_addr, 0, sizeof(u->server_addr));
    u->server_addr.sin_family = AF_INET;
    u->server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(u->server, (struct sockaddr *)&u->server_addr, sizeof(u->server_addr)) != 0)
        abort();
    socklen_t len = sizeof(u->server_addr);
    getsockname(u->server, (struct sockaddr *)&u->server_addr, &len);
}

static void udp_pair_close(struct udp_pair *u)
{
    close(u->server);
    close(u->client);
}

/* Sends `msg` from the client, lets process_ssdp_request() handle it, then
 * collects every reply that arrives within timeout_ms into `replies`
 * (concatenated, NUL-terminated). Returns the number of replies. */
static int exchange(const char *msg, char *replies, size_t cap, int timeout_ms)
{
    struct udp_pair u;
    udp_pair_open(&u);

    sendto(u.client, msg, strlen(msg), 0, (struct sockaddr *)&u.server_addr,
           sizeof(u.server_addr));
    process_ssdp_request(u.server);

    int count = 0;
    size_t used = 0;
    replies[0] = '\0';
    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(u.client, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = timeout_ms * 1000 };
        if (select(u.client + 1, &rfds, NULL, NULL, &tv) <= 0)
            break;

        ssize_t n = recv(u.client, replies + used, cap - used - 1, 0);
        if (n <= 0)
            break;
        used += (size_t)n;
        replies[used] = '\0';
        count++;
    }

    udp_pair_close(&u);
    return count;
}

#define MSEARCH(st) \
    "M-SEARCH * HTTP/1.1\r\n" \
    "HOST: 239.255.255.250:1900\r\n" \
    "MAN: \"ssdp:discover\"\r\n" \
    "MX: 1\r\n" \
    "ST: " st "\r\n" \
    "\r\n"

static char replies[16384];

/* ---- valid searches ---------------------------------------------------- */

TEST(msearch_rootdevice_gets_one_reply_with_location)
{
    ASSERT_INT_EQ(exchange(MSEARCH("upnp:rootdevice"), replies, sizeof(replies), 200), 1);
    ASSERT_STR_CONTAINS(replies, "HTTP/1.1 200 OK\r\n");
    ASSERT_STR_CONTAINS(replies, "ST: upnp:rootdevice\r\n");
    ASSERT_STR_CONTAINS(replies, "USN: uuid:4d696e69-444c-164e-9d41-000000000000::upnp:rootdevice\r\n");
    ASSERT_STR_CONTAINS(replies, "LOCATION: http://127.0.0.1:2800/rootDesc.xml\r\n");
}

TEST(msearch_device_uuid_gets_one_reply)
{
    ASSERT_INT_EQ(exchange(MSEARCH("uuid:4d696e69-444c-164e-9d41-000000000000"),
                           replies, sizeof(replies), 200), 1);
    ASSERT_STR_CONTAINS(replies, "USN: uuid:4d696e69-444c-164e-9d41-000000000000\r\n");
}

TEST(msearch_versioned_types_are_answered_with_version_1)
{
    const char *types[] = {
        MSEARCH("urn:schemas-upnp-org:device:MediaServer:1"),
        MSEARCH("urn:schemas-upnp-org:service:ContentDirectory:1"),
        MSEARCH("urn:schemas-upnp-org:service:ConnectionManager:1"),
        MSEARCH("urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1"),
    };
    for (int i = 0; i < 4; i++)
        ASSERT_INT_EQ(exchange(types[i], replies, sizeof(replies), 200), 1);

    exchange(types[1], replies, sizeof(replies), 200);
    ASSERT_STR_CONTAINS(replies, "ST: urn:schemas-upnp-org:service:ContentDirectory:1\r\n");
}

TEST(msearch_unversioned_type_is_answered)
{
    ASSERT_INT_EQ(exchange(MSEARCH("urn:schemas-upnp-org:device:MediaServer:"),
                           replies, sizeof(replies), 200), 1);
}

TEST(msearch_ssdp_all_gets_one_reply_per_known_type)
{
    ASSERT_INT_EQ(exchange(MSEARCH("ssdp:all"), replies, sizeof(replies), 200), 6);
}

/* ---- searches that must be ignored ------------------------------------ */

TEST(msearch_higher_version_or_garbage_suffix_is_ignored)
{
    ASSERT_INT_EQ(exchange(MSEARCH("urn:schemas-upnp-org:device:MediaServer:2"),
                           replies, sizeof(replies), 100), 0);
    ASSERT_INT_EQ(exchange(MSEARCH("urn:schemas-upnp-org:device:MediaServer:1x"),
                           replies, sizeof(replies), 100), 0);
    ASSERT_INT_EQ(exchange(MSEARCH("urn:schemas-upnp-org:device:Printer:1"),
                           replies, sizeof(replies), 100), 0);
}

TEST(msearch_with_missing_or_bad_headers_is_ignored)
{
    const char *bad[] = {
        /* no MAN */
        "M-SEARCH * HTTP/1.1\r\nMX: 1\r\nST: ssdp:all\r\n\r\n",
        /* wrong MAN */
        "M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:other\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n",
        /* no MX */
        "M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nST: ssdp:all\r\n\r\n",
        /* non-numeric MX */
        "M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nMX: x\r\nST: ssdp:all\r\n\r\n",
        /* negative MX */
        "M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nMX: -1\r\nST: ssdp:all\r\n\r\n",
        /* no ST */
        "M-SEARCH * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\n\r\n",
        /* not an M-SEARCH */
        "NOTIFY * HTTP/1.1\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n",
        "",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        ASSERT_INT_EQ(exchange(bad[i], replies, sizeof(replies), 100), 0);
}

TEST(msearch_longer_than_200_bytes_is_still_answered)
{
    /* Real control points send USER-AGENT (and other) headers that push an
     * M-SEARCH well past 200 bytes. process_ssdp_request() used to read
     * into a 200-byte buffer, silently truncating the datagram (its
     * "exceeded buffer" check could never trigger), so an ST header after
     * the cut was lost and the search went unanswered. */
    const char *msg =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "USER-AGENT: Linux/6.1 UPnP/1.1 SomeVeryLongControlPointName/12.34.56 "
        "(with a long build description string)\r\n"
        "CPFN.UPNP.ORG: Living room renderer\r\n"
        "ST: upnp:rootdevice\r\n"
        "\r\n";
    ASSERT_TRUE(strlen(msg) > 200);
    ASSERT_INT_EQ(exchange(msg, replies, sizeof(replies), 200), 1);
}

TEST(msearch_filling_the_whole_buffer_is_rejected_as_possibly_truncated)
{
    static char msg[2000];
    snprintf(msg, sizeof(msg), "%s", MSEARCH("upnp:rootdevice"));
    size_t len = strlen(msg) - 2; /* insert padding before the final CRLF */
    snprintf(msg + len, sizeof(msg) - len, "X-PAD: ");
    len += 7;
    memset(msg + len, 'a', 1600);
    snprintf(msg + len + 1600, sizeof(msg) - len - 1600, "\r\n\r\n");

    ASSERT_INT_EQ(exchange(msg, replies, sizeof(replies), 100), 0);
}

/* ---- NOTIFY alive / byebye format --------------------------------------
 * send_ssdp_notifies()/send_ssdp_goodbyes() always send to
 * 239.255.255.250:1900, so here the sending socket is routed over lo with
 * multicast loopback enabled, and a receiver joins the group on lo. */

static int open_multicast_receiver(void)
{
    int r = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1, zero = 0;
    setsockopt(r, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef IP_MULTICAST_ALL
    setsockopt(r, IPPROTO_IP, IP_MULTICAST_ALL, &zero, sizeof(zero));
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(1900);
    a.sin_addr.s_addr = inet_addr("239.255.255.250");
    if (bind(r, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(r);
        return -1;
    }
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = htonl(INADDR_LOOPBACK);
    if (setsockopt(r, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
    {
        close(r);
        return -1;
    }
    return r;
}

static int open_multicast_sender(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct in_addr lo = { .s_addr = htonl(INADDR_LOOPBACK) };
    unsigned char loop = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &lo, sizeof(lo));
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    return s;
}

/* Reads every datagram waiting on r that mentions our test LOCATION
 * (other SSDP traffic on the machine is ignored). */
static int collect_ours(int r, char *out, size_t cap)
{
    int count = 0;
    size_t used = 0;
    out[0] = '\0';
    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(r, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 300 * 1000 };
        if (select(r + 1, &rfds, NULL, NULL, &tv) <= 0)
            break;
        char buf[1024];
        ssize_t n = recv(r, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
            break;
        buf[n] = '\0';
        if (!strstr(buf, "uuid:4d696e69-444c-164e-9d41-000000000000"))
            continue;
        if (used + (size_t)n + 1 < cap)
        {
            memcpy(out + used, buf, (size_t)n + 1);
            used += (size_t)n;
        }
        count++;
    }
    return count;
}

TEST(notify_alive_sends_each_type_twice)
{
    int r = open_multicast_receiver();
    if (r < 0)
    {
        fprintf(stderr, "  (skipped: cannot bind/join 239.255.255.250:1900 on lo) ");
        return;
    }
    int s = open_multicast_sender();

    send_ssdp_notifies(s, "127.0.0.1");

    ASSERT_INT_EQ(collect_ours(r, replies, sizeof(replies)), 12);
    ASSERT_STR_CONTAINS(replies, "NOTIFY * HTTP/1.1\r\n");
    ASSERT_STR_CONTAINS(replies, "NTS:ssdp:alive\r\n");
    ASSERT_STR_CONTAINS(replies, "LOCATION:http://127.0.0.1:2800/rootDesc.xml\r\n");
    ASSERT_STR_CONTAINS(replies, "NT:urn:schemas-upnp-org:service:ContentDirectory:1\r\n");
    ASSERT_STR_CONTAINS(replies,
        "USN:uuid:4d696e69-444c-164e-9d41-000000000000::upnp:rootdevice\r\n");

    close(s);
    close(r);
}

TEST(notify_byebye_sends_each_type_twice)
{
    int r = open_multicast_receiver();
    if (r < 0)
    {
        fprintf(stderr, "  (skipped: cannot bind/join 239.255.255.250:1900 on lo) ");
        return;
    }
    int s = open_multicast_sender();

    send_ssdp_goodbyes(s);

    ASSERT_INT_EQ(collect_ours(r, replies, sizeof(replies)), 12);
    ASSERT_STR_CONTAINS(replies, "NTS: ssdp:byebye\r\n");
    ASSERT_STR_CONTAINS(replies, "NT: upnp:rootdevice\r\n");

    close(s);
    close(r);
}
