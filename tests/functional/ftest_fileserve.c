/* GET /MediaItems/<file>: full request (Content-Length framing, never
 * chunked, see upnphttp.c ~l.834/843) and the Range subset (upnphttp.c
 * ~l.806-843):
 *   - a satisfiable byte range -> 206 + Content-Range
 *   - start > end, or start < 0  -> 400 (upnphttp.c, "Specified range was
 *     invalid!")
 *   - an explicit end >= file size -> 416 (upnphttp.c, "Specified range
 *     was outside file boundaries!")
 */

#include <stdio.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

#define FILE_SIZE 100

static void build_get(char *buf, size_t cap, int port, const char *range_header)
{
    snprintf(buf, cap,
             "GET /MediaItems/movie.mp4 HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Connection: close\r\n"
             "%s"
             "\r\n",
             port, range_header ? range_header : "");
}

TEST(file_serving_full_request_returns_exact_bytes)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", FILE_SIZE);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    build_get(req, sizeof(req), port, NULL);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        ASSERT_TRUE(mdlna_stop(&proc));
        return;
    }

    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_INT_EQ((long long)resp.body_len, FILE_SIZE);

    unsigned char expected[FILE_SIZE];
    for (int i = 0; i < FILE_SIZE; i++)
        expected[i] = (unsigned char)(i % 256);
    ASSERT_MEM_EQ(resp.body, expected, FILE_SIZE);

    http_response_free(&resp);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(file_serving_satisfiable_range_returns_206_with_exact_slice)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", FILE_SIZE);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    build_get(req, sizeof(req), port, "Range: bytes=10-19\r\n");

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        ASSERT_TRUE(mdlna_stop(&proc));
        return;
    }

    ASSERT_INT_EQ(resp.status, 206);

    char expect_range[64];
    snprintf(expect_range, sizeof(expect_range), "Content-Range: bytes 10-19/%d", FILE_SIZE);
    ASSERT_STR_CONTAINS(resp.headers, expect_range);

    ASSERT_INT_EQ((long long)resp.body_len, 10);
    unsigned char expected[10];
    for (int i = 0; i < 10; i++)
        expected[i] = (unsigned char)((10 + i) % 256);
    ASSERT_MEM_EQ(resp.body, expected, 10);

    http_response_free(&resp);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(file_serving_range_start_after_end_returns_400)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", FILE_SIZE);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    build_get(req, sizeof(req), port, "Range: bytes=50-10\r\n");

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        ASSERT_TRUE(mdlna_stop(&proc));
        return;
    }

    ASSERT_INT_EQ(resp.status, 400);
    http_response_free(&resp);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(file_serving_range_beyond_end_of_file_returns_416)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", FILE_SIZE);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    /* explicit end beyond the file size: req_range_end stays as given
     * (upnphttp.c only defaults a *missing* end to size - 1), so this
     * trips the "end >= size" check, not the "start > end" one. */
    build_get(req, sizeof(req), port, "Range: bytes=0-99999999\r\n");

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        ASSERT_TRUE(mdlna_stop(&proc));
        return;
    }

    ASSERT_INT_EQ(resp.status, 416);
    http_response_free(&resp);
    ASSERT_TRUE(mdlna_stop(&proc));
}
