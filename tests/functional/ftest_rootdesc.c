/* GET of the static UPnP description endpoints. These are always sent
 * chunked (upnphttp.c), never with Content-Length -- unlike the file
 * serving endpoint covered in ftest_fileserve.c. */

#include <stdio.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

static void get_and_check(int port, const char *path, const char *expect_fragment)
{
    char req[256];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
             path, port);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "request to %s failed", path);
        return;
    }

    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_STR_CONTAINS(resp.headers, "Transfer-Encoding: chunked");
    if (expect_fragment)
        ASSERT_STR_CONTAINS(resp.body, expect_fragment);

    http_response_free(&resp);
}

TEST(description_endpoints_return_200_chunked)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    get_and_check(port, "/rootDesc.xml", "<deviceType>");
    get_and_check(port, "/ContentDir.xml", "BrowseDirectChildren");
    get_and_check(port, "/ConnectionMgr.xml", "ConnectionManager");
    get_and_check(port, "/X_MS_MediaReceiverRegistrar.xml", NULL);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(unknown_path_returns_404)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    snprintf(req, sizeof(req),
             "GET /does-not-exist HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
             port);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) == 0)
    {
        ASSERT_INT_EQ(resp.status, 404);
        http_response_free(&resp);
    }
    else
    {
        tests_fail(__FILE__, __LINE__, "request failed");
    }

    ASSERT_TRUE(mdlna_stop(&proc));
}
