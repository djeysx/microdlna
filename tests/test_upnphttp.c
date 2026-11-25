/* Tests for upnphttp.c's request parsing: the Host header check (anti DNS
 * rebinding), other parse_http_header() cases, get_srt_path(), and
 * readline(). upnphttp.o is excluded from this binary's link.
 *
 * parse_http_header()/readline() are internal (static) helpers, reached
 * only via "#include ../upnphttp.c" (see docs/dev/TESTS.md).
 */

#include "../upnphttp.c"

#include "test.h"
#include "helpers.h"

static struct upnphttp new_h(void)
{
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.iface = -1;    /* get_interface_ip_str(-1) == "127.0.0.1" */
    return h;
}

/* ---- Host header: anti DNS-rebinding check (upnphttp.c:224-252) -------
 * get_interface_ip_str(-1) is "127.0.0.1" and the stubbed listening_port
 * (tests/support/stubs.c) is 2800, so the expected Host value is
 * "127.0.0.1:2800" unless a test overrides listening_port itself. */

TEST(host_header_exact_match_is_accepted)
{
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1:2800";

    parse_http_header(&h, "Host", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_HOST);
}

TEST(host_header_rejects_prefix_match)
{
    /* regression test: the Host check must compare the exact value, not
     * just a prefix (README "Modifications": this was the actual bug). */
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1:2800.evil.com";

    parse_http_header(&h, "Host", value, (int)strlen(value));

    ASSERT_TRUE(!(h.reqflags & FLAG_HOST));
}

TEST(host_header_rejects_wrong_port)
{
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1:9999";

    parse_http_header(&h, "Host", value, (int)strlen(value));

    ASSERT_TRUE(!(h.reqflags & FLAG_HOST));
}

TEST(host_header_port_80_has_no_port_suffix)
{
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1";
    int saved = listening_port;
    listening_port = 80;

    parse_http_header(&h, "Host", value, (int)strlen(value));

    listening_port = saved;
    ASSERT_TRUE(h.reqflags & FLAG_HOST);
}

TEST(host_header_accepts_trailing_whitespace_after_exact_match)
{
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1:2800 ";   /* trailing space included in len */

    parse_http_header(&h, "Host", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_HOST);
}

TEST(host_header_accepts_trailing_tab_or_cr)
{
    struct upnphttp h1 = new_h();
    char v1[] = "127.0.0.1:2800\t";
    parse_http_header(&h1, "Host", v1, (int)strlen(v1));
    ASSERT_TRUE(h1.reqflags & FLAG_HOST);

    struct upnphttp h2 = new_h();
    char v2[] = "127.0.0.1:2800\r";
    parse_http_header(&h2, "Host", v2, (int)strlen(v2));
    ASSERT_TRUE(h2.reqflags & FLAG_HOST);
}

TEST(host_header_name_is_case_insensitive)
{
    struct upnphttp h = new_h();
    char value[] = "127.0.0.1:2800";

    parse_http_header(&h, "HOST", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_HOST);
}

/* ---- other headers ----------------------------------------------------- */

TEST(content_length_negative_is_reset_to_zero)
{
    struct upnphttp h = new_h();
    char value[] = "-5";

    parse_http_header(&h, "Content-Length", value, (int)strlen(value));

    ASSERT_INT_EQ(h.data_len, 0);
}

TEST(range_header_start_and_end)
{
    struct upnphttp h = new_h();
    char value[] = "bytes=100-200";

    parse_http_header(&h, "Range", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_RANGE);
    ASSERT_INT_EQ(h.req_range_start, 100);
    ASSERT_INT_EQ(h.req_range_end, 200);
}

TEST(range_header_open_ended)
{
    struct upnphttp h = new_h();
    char value[] = "bytes=100-";

    parse_http_header(&h, "Range", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_RANGE);
    ASSERT_INT_EQ(h.req_range_start, 100);
    ASSERT_INT_EQ(h.req_range_end, 0);
}

TEST(range_header_suffix_form_is_a_negative_start)
{
    struct upnphttp h = new_h();
    char value[] = "bytes=-500";

    parse_http_header(&h, "Range", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_RANGE);
    ASSERT_INT_EQ(h.req_range_start, -500);
}

TEST(range_header_without_bytes_prefix_is_ignored)
{
    struct upnphttp h = new_h();
    char value[] = "100-200";

    parse_http_header(&h, "Range", value, (int)strlen(value));

    ASSERT_TRUE(!(h.reqflags & FLAG_RANGE));
}

TEST(timeout_header_parses_seconds)
{
    struct upnphttp h = new_h();
    char value[] = "Second-300";

    parse_http_header(&h, "Timeout", value, (int)strlen(value));

    ASSERT_INT_EQ(h.req_timeout, 300);
}

TEST(soap_action_browse)
{
    struct upnphttp h = new_h();
    char value[] = "urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"";

    parse_http_header(&h, "SOAPAction", value, (int)strlen(value));

    ASSERT_TRUE(h.req_soap_action == browse_content_directory);
}

