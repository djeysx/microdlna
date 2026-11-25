/* Tests for xmlregex.c (the hand-rolled SOAP body "parser": a regex-like
 * name/value scan, not real XML parsing). xmlregex.o is excluded from this
 * binary's link.
 *
 * process_post_content() reads from h->fd via read(2), so each test feeds
 * it a fake fd built from a byte buffer (fd_from_bytes / helpers.h).
 */

#include "../xmlregex.c"

#include <stdio.h>

#include "test.h"
#include "helpers.h"

static struct upnphttp new_request(const char *body)
{
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.fd = fd_from_bytes(body, strlen(body));
    h.data_len = (int)strlen(body);
    return h;
}

/* Builds the HTTP chunked-transfer-coding wire format for exactly two data
 * chunks, followed by the standard "0\r\n\r\n" terminator. */
static struct upnphttp new_chunked_request(const char *chunk1, const char *chunk2)
{
    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "%x\r\n%s\r\n%x\r\n%s\r\n0\r\n\r\n",
                      (unsigned)strlen(chunk1), chunk1,
                      (unsigned)strlen(chunk2), chunk2);

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.fd = fd_from_bytes(buf, (size_t)n);
    h.reqflags = FLAG_CHUNKED;
    return h;
}

TEST(browse_body_sets_object_id_and_counts)
{
    struct upnphttp h = new_request(
        "<ObjectID>0</ObjectID>"
        "<StartingIndex>5</StartingIndex>"
        "<RequestedCount>10</RequestedCount>");

    process_post_content(&h);

    ASSERT_STR_EQ(h.remote_dirpath, "0");
    ASSERT_INT_EQ(h.starting_index, 5);
    ASSERT_INT_EQ(h.requested_count, 10);
    free(h.remote_dirpath);
}

TEST(container_id_is_an_alias_for_object_id)
{
    struct upnphttp h = new_request("<ContainerID>some/path</ContainerID>");

    process_post_content(&h);

    ASSERT_STR_EQ(h.remote_dirpath, "some/path");
    free(h.remote_dirpath);
}

TEST(element_attributes_are_ignored)
{
    struct upnphttp h = new_request("<ObjectID foo=\"bar\" baz='qux'>5</ObjectID>");

    process_post_content(&h);

    ASSERT_STR_EQ(h.remote_dirpath, "5");
    free(h.remote_dirpath);
}

TEST(element_value_is_trimmed)
{
    struct upnphttp h = new_request("<ObjectID>  abc  </ObjectID>");

    process_post_content(&h);

    ASSERT_STR_EQ(h.remote_dirpath, "abc");
    free(h.remote_dirpath);
}

TEST(chunked_body_with_tag_split_across_two_chunks)
{
    /* "<Obj" | "ectID>7</ObjectID>" - the opening tag's name is split right
     * across the chunk boundary. */
    struct upnphttp h = new_chunked_request("<Obj", "ectID>7</ObjectID>");

    process_post_content(&h);

    ASSERT_STR_EQ(h.remote_dirpath, "7");
    free(h.remote_dirpath);
}

TEST(malformed_chunk_size_yields_no_fields_and_no_crash)
{
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    const char body[] = "ZZ\r\nwhatever";
    h.fd = fd_from_bytes(body, sizeof(body) - 1);
    h.reqflags = FLAG_CHUNKED;

    process_post_content(&h);

    ASSERT_NULL(h.remote_dirpath);
}

TEST(chunk_size_over_2048_is_rejected)
{
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    /* 0x801 = 2049, one over the xmlregex.c limit */
    const char body[] = "801\r\n<ObjectID>5</ObjectID>";
    h.fd = fd_from_bytes(body, sizeof(body) - 1);
    h.reqflags = FLAG_CHUNKED;

    process_post_content(&h);

    ASSERT_NULL(h.remote_dirpath);
}

TEST(oversized_value_aborts_parsing_of_remaining_elements)
{
    /* ELE_VALUE_SIZE (xmlregex.c) is BUF_SIZE+1 = 1025; a value that long
     * makes the whole parse loop give up, so a well-formed element placed
     * right after it is never reached either (pins this current
     * behaviour: it's a parse abort, not just "this one element skipped"). */
    char body[1025 + 64];
    int n = snprintf(body, sizeof(body), "<Foo>");
    memset(body + n, 'a', 1030);
    n += 1030;
    n += snprintf(body + n, sizeof(body) - (size_t)n,
                  "</Foo><StartingIndex>5</StartingIndex>");

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.fd = fd_from_bytes(body, (size_t)n);

    process_post_content(&h);

    ASSERT_INT_EQ(h.starting_index, 0);   /* never reached */
}

TEST(element_name_of_20_chars_or_more_is_skipped_but_parsing_resumes)
{
    /* ELE_NAME_SIZE is 20; a name that long is rejected, but the scanner
     * recovers and keeps parsing the elements that follow. */
    struct upnphttp h = new_request(
        "<AAAAAAAAAAAAAAAAAAAA>x</AAAAAAAAAAAAAAAAAAAA>"
        "<StartingIndex>5</StartingIndex>");

    process_post_content(&h);

    ASSERT_NULL(h.remote_dirpath);
    ASSERT_INT_EQ(h.starting_index, 5);
}

TEST(starting_index_zero_or_negative_is_ignored)
{
    struct upnphttp h = new_request("<StartingIndex>0</StartingIndex>");
    h.starting_index = 99;   /* sentinel: proves the assignment is skipped */

    process_post_content(&h);

    ASSERT_INT_EQ(h.starting_index, 99);
}

TEST(namespaced_element_name_is_not_recognised)
{
    /* ':' is not accepted in element names (xmlregex.c only accepts
     * letters), so a namespaced tag like <u:ObjectID> is skipped entirely
     * rather than being matched as "ObjectID". Pins current behaviour. */
    struct upnphttp h = new_request("<u:ObjectID>5</u:ObjectID>");

    process_post_content(&h);

    ASSERT_NULL(h.remote_dirpath);
}
