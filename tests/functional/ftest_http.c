/* HTTP / DLNA protocol conformance of the real server: HEAD, icons, the
 * Host check (anti DNS rebinding), unsupported methods and versions, POST
 * framing, every SOAP action, the DLNA request headers, subtitles
 * (CaptionInfo.sec) and the MediaItems error paths. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

/* Sends `req` and reads everything until the server closes the
 * connection. Returns the number of bytes read, or -1. Used where
 * http_do_request()'s framing logic doesn't apply (HEAD responses
 * advertise a body they don't send). */
static int raw_exchange(int port, const char *req, char *buf, size_t cap)
{
    int fd = http_connect(port);
    if (fd < 0)
        return -1;
    if (write(fd, req, strlen(req)) < 0)
    {
        close(fd);
        return -1;
    }
    size_t used = 0;
    ssize_t n;
    while (used + 1 < cap && (n = read(fd, buf + used, cap - used - 1)) > 0)
        used += (size_t)n;
    buf[used] = '\0';
    close(fd);
    return (int)used;
}

/* Sends "<method> <path> HTTP/1.1" with a correct Host plus extra_headers,
 * and returns the status (or -1); the response is stored in *resp if
 * non-NULL (caller frees). */
static int request(int port, const char *method, const char *path,
                   const char *extra_headers, struct http_response *resp)
{
    char req[2048];
    snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n%s\r\n",
             method, path, port, extra_headers ? extra_headers : "");
    struct http_response local;
    struct http_response *r = resp ? resp : &local;
    if (http_do_request(port, req, strlen(req), r) != 0)
        return -1;
    int status = r->status;
    if (!resp)
        http_response_free(r);
    return status;
}

static int soap(int port, const char *action, const char *body, struct http_response *resp)
{
    char headers[512];
    snprintf(headers, sizeof(headers),
             "SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#%s\"\r\n"
             "Content-Length: %zu\r\n", action, strlen(body));
    char req[4096];
    snprintf(req, sizeof(req), "POST /ctl/ContentDir HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n%s\r\n%s",
             port, headers, body);
    if (http_do_request(port, req, strlen(req), resp) != 0)
        return -1;
    return resp->status;
}

#define START(root)                                         \
    int port = ftest_alloc_port();                          \
    struct mdlna_proc proc;                                 \
    mdlna_start(&proc, (root), port, NULL)

/* ---- HEAD --------------------------------------------------------------- */

TEST(head_returns_headers_without_body)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", 1000);
    START(root);

    char req[256], buf[4096];

    snprintf(req, sizeof(req), "HEAD /MediaItems/movie.mp4 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
    int n = raw_exchange(port, req, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strncmp(buf, "HTTP/1.1 200", 12) == 0);
    ASSERT_STR_CONTAINS(buf, "Content-Length: 1000\r\n");
    const char *end = strstr(buf, "\r\n\r\n");
    ASSERT_TRUE(end && (int)(end + 4 - buf) == n);

    snprintf(req, sizeof(req), "HEAD /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
    n = raw_exchange(port, req, buf, sizeof(buf));
    ASSERT_TRUE(strncmp(buf, "HTTP/1.1 200", 12) == 0);
    end = strstr(buf, "\r\n\r\n");
    ASSERT_TRUE(end && (int)(end + 4 - buf) == n);

    snprintf(req, sizeof(req), "HEAD /icons/sm.png HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
    n = raw_exchange(port, req, buf, sizeof(buf));
    ASSERT_TRUE(strncmp(buf, "HTTP/1.1 200", 12) == 0);
    end = strstr(buf, "\r\n\r\n");
    ASSERT_TRUE(end && (int)(end + 4 - buf) == n);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- icons -------------------------------------------------------------- */

TEST(icons_are_served_with_their_image_type)
{
    START(ftest_mkdir());

    const char *png_sig = "\x89PNG\r\n\x1a\n";
    const char *names[] = { "/icons/sm.png", "/icons/lrg.png", "/icons/sm.jpg", "/icons/lrg.jpg" };
    for (int i = 0; i < 4; i++)
    {
        struct http_response resp;
        if (request(port, "GET", names[i], NULL, &resp) != 200)
        {
            tests_fail(__FILE__, __LINE__, "%s did not return 200", names[i]);
            continue;
        }
        if (i < 2)
        {
            ASSERT_STR_CONTAINS(resp.headers, "Content-Type: image/png");
            ASSERT_TRUE(resp.body_len > 8 && memcmp(resp.body, png_sig, 8) == 0);
        }
        else
        {
            ASSERT_STR_CONTAINS(resp.headers, "Content-Type: image/jpeg");
            ASSERT_TRUE(resp.body_len > 3 && memcmp(resp.body, "\xff\xd8\xff", 3) == 0);
        }
        http_response_free(&resp);
    }

    ASSERT_INT_EQ(request(port, "GET", "/icons/nope.png", NULL, NULL), 404);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- request line and Host header --------------------------------------- */

TEST(host_header_must_match_the_listening_address)
{
    START(ftest_mkdir());

    char req[256];
    struct http_response resp;

    snprintf(req, sizeof(req), "GET /rootDesc.xml HTTP/1.1\r\n\r\n");
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), 0);
    ASSERT_INT_EQ(resp.status, 400);
    http_response_free(&resp);

    snprintf(req, sizeof(req), "GET /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port + 1);
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), 0);
    ASSERT_INT_EQ(resp.status, 400);
    http_response_free(&resp);

    snprintf(req, sizeof(req), "GET /rootDesc.xml HTTP/1.1\r\nHost: evil.example:%d\r\n\r\n", port);
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), 0);
    ASSERT_INT_EQ(resp.status, 400);
    http_response_free(&resp);

    ASSERT_INT_EQ(request(port, "GET", "/rootDesc.xml", NULL, NULL), 200);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(unsupported_method_or_version_returns_501)
{
    START(ftest_mkdir());

    ASSERT_INT_EQ(request(port, "PUT", "/rootDesc.xml", NULL, NULL), 501);
    ASSERT_INT_EQ(request(port, "DELETE", "/rootDesc.xml", NULL, NULL), 501);

    char req[256];
    snprintf(req, sizeof(req), "GET /rootDesc.xml HTTP/1.0\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
    struct http_response resp;
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), 0);
    ASSERT_INT_EQ(resp.status, 501);
    http_response_free(&resp);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- POST framing and SOAP actions -------------------------------------- */

