/* SIGTERM must lead to a clean exit (status 0) through microdlna.c's
 * "shutdown:" path (closes sockets, frees media_dir/pidfilename, exits
 * EXIT_SUCCESS). Run against a binary built with ASan/UBSan (see the
 * check-functional Makefile target), this is what actually proves the
 * absence of a leak or UB under live socket traffic -- the unit tests
 * never run main() at all, so they can't catch this class of bug. */

#include <stdio.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

TEST(sigterm_after_handling_requests_exits_cleanly)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", 64);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[256];
    snprintf(req, sizeof(req),
             "GET /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
             port);

    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) == 0)
    {
        ASSERT_INT_EQ(resp.status, 200);
        http_response_free(&resp);
    }
    else
    {
        tests_fail(__FILE__, __LINE__, "request failed");
    }

    ASSERT_TRUE(mdlna_stop(&proc));
}
