/* UPnP eventing (GENA) end to end: SUBSCRIBE / renew / UNSUBSCRIBE against
 * the real server, with this test playing the control point's callback
 * server (support/eventsink.c) to receive the initial NOTIFY. The NOTIFY
 * state machine in upnpevents.c is driven by the main select() loop, so
 * these tests also cover its fd handling: a callback that refuses or
 * never answers must not stall the server. */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "../support/test.h"
#include "support/eventsink.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"

/* Copies the value of header `name` (case-insensitive) from a raw header
 * block into out; returns 0 if found. */
static int header_value(const char *headers, const char *name, char *out, size_t cap)
{
    size_t name_len = strlen(name);
    for (const char *p = headers; p && *p; )
    {
        if (strncasecmp(p, name, name_len) == 0 && p[name_len] == ':')
        {
            const char *v = p + name_len + 1;
            while (*v == ' ')
                v++;
            size_t n = strcspn(v, "\r\n");
            if (n >= cap)
                n = cap - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
        p = strstr(p, "\r\n");
        if (p)
            p += 2;
    }
    return -1;
}

static int gena_request(int port, const char *method, const char *path,
                        const char *extra_headers, struct http_response *resp)
{
    char req[1024];
    snprintf(req, sizeof(req),
             "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n%s\r\n",
             method, path, port, extra_headers);
    return http_do_request(port, req, strlen(req), resp);
}

/* SUBSCRIBE to /evt/ContentDir with a callback on sink_port; on success
 * stores the SID and returns the HTTP status. */
static int subscribe(int port, int sink_port, int timeout_s, char *sid, size_t sid_cap)
{
    char headers[256];
    snprintf(headers, sizeof(headers),
             "Callback: <http://127.0.0.1:%d/cb>\r\nNT: upnp:event\r\nTimeout: Second-%d\r\n",
             sink_port, timeout_s);

    struct http_response resp;
    if (gena_request(port, "SUBSCRIBE", "/evt/ContentDir", headers, &resp) != 0)
        return -1;

    int status = resp.status;
    if (sid && header_value(resp.headers, "SID", sid, sid_cap) != 0)
        sid[0] = '\0';
    http_response_free(&resp);
    return status;
}

static int sid_request(int port, const char *method, const char *sid)
{
    char headers[128];
    snprintf(headers, sizeof(headers), "SID: %s\r\n", sid);

    struct http_response resp;
    if (gena_request(port, method, "/evt/ContentDir", headers, &resp) != 0)
        return -1;
    int status = resp.status;
    http_response_free(&resp);
    return status;
}

static int get_rootdesc_status(int port)
{
    struct http_response resp;
    if (gena_request(port, "GET", "/rootDesc.xml", "Connection: close\r\n", &resp) != 0)
        return -1;
    int status = resp.status;
    http_response_free(&resp);
    return status;
}

TEST(subscribe_returns_sid_and_initial_notify_reaches_callback)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int sink_port;
    int sink = eventsink_open(&sink_port);

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char headers[256];
    snprintf(headers, sizeof(headers),
             "Callback: <http://127.0.0.1:%d/cb>\r\nNT: upnp:event\r\nTimeout: Second-300\r\n",
             sink_port);
    struct http_response resp;
    char sid[64] = "";
    if (gena_request(port, "SUBSCRIBE", "/evt/ContentDir", headers, &resp) == 0)
    {
        ASSERT_INT_EQ(resp.status, 200);
        ASSERT_INT_EQ(header_value(resp.headers, "SID", sid, sizeof(sid)), 0);
        ASSERT_TRUE(strncmp(sid, "uuid:", 5) == 0);
        ASSERT_STR_CONTAINS(resp.headers, "Timeout: Second-300");
        http_response_free(&resp);
    }
    else
    {
        tests_fail(__FILE__, __LINE__, "SUBSCRIBE failed");
    }

    struct http_response notify;
    if (eventsink_accept(sink, 3000, 1, &notify) == 0)
    {
        ASSERT_TRUE(strncmp(notify.headers, "NOTIFY /cb HTTP/1.1\r\n", 21) == 0);
        char value[64];
        ASSERT_INT_EQ(header_value(notify.headers, "SID", value, sizeof(value)), 0);
        ASSERT_STR_EQ(value, sid);
        ASSERT_INT_EQ(header_value(notify.headers, "SEQ", value, sizeof(value)), 0);
        ASSERT_STR_EQ(value, "0");
        ASSERT_STR_CONTAINS(notify.headers, "NT: upnp:event\r\n");
        ASSERT_STR_CONTAINS(notify.headers, "NTS: upnp:propchange\r\n");
        ASSERT_STR_CONTAINS(notify.body, "<e:propertyset");
        ASSERT_STR_CONTAINS(notify.body, "<SystemUpdateID>0</SystemUpdateID>");
        http_response_free(&notify);
    }
    else
    {
        tests_fail(__FILE__, __LINE__, "no NOTIFY received on the callback");
    }

    close(sink);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(renew_and_unsubscribe_lifecycle)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int sink_port;
    int sink = eventsink_open(&sink_port);

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char sid[64] = "";
    ASSERT_INT_EQ(subscribe(port, sink_port, 300, sid, sizeof(sid)), 200);
    struct http_response notify;
    if (eventsink_accept(sink, 3000, 1, &notify) == 0)
        http_response_free(&notify);

    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", sid), 200);
    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", "uuid:00000000-0000-0000-0000-000000000000"), 412);
    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", "uuid:short"), 412);
    ASSERT_INT_EQ(sid_request(port, "UNSUBSCRIBE", sid), 200);
    ASSERT_INT_EQ(sid_request(port, "UNSUBSCRIBE", sid), 412);
    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", sid), 412);

    close(sink);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(invalid_subscribe_requests_are_rejected)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    struct
    {
        const char *path;
        const char *headers;
        int status;
    } cases[] = {
        { "/evt/ContentDir", "Callback: <http://127.0.0.1:1/cb>\r\nNT: upnp:event\r\nSID: uuid:x\r\n", 400 },
        { "/evt/ContentDir", "Callback: <ftp://127.0.0.1:1/cb>\r\nNT: upnp:event\r\n", 412 },
        { "/evt/ContentDir", "Callback: <http://127.0.0.1:1/cb>\r\nNT: upnp:other\r\n", 412 },
        { "/evt/ContentDir", "", 412 },
        /* used to answer 200 without a SID, subscribing nothing */
        { "/evt/Nope", "Callback: <http://127.0.0.1:1/cb>\r\nNT: upnp:event\r\n", 412 },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        struct http_response resp;
        if (gena_request(port, "SUBSCRIBE", cases[i].path, cases[i].headers, &resp) != 0)
        {
            tests_fail(__FILE__, __LINE__, "case %u: request failed", i);
            continue;
        }
        if (resp.status != cases[i].status)
            tests_fail(__FILE__, __LINE__, "case %u: expected %d, got %d",
                       i, cases[i].status, resp.status);
        http_response_free(&resp);
    }

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(expired_subscription_can_no_longer_be_renewed)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int sink_port;
    int sink = eventsink_open(&sink_port);

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char sid[64] = "";
    ASSERT_INT_EQ(subscribe(port, sink_port, 1, sid, sizeof(sid)), 200);
    /* answer the initial NOTIFY so the subscriber has no pending notify:
     * only then is it eligible for expiry (upnpevents_removed_timedout_subs) */
    struct http_response notify;
    if (eventsink_accept(sink, 3000, 1, &notify) == 0)
        http_response_free(&notify);

    struct timespec wait = { .tv_sec = 2, .tv_nsec = 200 * 1000 * 1000 };
    nanosleep(&wait, NULL);

    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", sid), 412);

    close(sink);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(callback_refusing_connections_does_not_affect_the_server)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int closed_port = ftest_alloc_port(); /* nothing listens there */

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    ASSERT_INT_EQ(subscribe(port, closed_port, 300, NULL, 0), 200);

    for (int i = 0; i < 3; i++)
        ASSERT_INT_EQ(get_rootdesc_status(port), 200);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(callback_that_never_answers_does_not_stall_the_server)
{
    /* The sink listens (so the TCP connect succeeds and the NOTIFY is
     * written into the kernel buffer) but never accepts or replies: the
     * notify object stays in EWaitingForResponse. The non-blocking state
     * machine must keep serving HTTP meanwhile, and shutdown must still be
     * clean with the notify pending. */
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int sink_port;
    int sink = eventsink_open(&sink_port);

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    ASSERT_INT_EQ(subscribe(port, sink_port, 300, NULL, 0), 200);

    for (int i = 0; i < 3; i++)
        ASSERT_INT_EQ(get_rootdesc_status(port), 200);

    ASSERT_TRUE(mdlna_stop(&proc));
    close(sink);
}

TEST(callback_closing_without_answer_is_handled)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();
    int sink_port;
    int sink = eventsink_open(&sink_port);

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char sid[64] = "";
    ASSERT_INT_EQ(subscribe(port, sink_port, 300, sid, sizeof(sid)), 200);

    struct http_response notify;
    ASSERT_INT_EQ(eventsink_accept(sink, 3000, 0, &notify), 0);
    http_response_free(&notify);

    ASSERT_INT_EQ(get_rootdesc_status(port), 200);
    ASSERT_INT_EQ(sid_request(port, "SUBSCRIBE", sid), 200);

    close(sink);
    ASSERT_TRUE(mdlna_stop(&proc));
}
