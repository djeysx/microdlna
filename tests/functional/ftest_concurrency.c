/* Limits of the threading model, around max_connections (threads.c:
 * create_thread() refuses to spawn once active_threads reaches
 * max_connections, and send_resp_dlnafile() turns that refusal into a 500,
 * upnphttp.c). */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

static int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

#define BIG_FILE_SIZE (4 * 1024 * 1024)

TEST(max_connections_limit_returns_500_once_saturated_then_recovers)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "big.mp4", BIG_FILE_SIZE);

    int port = ftest_alloc_port();
    const char *extra_args[] = { "-c", "2", NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, extra_args);

    char req[256];
    snprintf(req, sizeof(req), "GET /MediaItems/big.mp4 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n",
             port);

    int fd_a = http_connect(port);
    int fd_b = http_connect(port);
    ASSERT_TRUE(fd_a >= 0);
    ASSERT_TRUE(fd_b >= 0);

    ASSERT_INT_EQ(write_all(fd_a, req, strlen(req)), 0);
    ASSERT_INT_EQ(write_all(fd_b, req, strlen(req)), 0);

    /* Confirm both transfers actually started -- headers for a file
     * response are only sent from inside serve_file(), i.e. after
     * create_thread() already succeeded (upnphttp.c), so seeing "200"
     * here means active_threads is already at 2. Deliberately never read
     * past the headers: the server's write()/sendfile() will then block
     * once the kernel socket buffers fill, since nothing drains the rest
     * of the 4MB body -- that's what pins active_threads at the limit
     * without depending on timing. */
    ASSERT_INT_EQ(http_read_status_only(fd_a), 200);
    ASSERT_INT_EQ(http_read_status_only(fd_b), 200);

    /* Give the now-running serve_file threads a moment to actually block
     * in write()/sendfile(), rather than just having sent their headers. */
    struct timespec settle = { .tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 };
    nanosleep(&settle, NULL);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "third request failed outright");
    }
    else
    {
        ASSERT_INT_EQ(resp.status, 500);
        http_response_free(&resp);
    }

    /* Release one slot: a closed socket with an undrained receive buffer
     * makes the kernel send a RST, which unblocks the peer's write() with
     * an error -- serve_file()'s error path then runs
     * decrement_thread_count(). Poll (rather than guess a sleep) until
     * capacity is confirmed back, since exactly how fast that unblocking
     * happens isn't something to hard-code a timing assumption around. */
    close(fd_a);

    int recovered = 0;
    for (int attempt = 0; attempt < 40 && !recovered; attempt++)
    {
        if (http_do_request(port, req, strlen(req), &resp) == 0)
        {
            if (resp.status == 200)
                recovered = 1;
            http_response_free(&resp);
        }
        struct timespec nap = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    ASSERT_TRUE(recovered);

    close(fd_b);
    mdlna_stop(&proc);
}

#define NUM_CLIENT_THREADS 4
#define REQUESTS_PER_THREAD 3

struct client_thread_arg
{
    int port;
    int ok; /* written only by this thread itself -- no shared-state race */
};

static void *client_thread_main(void *arg_)
{
    struct client_thread_arg *arg = arg_;
    arg->ok = 1;

    for (int i = 0; i < REQUESTS_PER_THREAD; i++)
    {
        char req[256];
        snprintf(req, sizeof(req),
                 "GET /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
                 arg->port);

        struct http_response resp;
        if (http_do_request(arg->port, req, strlen(req), &resp) != 0)
        {
            arg->ok = 0;
            continue;
        }
        if (resp.status != 200)
            arg->ok = 0;
        http_response_free(&resp);
    }

    return NULL;
}

TEST(many_concurrent_short_requests_do_not_crash_or_leak)
{
    const char *root = ftest_mkdir();

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    pthread_t threads[NUM_CLIENT_THREADS];
    struct client_thread_arg args[NUM_CLIENT_THREADS];

    for (int i = 0; i < NUM_CLIENT_THREADS; i++)
    {
        args[i].port = port;
        args[i].ok = 0;
        if (pthread_create(&threads[i], NULL, client_thread_main, &args[i]) != 0)
            abort();
    }

    for (int i = 0; i < NUM_CLIENT_THREADS; i++)
        pthread_join(threads[i], NULL);

    for (int i = 0; i < NUM_CLIENT_THREADS; i++)
        ASSERT_TRUE(args[i].ok);

    /* A real leak or UB triggered by concurrent traffic would make the
     * ASan/UBSan-instrumented server exit non-zero here instead of
     * cleanly (see check-functional's sanitized build). */
    ASSERT_TRUE(mdlna_stop(&proc));
}
