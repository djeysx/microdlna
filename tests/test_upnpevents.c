/* Tests for upnpevents.c (GENA: SUBSCRIBE/RENEW/UNSUBSCRIBE bookkeeping and
 * the NOTIFY callback parsing). upnpevents.o is excluded from this binary's
 * link.
 *
 * The subscriber and notify lists are file-static globals: every test ends
 * with reset_lists() so the next one starts from empty lists. */

#include "../upnpevents.c"

#include "test.h"
#include "helpers.h"

static void reset_lists(void)
{
    upnpevents_clear_notify_list();
    clear_upnpevent_subscribers();
}

static int count_subscribers(void)
{
    int n = 0;
    for (struct subscriber *s = subscriberlist; s; s = s->next)
        n++;
    return n;
}

/* ---- add / renew / remove --------------------------------------------- */

TEST(add_subscriber_accepts_each_event_url)
{
    const char *urls[] = { CONTENTDIRECTORY_EVENTURL, CONNECTIONMGR_EVENTURL,
                           X_MS_MEDIARECEIVERREGISTRAR_EVENTURL };

    for (int i = 0; i < 3; i++)
    {
        const char *sid = add_upnpevent_subscriber(urls[i], "http://127.0.0.1:1/cb", 300);
        ASSERT_NOT_NULL(sid);
        if (sid)
        {
            ASSERT_INT_EQ(strlen(sid), 41);
            ASSERT_TRUE(strncmp(sid, "uuid:", 5) == 0);
        }
    }
    ASSERT_INT_EQ(count_subscribers(), 3);

    reset_lists();
}

TEST(add_subscriber_rejects_unknown_event_url_and_null_args)
{
    ASSERT_NULL(add_upnpevent_subscriber("/evt/Nope", "http://127.0.0.1:1/cb", 300));
    ASSERT_NULL(add_upnpevent_subscriber(NULL, "http://127.0.0.1:1/cb", 300));
    ASSERT_NULL(add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, NULL, 300));
    ASSERT_INT_EQ(count_subscribers(), 0);

    reset_lists();
}

TEST(renew_known_sid_updates_timeout_unknown_sid_fails)
{
    char sid[42];
    strxcpy(sid, add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL,
                                          "http://127.0.0.1:1/cb", 10), sizeof(sid));

    ASSERT_INT_EQ(renew_upnpevent_subscriber(sid, 0), 0);
    ASSERT_INT_EQ(subscriberlist->timeout, 0);

    ASSERT_INT_EQ(renew_upnpevent_subscriber(sid, 100), 0);
    ASSERT_TRUE(subscriberlist->timeout >= time(NULL) + 99);

    ASSERT_INT_EQ(renew_upnpevent_subscriber("uuid:00000000-0000-0000-0000-000000000000", 1), -1);

    reset_lists();
}

TEST(remove_subscriber_in_the_middle_of_the_list)
{
    char sids[3][42];
    for (int i = 0; i < 3; i++)
        strxcpy(sids[i], add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL,
                                                  "http://127.0.0.1:1/cb", 300), 42);

    /* the random 4-hex-digit suffix could collide: skip rather than flake */
    if (strcmp(sids[0], sids[1]) == 0 || strcmp(sids[1], sids[2]) == 0
        || strcmp(sids[0], sids[2]) == 0)
    {
        reset_lists();
        return;
    }

    ASSERT_INT_EQ(remove_upnpevent_subscriber(sids[1]), 0);
    ASSERT_INT_EQ(count_subscribers(), 2);
    ASSERT_INT_EQ(remove_upnpevent_subscriber(sids[1]), -1);
    ASSERT_INT_EQ(renew_upnpevent_subscriber(sids[0], 1), 0);
    ASSERT_INT_EQ(renew_upnpevent_subscriber(sids[2], 1), 0);

    reset_lists();
}

TEST(remove_subscriber_while_notify_in_flight_detaches_it)
{
    char sid[42];
    strxcpy(sid, add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL,
                                          "http://127.0.0.1:1/cb", 300), sizeof(sid));
    struct upnp_event_notify *obj = notifylist;
    ASSERT_NOT_NULL(obj);

    ASSERT_INT_EQ(remove_upnpevent_subscriber(sid), 0);
    if (obj)
        ASSERT_NULL(obj->sub);

    reset_lists();
}

