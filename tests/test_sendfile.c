/* Tests for sendfile.c's send_file(): a socketpair() stands in for the
 * client socket, a memfd for the source file (both are real fds, so the
 * Linux sendfile(2) code path - not just the read/write fallback - is
 * actually exercised). sendfile.o is excluded from this binary's link. */

#include "../sendfile.c"

#include <sys/socket.h>

#include "test.h"
#include "helpers.h"

/* Reads from fd until EOF, returning a malloc'd buffer of exactly *out_len
 * bytes. Caller must free() it. */
static char *read_all(int fd, size_t *out_len)
{
    size_t cap = 4096;
    char *buf = malloc(cap);
    size_t len = 0;

    for (;;)
    {
        if (len == cap)
        {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        ssize_t n = read(fd, buf + len, cap - len);
        if (n < 0)
            abort();
        if (n == 0)
            break;
        len += (size_t)n;
    }

    *out_len = len;
    return buf;
}

TEST(send_file_from_offset_zero)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int src = fd_from_bytes("Hello, World!", 13);

    send_file(sv[0], src, 0, 12);   /* end_offset is inclusive */
    close(sv[0]);

    size_t got_len;
    char *got = read_all(sv[1], &got_len);
    ASSERT_INT_EQ((long long)got_len, 13);
    ASSERT_MEM_EQ(got, "Hello, World!", 13);

    free(got);
    close(sv[1]);
    close(src);
}

TEST(send_file_with_nonzero_offset)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int src = fd_from_bytes("0123456789", 10);

    send_file(sv[0], src, 3, 6);    /* bytes '3','4','5','6' */
    close(sv[0]);

    size_t got_len;
    char *got = read_all(sv[1], &got_len);
    ASSERT_INT_EQ((long long)got_len, 4);
    ASSERT_MEM_EQ(got, "3456", 4);

    free(got);
    close(sv[1]);
    close(src);
}

TEST(send_file_single_byte)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int src = fd_from_bytes("ABCDE", 5);

    send_file(sv[0], src, 2, 2);    /* just 'C' */
    close(sv[0]);

    size_t got_len;
    char *got = read_all(sv[1], &got_len);
    ASSERT_INT_EQ((long long)got_len, 1);
    ASSERT_MEM_EQ(got, "C", 1);

    free(got);
    close(sv[1]);
    close(src);
}

TEST(send_file_exact_byte_count_for_larger_payload)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    char *payload = malloc(200000);
    memset(payload, 'x', 200000);
    int src = fd_from_bytes(payload, 200000);
    free(payload);

    send_file(sv[0], src, 0, 199999);
    close(sv[0]);

    size_t got_len;
    char *got = read_all(sv[1], &got_len);
    ASSERT_INT_EQ((long long)got_len, 200000);

    free(got);
    close(sv[1]);
    close(src);
}
