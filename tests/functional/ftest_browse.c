/* SOAP Browse against a real microdlnad instance: the listing a control
 * point actually gets (ordering, filtering, sub-folders, pagination,
 * escaping of odd file names, URLs that really download), and the
 * "stateless, derived from the filesystem on every request" design
 * (docs/ARCHITECTURE.md) for the running binary.
 *
 * The DIDL-Lite document is sent XML-escaped inside the SOAP <Result>
 * element, and the names inside it are escaped once more
 * (xml_escape_double(), upnpsoap.c). didl_of() undoes both levels, the way
 * a control point would, so assertions are written against what a client
 * really sees. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

static size_t build_browse_request(char *buf, size_t cap, int port, const char *object_id,
                                    const char *flag, int start, int count)
{
    char body[1024];
    int body_len = snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>%s</ObjectID>"
        "<BrowseFlag>%s</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>%d</StartingIndex>"
        "<RequestedCount>%d</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse>"
        "</s:Body>"
        "</s:Envelope>",
        object_id, flag, start, count);

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

/* In-place XML entity decoding of the five predefined entities. */
static void xml_unescape_inplace(char *s)
{
    static const struct { const char *ent; char c; } map[] = {
        { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }, { "&amp;", '&' },
    };
    char *w = s;
    for (char *r = s; *r; )
    {
        int done = 0;
        for (unsigned i = 0; i < 5 && !done; i++)
        {
            size_t n = strlen(map[i].ent);
            if (strncmp(r, map[i].ent, n) == 0)
            {
                *w++ = map[i].c;
                r += n;
                done = 1;
            }
        }
        if (!done)
            *w++ = *r++;
    }
    *w = '\0';
}

struct browse_result
{
    int status;
    char *didl;           /* client-side view of the DIDL-Lite (malloc'd) */
    int number_returned;
    int total_matches;
};

static void browse(int port, const char *object_id, const char *flag, int start, int count,
                   struct browse_result *out)
{
    memset(out, 0, sizeof(*out));
    out->status = -1;

    char req[2048];
    size_t len = build_browse_request(req, sizeof(req), port, object_id, flag, start, count);
    struct http_response resp;
    if (http_do_request(port, req, len, &resp) != 0)
        return;

    out->status = resp.status;
    const char *r = strstr(resp.body, "<Result>");
    const char *e = strstr(resp.body, "</Result>");
    if (r && e)
    {
        r += 8;
        out->didl = strndup(r, (size_t)(e - r));
        xml_unescape_inplace(out->didl);   /* SOAP level */
    }
    const char *nr = strstr(resp.body, "<NumberReturned>");
    const char *tm = strstr(resp.body, "<TotalMatches>");
    if (nr)
        out->number_returned = atoi(nr + 16);
    if (tm)
        out->total_matches = atoi(tm + 14);
    http_response_free(&resp);
}

/* Finds the n-th "<dc:title>...</dc:title>" in the DIDL and returns the
 * title with the second (DIDL-level) escaping undone, in a static buffer. */
static const char *title_at(const char *didl, int n)
{
    static char title[256];
    const char *p = didl;
    for (int i = 0; p && i <= n; i++)
    {
        p = strstr(p, "<dc:title>");
        if (p)
            p += 10;
    }
    if (!p)
        return "";
    const char *e = strstr(p, "</dc:title>");
    size_t len = e ? (size_t)(e - p) : 0;
    if (len >= sizeof(title))
        len = sizeof(title) - 1;
    memcpy(title, p, len);
    title[len] = '\0';
    xml_unescape_inplace(title);
    return title;
}