TEST(post_body_larger_than_2048_bytes_is_rejected)
{
    START(ftest_mkdir());

    ASSERT_INT_EQ(request(port, "POST", "/ctl/ContentDir",
                          "SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"\r\n"
                          "Content-Length: 4096\r\n", NULL), 400);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(browse_with_chunked_request_body_is_understood)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "song.mp3", 10);
    START(root);

    const char *part1 = "<s:Envelope><s:Body><u:Browse><ObjectID>0</Obj";
    const char *part2 = "ectID><RequestedCount>0</RequestedCount></u:Browse></s:Body></s:Envelope>";
    char req[2048];
    snprintf(req, sizeof(req),
             "POST /ctl/ContentDir HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
             "SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"\r\n"
             "Transfer-Encoding: chunked\r\n\r\n"
             "%zx\r\n%s\r\n%zx\r\n%s\r\n0\r\n\r\n",
             port, strlen(part1), part1, strlen(part2), part2);

    struct http_response resp;
    ASSERT_INT_EQ(http_do_request(port, req, strlen(req), &resp), 0);
    ASSERT_INT_EQ(resp.status, 200);
    ASSERT_STR_CONTAINS(resp.body, "song.mp3");
    http_response_free(&resp);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(every_soap_action_has_the_expected_answer)
{
    START(ftest_mkdir());
    struct http_response resp;

    ASSERT_INT_EQ(soap(port, "GetSearchCapabilities", "", &resp), 200);
    ASSERT_STR_CONTAINS(resp.body, "<SearchCaps>");
    http_response_free(&resp);

    ASSERT_INT_EQ(soap(port, "GetSortCapabilities", "", &resp), 200);
    ASSERT_STR_CONTAINS(resp.body, "<SortCaps>dc:title,</SortCaps>");
    http_response_free(&resp);

    ASSERT_INT_EQ(soap(port, "GetProtocolInfo", "", &resp), 200);
    ASSERT_STR_CONTAINS(resp.body, "<Source>http-get:*:");
    http_response_free(&resp);

    ASSERT_INT_EQ(soap(port, "Search", "", &resp), 708);
    ASSERT_STR_CONTAINS(resp.body, "<errorCode>708</errorCode>");
    http_response_free(&resp);

    ASSERT_INT_EQ(soap(port, "Frobnicate", "", &resp), 401);
    http_response_free(&resp);

    /* POST without any SOAPAction header */
    ASSERT_INT_EQ(request(port, "POST", "/ctl/ContentDir", "Content-Length: 0\r\n", NULL), 401);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- DLNA request headers ----------------------------------------------- */

TEST(dlna_request_headers_are_validated)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", 100);
    ftest_mkfile_pattern(root, "photo.jpg", 100);
    START(root);

    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "TimeSeekRange.dlna.org: npt=0-\r\n", NULL), 406);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "PlaySpeed.dlna.org: speed=2\r\n", NULL), 406);
    /* with a Range, TimeSeekRange is tolerated */
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "TimeSeekRange.dlna.org: npt=0-\r\nRange: bytes=0-9\r\n", NULL), 206);

    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/photo.jpg",
                          "transferMode.dlna.org: Streaming\r\n", NULL), 406);
    /* Interactive is the image transfer mode: refused for a video, accepted
     * for a photo (this check used to be inverted) */
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "transferMode.dlna.org: Interactive\r\n", NULL), 406);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/photo.jpg",
                          "transferMode.dlna.org: Interactive\r\n", NULL), 200);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/photo.jpg",
                          "transferMode.dlna.org: Interactive\r\nrealTimeInfo.dlna.org: DLNA.ORG_TLAG=*\r\n",
                          NULL), 400);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "getcontentFeatures.dlna.org: 0\r\n", NULL), 400);

    struct http_response resp;
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4",
                          "transferMode.dlna.org: Streaming\r\ngetcontentFeatures.dlna.org: 1\r\n",
                          &resp), 200);
    ASSERT_STR_CONTAINS(resp.headers, "transferMode.dlna.org: Streaming\r\n");
    ASSERT_STR_CONTAINS(resp.headers, "contentFeatures.dlna.org: DLNA.ORG_OP=01;");
    http_response_free(&resp);

    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/photo.jpg", NULL, &resp), 200);
    ASSERT_STR_CONTAINS(resp.headers, "transferMode.dlna.org: Interactive\r\n");
    http_response_free(&resp);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- subtitles ---------------------------------------------------------- */

