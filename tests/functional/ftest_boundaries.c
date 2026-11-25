/* Parameters outside their normal bounds: a malicious or buggy client can
 * send any of these, and the server must degrade predictably (an error
 * response, or simply closing the connection) rather than crash or hang.
 * Each finding below was checked against the actual source before being
 * written as an assertion, not assumed. */

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

static size_t build_browse_request(char *buf, size_t cap, int port,
                                    const char *object_id, const char *requested_count)
{
    char body[512];
    int body_len = snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>%s</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>%s</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse>"
        "</s:Body>"
        "</s:Envelope>",
        object_id, requested_count);

    return (size_t)snprintf(buf, cap,
        "POST /ctl/ContentDir HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Connection: close\r\n"
        "SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        port, body_len, body);
}

static void check_200_with_both_files(int port, const char *requested_count)
{
    char req[1024];
    size_t req_len = build_browse_request(req, sizeof(req), port, "0", requested_count);

    struct http_response resp;
    if (http_do_request(port, req, req_len, &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "Browse (RequestedCount=%s) request failed",
                   requested_count);
        return;
    }

    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_STR_CONTAINS(resp.body, "a.mp3");
    ASSERT_STR_CONTAINS(resp.body, "b.mp3");
    http_response_free(&resp);
}

TEST(browse_requested_count_negative_is_clamped_to_all_like_zero)
{
    /* upnpsoap.c: "if (h->requested_count < 1) h->requested_count = -1;"
     * -1 is the sentinel for "every item" (same as RequestedCount=0) --
     * confirm a negative value is funnelled into the same safe path. */
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "a.mp3", 16);
    ftest_mkfile_pattern(root, "b.mp3", 16);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    check_200_with_both_files(port, "-1");

    mdlna_stop(&proc);
}

TEST(browse_requested_count_larger_than_directory_is_clamped_not_overrun)
{
    /* dirlist.c clamps RequestedCount to the number of entries actually
     * available (unsigned comparison against file_count) -- confirm a
     * huge value never causes an over-read or a hang. */
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "a.mp3", 16);
    ftest_mkfile_pattern(root, "b.mp3", 16);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    check_200_with_both_files(port, "999999999");

    mdlna_stop(&proc);
}

TEST(browse_path_traversal_object_id_is_rejected)
{
    /* sanitise_path() (utils.c) rejects a ".." that has no earlier path
     * component to consume; dirlist.c turns that into 403. */
    const char *root = ftest_mkdir();

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[1024];
    size_t req_len = build_browse_request(req, sizeof(req), port, "../../../etc", "0");

    struct http_response resp;
    if (http_do_request(port, req, req_len, &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_INT_EQ(resp.status, 403);
    http_response_free(&resp);
    mdlna_stop(&proc);
}

TEST(browse_with_negative_content_length_is_rejected_cleanly)
{
    /* parse_http_header() clamps a negative Content-Length to 0
     * (upnphttp.c), so process_post_content() never runs and
     * h->remote_dirpath stays NULL; browse_content_directory() (upnpsoap.c)
     * explicitly guards that case with a 402 SOAP error, never a crash. */
    const char *root = ftest_mkdir();

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[512];
    snprintf(req, sizeof(req),
             "POST /ctl/ContentDir HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Connection: close\r\n"
             "SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"\r\n"
             "Content-Length: -5\r\n"
             "\r\n",
             port);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_INT_EQ(resp.status, 402);
    http_response_free(&resp);
    mdlna_stop(&proc);
}

TEST(oversized_request_line_closes_connection_without_crashing_server)
{
    /* readline() (upnphttp.c) bails out with -1 once a line exceeds its
     * 1024-byte limit without finding "\r\n", and the caller just closes
     * the connection -- no response, but no crash either. Proven here by
     * immediately issuing a normal request on a fresh connection and
     * checking the server is still alive and answering. */
    const char *root = ftest_mkdir();

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char oversized[2048];
    memset(oversized, 'A', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';

    char req[2200];
    snprintf(req, sizeof(req), "GET /%s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n",
             oversized, port);

    struct http_response resp;
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), -1);

    char liveness_req[256];
    snprintf(liveness_req, sizeof(liveness_req),
             "GET /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
             port);
    if (http_do_request(port, liveness_req, strlen(liveness_req), &resp) == 0)
    {
        ASSERT_INT_EQ(resp.status, 200);
        http_response_free(&resp);
    }
    else
    {
        tests_fail(__FILE__, __LINE__, "server did not survive the oversized request line");
    }

    mdlna_stop(&proc);
}

TEST(invalid_cli_port_exits_immediately_without_listening)
{
    /* microdlna.c validates -p during CLI parsing itself
     * ("if (listening_port < 1 || listening_port > 65535) EXIT_ERROR(...)")
     * and exits before ever creating a socket. */
    const char *root = ftest_mkdir();

    int status = mdlna_expect_fast_exit(root, 99999, NULL);

    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_INT_EQ(WEXITSTATUS(status), 1);
}

TEST(missing_media_dir_starts_fine_but_browse_returns_503)
{
    /* media_dir is only required to be *set*, not to exist on disk --
     * nothing checks it exists at startup (mediadir.c resolves it lazily,
     * on the first request that needs it). */
    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, "/no/such/media/directory", port, NULL);

    char req[1024];
    size_t req_len = build_browse_request(req, sizeof(req), port, "0", "0");

    struct http_response resp;
    if (http_do_request(port, req, req_len, &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request failed");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_INT_EQ(resp.status, 503);
    http_response_free(&resp);
    mdlna_stop(&proc);
}