TEST(soap_action_search_is_unsupported)
{
    struct upnphttp h = new_h();
    char value[] = "urn:schemas-upnp-org:service:ContentDirectory:1#Search\"";

    parse_http_header(&h, "SOAPAction", value, (int)strlen(value));

    ASSERT_TRUE(h.req_soap_action == unsupported_soap_action);
}

TEST(soap_action_unknown_is_invalid)
{
    struct upnphttp h = new_h();
    char value[] = "urn:schemas-upnp-org:service:ContentDirectory:1#Frobnicate\"";

    parse_http_header(&h, "SOAPAction", value, (int)strlen(value));

    ASSERT_TRUE(h.req_soap_action == invalid_soap_action);
}

TEST(soap_action_get_protocol_info)
{
    struct upnphttp h = new_h();
    char value[] = "urn:schemas-upnp-org:service:ConnectionManager:1#GetProtocolInfo\"";

    parse_http_header(&h, "SOAPAction", value, (int)strlen(value));

    ASSERT_TRUE(h.req_soap_action == get_protocol_info);
}

TEST(callback_header_strips_angle_brackets)
{
    struct upnphttp h = new_h();
    char value[] = "<http://x/y>";

    parse_http_header(&h, "Callback", value, (int)strlen(value));

    ASSERT_STR_EQ(h.req_callback, "http://x/y");
    free(h.req_callback);
}

TEST(transfer_encoding_chunked_is_case_insensitive)
{
    struct upnphttp h = new_h();
    char value[] = "CHUNKED";

    parse_http_header(&h, "Transfer-Encoding", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_CHUNKED);
}

TEST(transfer_mode_streaming)
{
    struct upnphttp h = new_h();
    char value[] = "Streaming";

    parse_http_header(&h, "transferMode.dlna.org", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_XFERSTREAMING);
}

TEST(transfer_mode_interactive)
{
    struct upnphttp h = new_h();
    char value[] = "Interactive";

    parse_http_header(&h, "transferMode.dlna.org", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_XFERINTERACTIVE);
}

TEST(get_content_features_other_than_1_is_invalid)
{
    struct upnphttp h = new_h();
    char value[] = "0";

    parse_http_header(&h, "getcontentFeatures.dlna.org", value, (int)strlen(value));

    ASSERT_TRUE(h.reqflags & FLAG_INVALID_REQ);
}

TEST(get_content_features_1_is_valid)
{
    struct upnphttp h = new_h();
    char value[] = "1";

    parse_http_header(&h, "getcontentFeatures.dlna.org", value, (int)strlen(value));

    ASSERT_TRUE(!(h.reqflags & FLAG_INVALID_REQ));
}

/* ---- get_srt_path -------------------------------------------------------
 * Bounded to the last 7 bytes of the path (upnphttp.c:689-706); unlike
 * mime.c's get_mime_type() this never looks before the start of the
 * string, so short inputs are safe here. */

TEST(get_srt_path_replaces_extension)
{
    char *p = get_srt_path("a/b/film.mkv");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "a/b/film.srt");
    free(p);
}

TEST(get_srt_path_no_extension_returns_null)
{
    ASSERT_NULL(get_srt_path("noext"));
}

TEST(get_srt_path_slash_before_dot_returns_null)
{
    /* the dot belongs to a parent directory name, not the filename */
    ASSERT_NULL(get_srt_path("dir.d/file"));
}

TEST(get_srt_path_extension_longer_than_window_returns_null)
{
    ASSERT_NULL(get_srt_path("movie.toolongext"));
}

/* ---- readline ------------------------------------------------------------
 * Pins the current contract (no distinction between "clean EOF" and other
 * failures): the upcoming keep-alive refactor
 * (docs/dev/client_fast_seek_Analyse.md §1a) is expected to change this. */

TEST(readline_reads_up_to_crlf)
{
    char out[16];
    int fd = fd_from_bytes("abc\r\nrest", 9);

    int len = readline(fd, out, sizeof(out));

    ASSERT_INT_EQ(len, 3);
    ASSERT_STR_EQ(out, "abc");
}

TEST(readline_cr_without_lf_is_an_error)
{
    char out[16];
    int fd = fd_from_bytes("abc\rX", 5);

    int len = readline(fd, out, sizeof(out));

    ASSERT_INT_EQ(len, -1);
}

TEST(readline_eof_before_crlf_is_an_error)
{
    char out[16];
    int fd = fd_from_bytes("abc", 3);

    int len = readline(fd, out, sizeof(out));

    ASSERT_INT_EQ(len, -1);
}

TEST(readline_limit_reached_is_an_error)
{
    char out[16];
    int fd = fd_from_bytes("abcdef\r\n", 8);

    int len = readline(fd, out, 3);

    ASSERT_INT_EQ(len, -1);
}

TEST(readline_empty_line_is_zero)
{
    char out[16];
    int fd = fd_from_bytes("\r\n", 2);

    int len = readline(fd, out, sizeof(out));

    ASSERT_INT_EQ(len, 0);
}
