/* SOAP Browse against a real microdlnad instance: proves the
 * "stateless, derived from the filesystem on every request" design
 * (docs/ARCHITECTURE.md) actually holds for the running binary, not just
 * for get_directory_listing() in isolation. */

#include <stdio.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

/* Builds a minimal SOAP Browse request for ObjectID "0" (the media root),
 * requesting every child (RequestedCount "0" means "all", see
 * xmlregex.c/upnpsoap.c). */
static size_t build_browse_request(char *buf, size_t cap, int port)
{
    char body[512];
    int body_len = snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>0</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>0</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse>"
        "</s:Body>"
        "</s:Envelope>");

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

TEST(browse_lists_known_files_and_sees_filesystem_changes_live)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "first.mp3", 64);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char req[1024];
    size_t req_len = build_browse_request(req, sizeof(req), port);

    struct http_response resp;
    if (http_do_request(port, req, req_len, &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "Browse request failed");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_STR_CONTAINS(resp.body, "first.mp3");
    http_response_free(&resp);

    /* Add a second file on disk without restarting the server, then
     * Browse again: the new file must appear immediately, since nothing
     * is cached (ARCHITECTURE.md §2, "stateless"). */
    ftest_mkfile_pattern(root, "second.mp3", 64);

    if (http_do_request(port, req, req_len, &resp) != 0)
    {
        tests_fail(__FILE__, __LINE__, "second Browse request failed");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_STR_CONTAINS(resp.body, "first.mp3");
    ASSERT_STR_CONTAINS(resp.body, "second.mp3");
    http_response_free(&resp);

    mdlna_stop(&proc);
}