TEST(short_sid_is_rejected_without_reading_past_its_end)
{
    /* renew/remove used to memcmp() 41 bytes of the client-supplied SID
     * regardless of its length: a heap over-read for any shorter value. */
    add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, "http://127.0.0.1:1/cb", 300);

    char *short_sid = strdup("uuid:1");
    ASSERT_INT_EQ(renew_upnpevent_subscriber(short_sid, 1), -1);
    ASSERT_INT_EQ(remove_upnpevent_subscriber(short_sid), -1);
    free(short_sid);

    char *empty = strdup("");
    ASSERT_INT_EQ(renew_upnpevent_subscriber(empty, 1), -1);
    ASSERT_INT_EQ(remove_upnpevent_subscriber(empty), -1);
    free(empty);

    reset_lists();
}

/* ---- timeouts ----------------------------------------------------------- */

TEST(expired_subscriber_without_pending_notify_is_removed)
{
    add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, "http://127.0.0.1:1/cb", 300);
    upnpevents_clear_notify_list();
    subscriberlist->notify = NULL;
    subscriberlist->timeout = time(NULL) - 1;

    upnpevents_removed_timedout_subs();

    ASSERT_INT_EQ(count_subscribers(), 0);
    reset_lists();
}

TEST(expired_subscriber_with_pending_notify_is_kept)
{
    add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, "http://127.0.0.1:1/cb", 300);
    subscriberlist->timeout = time(NULL) - 1;

    upnpevents_removed_timedout_subs();

    ASSERT_INT_EQ(count_subscribers(), 1);
    reset_lists();
}

TEST(zero_timeout_never_expires)
{
    add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, "http://127.0.0.1:1/cb", 0);
    upnpevents_clear_notify_list();
    subscriberlist->notify = NULL;

    upnpevents_removed_timedout_subs();

    ASSERT_INT_EQ(count_subscribers(), 1);
    reset_lists();
}

/* ---- check_event() / SUBSCRIBE / UNSUBSCRIBE ---------------------------- */

struct event_req
{
    struct upnphttp h;
    struct capture *c;
};

static void event_req_init(struct event_req *r, const char *path, const char *callback,
                           const char *nt, const char *sid)
{
    memset(&r->h, 0, sizeof(r->h));
    r->c = capture_open();
    r->h.st = &r->c->st;
    r->h.iface = -1;
    r->h.path = strdup(path);
    r->h.req_callback = callback ? strdup(callback) : NULL;
    r->h.req_nt = nt ? strdup(nt) : NULL;
    r->h.req_sid = sid ? strdup(sid) : NULL;
}

static void event_req_free(struct event_req *r)
{
    free(r->h.path);
    free(r->h.req_callback);
    free(r->h.req_nt);
    free(r->h.req_sid);
    capture_close(r->c);
}

static void check_event_status(const char *callback, const char *nt, const char *sid,
                               int expected_type, const char *expected_status)
{
    struct event_req r;
    event_req_init(&r, CONTENTDIRECTORY_EVENTURL, callback, nt, sid);

    ASSERT_INT_EQ(check_event(&r.h), expected_type);
    if (expected_status)
        ASSERT_STR_CONTAINS(capture_text(r.c), expected_status);

    event_req_free(&r);
}

TEST(check_event_matrix)
{
    check_event_status("http://127.0.0.1:1/cb", "upnp:event", NULL, E_SUBSCRIBE, NULL);
    check_event_status("http://127.0.0.1:1/cb", "upnp:event", "uuid:x", E_INVALID,
                       "HTTP/1.1 400");
    check_event_status("http://127.0.0.1:1/cb", NULL, NULL, E_INVALID, "HTTP/1.1 400");
    check_event_status("ftp://127.0.0.1:1/cb", "upnp:event", NULL, E_INVALID,
                       "HTTP/1.1 412");
    check_event_status("http://127.0.0.1:1/cb", "upnp:other", NULL, E_INVALID,
                       "HTTP/1.1 412");
    check_event_status(NULL, NULL, "uuid:x", E_RENEW, NULL);
    check_event_status(NULL, "upnp:event", "uuid:x", E_INVALID, "HTTP/1.1 400");
    check_event_status(NULL, NULL, NULL, E_INVALID, "HTTP/1.1 412");
}

