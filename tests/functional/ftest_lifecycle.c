/* Process lifecycle under signals. SIGTERM/SIGINT must lead to a clean
 * exit (status 0) through microdlna.c's "shutdown:" path; run against the
 * ASan/UBSan build (check-functional), that also proves no leak or UB was
 * left behind by live traffic. SIGHUP reloads the network interfaces and
 * the server must keep working afterwards. */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"
#include "support/ssdpclient.h"

static int get_status(int port, const char *path)
{
    char req[256];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n", path, port);
    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
        return -1;
    int status = resp.status;
    http_response_free(&resp);
    return status;
}

static void nap_ms(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000 * 1000 };
    nanosleep(&ts, NULL);
}

TEST(sigterm_after_handling_requests_exits_cleanly)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", 64);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    ASSERT_INT_EQ(get_status(port, "/rootDesc.xml"), 200);
    ASSERT_INT_EQ(get_status(port, "/MediaItems/movie.mp4"), 200);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(sigint_exits_cleanly_like_sigterm)
{
    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, NULL);

    ASSERT_INT_EQ(get_status(port, "/rootDesc.xml"), 200);

    ASSERT_TRUE(mdlna_stop_with(&proc, SIGINT));
}

TEST(sighup_reloads_interfaces_and_server_keeps_working)
{
    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, NULL);

    for (int i = 0; i < 3; i++)
    {
        kill(proc.pid, SIGHUP);
        nap_ms(200);

        ASSERT_INT_EQ(get_status(port, "/rootDesc.xml"), 200);

        char reply[1024];
        ASSERT_INT_EQ(ssdp_msearch("upnp:rootdevice", 1000, reply, sizeof(reply)), 1);
    }

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(sighup_during_traffic_does_not_break_requests)
{
    /* a burst of SIGHUPs while requests are being served: every request
     * must still get its answer */
    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, NULL);

    for (int i = 0; i < 20; i++)
    {
        kill(proc.pid, SIGHUP);
        ASSERT_INT_EQ(get_status(port, "/rootDesc.xml"), 200);
    }

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(sigterm_during_a_stalled_transfer_still_exits_in_time)
{
    /* a client that stops reading pins a serve_file() thread in
     * write()/sendfile() (same technique as ftest_concurrency.c); the main
     * loop must still shut down within mdlna_stop()'s grace period */
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "big.mp4", 4 * 1024 * 1024);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    int fd = http_connect(port);
    char req[256];
    snprintf(req, sizeof(req), "GET /MediaItems/big.mp4 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
    ASSERT_TRUE(write(fd, req, strlen(req)) > 0);
    ASSERT_INT_EQ(http_read_status_only(fd), 200);
    nap_ms(200);

    ASSERT_TRUE(mdlna_stop(&proc));
    close(fd);
}
