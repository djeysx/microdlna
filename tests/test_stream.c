/* Tests for stream.c (HTTP chunked-encoding helpers). stream.o is excluded
 * from this binary's link; output is inspected via capture_open(), a
 * struct stream backed by open_memstream() instead of a real socket. */

#include "../stream.c"

#include "test.h"
#include "helpers.h"

TEST(chunk_print_then_end_produces_one_chunk)
{
    struct capture *c = capture_open();

    chunk_print(&c->st, "hello");
    chunk_print_end(&c->st);

    ASSERT_STR_EQ(capture_text(c), "5\r\nhello\r\n0\r\n\r\n");
    capture_close(c);
}

TEST(chunk_print_end_alone_sends_terminator_only)
{
    struct capture *c = capture_open();

    chunk_print_end(&c->st);

    ASSERT_STR_EQ(capture_text(c), "0\r\n\r\n");
    capture_close(c);
}

/* BUFFER_SIZE is 1024 (stream.h). chunk_print only flushes mid-write once
 * s->pos has reached BUFFER_SIZE *before* writing the next byte, so writing
 * exactly 1023 or 1024 bytes never triggers an internal flush: everything
 * goes out as a single chunk once chunk_print_end() flushes it. */
TEST(chunk_print_1023_bytes_is_a_single_chunk)
{
    struct capture *c = capture_open();
    char buf[1024];

    memset(buf, 'x', 1023);
    buf[1023] = '\0';

    chunk_print(&c->st, buf);
    chunk_print_end(&c->st);

    const char *out = capture_text(c);
    ASSERT_STR_CONTAINS(out, "3FF\r\nxxx");    /* 1023 = 0x3FF, %X is uppercase */
    /* total length: "3FF\r\n" (5) + 1023 x's + "\r\n" (2) + "0\r\n\r\n" (5) */
    ASSERT_INT_EQ((long long)strlen(out), 5 + 1023 + 2 + 5);
    capture_close(c);
}

TEST(chunk_print_1024_bytes_is_a_single_chunk)
{
    struct capture *c = capture_open();
    char buf[1025];

    memset(buf, 'x', 1024);
    buf[1024] = '\0';

    chunk_print(&c->st, buf);
    chunk_print_end(&c->st);

    const char *out = capture_text(c);
    ASSERT_STR_CONTAINS(out, "400\r\nxxx");    /* 1024 = 0x400 */
    /* total length: "400\r\n" (5) + 1024 x's + "\r\n" (2) + "0\r\n\r\n" (5) */
    ASSERT_INT_EQ((long long)strlen(out), 5 + 1024 + 2 + 5);
    capture_close(c);
}

TEST(chunk_print_1025_bytes_splits_into_two_chunks)
{
    struct capture *c = capture_open();
    char buf[1026];

    memset(buf, 'x', 1025);
    buf[1025] = '\0';

    chunk_print(&c->st, buf);
    chunk_print_end(&c->st);

    const char *out = capture_text(c);
    /* first chunk: 1024 bytes (0x400), flushed mid-write */
    ASSERT_STR_CONTAINS(out, "400\r\n");
    /* second chunk: the 1 remaining byte */
    ASSERT_STR_CONTAINS(out, "\r\n1\r\nx\r\n0\r\n\r\n");
    /* "400\r\n"(5) + 1024 x's + "\r\n"(2) + "1\r\n"(3) + "x"(1) + "\r\n"(2) + "0\r\n\r\n"(5) */
    ASSERT_INT_EQ((long long)strlen(out), 5 + 1024 + 2 + 3 + 1 + 2 + 5);
    capture_close(c);
}

TEST(chunk_printf_flushes_pending_buffer_before_its_own_chunk)
{
    struct capture *c = capture_open();

    chunk_print(&c->st, "ab");
    chunk_printf(&c->st, "%d", 42);
    chunk_print_end(&c->st);

    /* "ab" (pending, pos=2) must be flushed as its own chunk before the
     * chunk_printf's "42", not merged with it (stream.c:66) */
    ASSERT_STR_EQ(capture_text(c), "2\r\nab\r\n2\r\n42\r\n0\r\n\r\n");
    capture_close(c);
}

TEST(chunk_print_len_truncates_at_len)
{
    struct capture *c = capture_open();

    chunk_print_len(&c->st, "hello world", 5);
    chunk_print_end(&c->st);

    ASSERT_STR_EQ(capture_text(c), "5\r\nhello\r\n0\r\n\r\n");
    capture_close(c);
}

TEST(chunk_print_len_stops_at_nul_before_len_exhausted)
{
    struct capture *c = capture_open();

    chunk_print_len(&c->st, "hi", 10);
    chunk_print_end(&c->st);

    ASSERT_STR_EQ(capture_text(c), "2\r\nhi\r\n0\r\n\r\n");
    capture_close(c);
}

TEST(chunk_print_all_macro_concatenates_before_flushing)
{
    struct capture *c = capture_open();

    CHUNK_PRINT_ALL(&c->st, "a", "b", "c");
    chunk_print_end(&c->st);

    ASSERT_STR_EQ(capture_text(c), "3\r\nabc\r\n0\r\n\r\n");
    capture_close(c);
}

TEST(stream_printf_bypasses_chunk_framing)
{
    struct capture *c = capture_open();

    stream_printf(&c->st, "HTTP/1.1 %d OK\r\n", 200);

    ASSERT_STR_EQ(capture_text(c), "HTTP/1.1 200 OK\r\n");
    capture_close(c);
}

TEST(stream_write_bypasses_chunk_framing)
{
    struct capture *c = capture_open();

    stream_write("raw", 3, &c->st);

    ASSERT_STR_EQ(capture_text(c), "raw");
    capture_close(c);
}

TEST(stream_flush_sends_pending_chunk_without_terminator)
{
    struct capture *c = capture_open();

    chunk_print(&c->st, "q");
    stream_flush(&c->st);

    ASSERT_STR_EQ(capture_text(c), "1\r\nq\r\n");
    capture_close(c);
}