TEST(browse_lists_known_files_and_sees_filesystem_changes_live)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "first.mp3", 64);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "0", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_STR_CONTAINS(b.didl, "first.mp3");
    free(b.didl);

    /* Add a second file on disk without restarting the server: it must
     * appear immediately, since nothing is cached. */
    ftest_mkfile_pattern(root, "second.mp3", 64);

    browse(port, "0", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_STR_CONTAINS(b.didl, "first.mp3");
    ASSERT_STR_CONTAINS(b.didl, "second.mp3");
    ASSERT_INT_EQ(b.number_returned, 2);
    ASSERT_INT_EQ(b.total_matches, 2);
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(browse_orders_folders_first_and_filters_hidden_and_unknown_files)
{
    const char *root = ftest_mkdir();
    ftest_mkpath(root, "Zeta");
    ftest_mkpath(root, "alpha");
    ftest_mkpath(root, ".hiddendir");
    ftest_mkfile_pattern(root, "b.mp3", 10);
    ftest_mkfile_pattern(root, "A.mp3", 10);
    ftest_mkfile_pattern(root, ".hidden.mp3", 10);
    ftest_mkfile_pattern(root, "$system.mp3", 10);
    ftest_mkfile_pattern(root, "notes.xyz", 10);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "0", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_INT_EQ(b.number_returned, 4);
    ASSERT_INT_EQ(b.total_matches, 4);

    /* containers first, then items, each sorted case-insensitively */
    ASSERT_STR_EQ(title_at(b.didl, 0), "alpha");
    ASSERT_STR_EQ(title_at(b.didl, 1), "Zeta");
    ASSERT_STR_EQ(title_at(b.didl, 2), "A.mp3");
    ASSERT_STR_EQ(title_at(b.didl, 3), "b.mp3");
    ASSERT_STR_CONTAINS(b.didl, "<container id=\"/alpha\"");
    ASSERT_STR_CONTAINS(b.didl, "object.container.storageFolder");
    ASSERT_STR_CONTAINS(b.didl, "object.item.audioItem");
    ASSERT_TRUE(strstr(b.didl, "hidden") == NULL);
    ASSERT_TRUE(strstr(b.didl, "system") == NULL);
    ASSERT_TRUE(strstr(b.didl, "notes.xyz") == NULL);
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(browse_into_a_subfolder_by_object_id)
{
    const char *root = ftest_mkdir();
    ftest_mkpath(root, "alpha/deeper");
    ftest_mkfile_pattern(root, "alpha/inner.mp4", 33);
    ftest_mkfile_pattern(root, "top.mp4", 10);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "alpha", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_INT_EQ(b.total_matches, 2);
    ASSERT_STR_CONTAINS(b.didl, "<container id=\"alpha/deeper\" parentID=\"alpha\"");
    ASSERT_STR_CONTAINS(b.didl, "<item id=\"alpha/inner.mp4\" parentID=\"alpha\"");
    ASSERT_STR_CONTAINS(b.didl, "size=\"33\"");
    ASSERT_STR_CONTAINS(b.didl, "protocolInfo=\"http-get:*:video/mp4:");
    ASSERT_TRUE(strstr(b.didl, "top.mp4") == NULL);

    /* the full port must be in the URL: a 5-digit port (ephemeral ports
     * always are) used to lose its last digit (6-byte snprintf limit) */
    char url[128];
    snprintf(url, sizeof(url), ">http://127.0.0.1:%d/MediaItems/alpha/inner.mp4</res>", port);
    ASSERT_STR_CONTAINS(b.didl, url);
    free(b.didl);

    browse(port, "missing", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 503);
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(browse_pagination_returns_the_requested_window)
{
    const char *root = ftest_mkdir();
    const char *names[] = { "f1.mp3", "f2.mp3", "f3.mp3", "f4.mp3", "f5.mp3" };
    for (int i = 0; i < 5; i++)
        ftest_mkfile_pattern(root, names[i], 10);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "0", "BrowseDirectChildren", 1, 2, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_INT_EQ(b.number_returned, 2);
    ASSERT_INT_EQ(b.total_matches, 5);
    ASSERT_STR_EQ(title_at(b.didl, 0), "f2.mp3");
    ASSERT_STR_EQ(title_at(b.didl, 1), "f3.mp3");
    free(b.didl);

    browse(port, "0", "BrowseDirectChildren", 4, 10, &b);
    ASSERT_INT_EQ(b.number_returned, 1);
    ASSERT_INT_EQ(b.total_matches, 5);
    ASSERT_STR_EQ(title_at(b.didl, 0), "f5.mp3");
    free(b.didl);

    browse(port, "0", "BrowseDirectChildren", 5, 10, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_INT_EQ(b.number_returned, 0);
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(odd_file_names_are_escaped_and_their_urls_download)
{
    const char *root = ftest_mkdir();
    const char *names[] = { "a & b.mp3", "\xc3\xa9t\xc3\xa9.mp3", "with space.mp3",
                            "quote\"and<tag>.mp3", "100%.mp3" };
    for (int i = 0; i < 5; i++)
        ftest_mkfile_pattern(root, names[i], 100 + (size_t)i);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "0", "BrowseDirectChildren", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_INT_EQ(b.number_returned, 5);

    for (int i = 0; i < 5; i++)
    {
        /* find this file's title, then the <res> URL that follows it */
        int found = 0;
        for (int t = 0; t < 5 && !found; t++)
            found = strcmp(title_at(b.didl, t), names[i]) == 0;
        if (!found)
        {
            tests_fail(__FILE__, __LINE__, "title for '%s' not found", names[i]);
            continue;
        }

        char url_prefix[64];
        snprintf(url_prefix, sizeof(url_prefix), "http://127.0.0.1:%d", port);
        const char *res = b.didl;
        int downloaded = 0;
        while ((res = strstr(res, url_prefix)) != NULL && !downloaded)
        {
            res += strlen(url_prefix);
            size_t plen = strcspn(res, "<");
            char path[512];
            snprintf(path, sizeof(path), "%.*s", (int)plen, res);
            xml_unescape_inplace(path);  /* DIDL level */

            char req[1024];
            snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
                     path, port);
            struct http_response resp;
            if (http_do_request(port, req, strlen(req), &resp) == 0)
            {
                if (resp.status == 200 && resp.body_len == 100 + (size_t)i)
                    downloaded = 1;
                http_response_free(&resp);
            }
        }
        if (!downloaded)
            tests_fail(__FILE__, __LINE__, "no advertised URL downloads '%s'", names[i]);
    }
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(browse_metadata_flag_is_ignored_and_lists_children)
{
    /* characterisation: BrowseFlag is never parsed (xmlregex.c only knows
     * ObjectID/ContainerID/StartingIndex/RequestedCount), so a
     * BrowseMetadata request gets the children listing instead of the
     * object's own metadata -- a missing feature, documented, not fixed */
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "song.mp3", 10);

    int port = ftest_alloc_port();
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct browse_result b;
    browse(port, "0", "BrowseMetadata", 0, 0, &b);
    ASSERT_INT_EQ(b.status, 200);
    ASSERT_STR_CONTAINS(b.didl, "song.mp3");
    free(b.didl);

    ASSERT_TRUE(mdlna_stop(&proc));
}