TEST(caption_info_points_to_the_matching_srt_file)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "movie.mp4", 100);
    ftest_mkfile_pattern(root, "movie.srt", 20);
    ftest_mkfile_pattern(root, "other.mp4", 100);
    START(root);

    struct http_response resp;
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4", "getCaptionInfo.sec: 1\r\n", &resp), 200);
    char expected[128];
    snprintf(expected, sizeof(expected),
             "CaptionInfo.sec: http://127.0.0.1:%d/MediaItems/movie.srt\r\n", port);
    ASSERT_STR_CONTAINS(resp.headers, expected);
    http_response_free(&resp);

    /* the advertised subtitle file is really downloadable */
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.srt", NULL, &resp), 200);
    ASSERT_INT_EQ(resp.body_len, 20);
    http_response_free(&resp);

    /* no .srt next to it: no header */
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/other.mp4", "getCaptionInfo.sec: 1\r\n", &resp), 200);
    ASSERT_TRUE(strstr(resp.headers, "CaptionInfo.sec") == NULL);
    http_response_free(&resp);

    /* not asked for: no header either */
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/movie.mp4", NULL, &resp), 200);
    ASSERT_TRUE(strstr(resp.headers, "CaptionInfo.sec") == NULL);
    http_response_free(&resp);

    ASSERT_TRUE(mdlna_stop(&proc));
}

/* ---- MediaItems error paths --------------------------------------------- */

TEST(media_items_error_paths)
{
    const char *root = ftest_mkdir();
    ftest_mkfile_pattern(root, "data.xyz", 10);
    ftest_mkpath(root, "folder.mp4");
    START(root);

    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/data.xyz", NULL, NULL), 406);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/folder.mp4", NULL, NULL), 403);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/missing.mp4", NULL, NULL), 404);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/../../etc/passwd", NULL, NULL), 406);
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/%2e%2e/%2e%2e/etc/passwd", NULL, NULL), 406);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(symlink_to_a_file_outside_media_dir_is_served)
{
    /* characterisation, not an endorsement: serve_file() resolves the path
     * with stat()/open(), which follow symlinks, so a link placed inside
     * media_dir exposes its target (see docs/dev/FUNCTIONAL_TESTS.md) */
    const char *root = ftest_mkdir();
    const char *outside = ftest_mkdir();
    ftest_mkfile_pattern(outside, "secret.mp4", 42);
    char target[4096], link_path[4096];
    snprintf(target, sizeof(target), "%s/secret.mp4", outside);
    snprintf(link_path, sizeof(link_path), "%s/linked.mp4", root);
    ASSERT_INT_EQ(symlink(target, link_path), 0);
    START(root);

    struct http_response resp;
    ASSERT_INT_EQ(request(port, "GET", "/MediaItems/linked.mp4", NULL, &resp), 200);
    ASSERT_INT_EQ(resp.body_len, 42);
    http_response_free(&resp);

    ASSERT_TRUE(mdlna_stop(&proc));
}