TEST(subscribe_response_carries_sid_and_timeout_headers)
{
    /* send_http_response_helper() used to overwrite h->respflags with
     * FLAG_HTML, silently dropping the FLAG_SID|FLAG_TIMEOUT set by the
     * SUBSCRIBE handler: clients never received their SID. */
    struct event_req r;
    event_req_init(&r, CONTENTDIRECTORY_EVENTURL, "http://127.0.0.1:1/cb", "upnp:event", NULL);
    r.h.req_timeout = 120;

    process_http_subscribe_upnphttp(&r.h);

    const char *out = capture_text(r.c);
    ASSERT_STR_CONTAINS(out, "HTTP/1.1 200");
    ASSERT_STR_CONTAINS(out, "SID: uuid:");
    ASSERT_STR_CONTAINS(out, "Timeout: Second-120");
    ASSERT_INT_EQ(count_subscribers(), 1);

    event_req_free(&r);
    reset_lists();
}

TEST(subscribe_to_unknown_event_url_is_rejected)
{
    /* used to answer 200 (without a SID) even though nothing was
     * subscribed */
    struct event_req r;
    event_req_init(&r, "/evt/Nope", "http://127.0.0.1:1/cb", "upnp:event", NULL);

    process_http_subscribe_upnphttp(&r.h);

    ASSERT_STR_CONTAINS(capture_text(r.c), "HTTP/1.1 412");
    ASSERT_INT_EQ(count_subscribers(), 0);

    event_req_free(&r);
    reset_lists();
}

TEST(renew_response_enforces_dlna_five_minute_timeout)
{
    char sid[42];
    strxcpy(sid, add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL,
                                          "http://127.0.0.1:1/cb", 300), sizeof(sid));
    struct event_req r;
    event_req_init(&r, CONTENTDIRECTORY_EVENTURL, NULL, NULL, sid);
    r.h.req_timeout = 9999;

    process_http_subscribe_upnphttp(&r.h);

    const char *out = capture_text(r.c);
    ASSERT_STR_CONTAINS(out, "HTTP/1.1 200");
    ASSERT_STR_CONTAINS(out, "Timeout: Second-300");
    ASSERT_STR_CONTAINS(out, sid);

    event_req_free(&r);
    reset_lists();
}

TEST(unsubscribe_known_then_unknown_sid)
{
    char sid[42];
    strxcpy(sid, add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL,
                                          "http://127.0.0.1:1/cb", 300), sizeof(sid));

    struct event_req r;
    event_req_init(&r, CONTENTDIRECTORY_EVENTURL, NULL, NULL, sid);
    process_http_un_subscribe_upnphttp(&r.h);
    ASSERT_STR_CONTAINS(capture_text(r.c), "HTTP/1.1 200");
    event_req_free(&r);

    event_req_init(&r, CONTENTDIRECTORY_EVENTURL, NULL, NULL, sid);
    process_http_un_subscribe_upnphttp(&r.h);
    ASSERT_STR_CONTAINS(capture_text(r.c), "HTTP/1.1 412");
    event_req_free(&r);

    reset_lists();
}

/* ---- NOTIFY callback URL parsing (upnp_event_notify_connect) ----------- */

static struct upnp_event_notify *connect_with_callback(const char *callback)
{
    add_upnpevent_subscriber(CONTENTDIRECTORY_EVENTURL, callback, 300);
    struct upnp_event_notify *obj = notifylist;
    if (obj)
        upnp_event_notify_connect(obj);
    return obj;
}

TEST(notify_callback_with_explicit_port_and_path)
{
    struct upnp_event_notify *obj = connect_with_callback("http://127.0.0.1:1/some/path");
    ASSERT_NOT_NULL(obj);
    if (obj)
    {
        ASSERT_STR_EQ(obj->addrstr, "127.0.0.1");
        ASSERT_STR_EQ(obj->portstr, ":1");
        ASSERT_STR_EQ(obj->path, "/some/path");
    }
    reset_lists();
}

TEST(notify_callback_without_port_or_path)
{
    struct upnp_event_notify *obj = connect_with_callback("http://127.0.0.1");
    ASSERT_NOT_NULL(obj);
    if (obj)
    {
        ASSERT_STR_EQ(obj->addrstr, "127.0.0.1");
        ASSERT_STR_EQ(obj->portstr, "");
        ASSERT_STR_EQ(obj->path, "/");
    }
    reset_lists();
}

TEST(notify_callback_oversized_host_and_port_stay_in_bounds)
{
    struct upnp_event_notify *obj =
        connect_with_callback("http://1234567890123456789012345:123456789012/p");
    ASSERT_NOT_NULL(obj);
    if (obj)
    {
        ASSERT_TRUE(strlen(obj->addrstr) <= sizeof(obj->addrstr) - 1);
        ASSERT_TRUE(strlen(obj->portstr) <= sizeof(obj->portstr) - 1);
    }
    reset_lists();
}
