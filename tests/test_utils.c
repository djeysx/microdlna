/* Tests for utils.c. #include's the module directly to also reach its
 * static helpers (needs_escaping, convert_hex); utils.o is excluded from
 * this binary's link (see Makefile / docs/dev/TESTS.md). */

#include "../utils.c"

#include "test.h"

TEST(url_escape_returns_input_when_nothing_to_escape)
{
    const char *in = "plain-file_09.mp4~";
    const char *out = url_escape(in);
    ASSERT_PTR_EQ(out, in);
}

TEST(url_escape_percent_encodes_special_chars)
{
    /* '/' and '.' are in the unescaped range ('-'..'9'), only the space is
     * escaped here, see the allowed-chars comment above needs_escaping(). */
    const char *out = url_escape("a b/c");
    ASSERT_STR_EQ(out, "a%20b/c");
    free((void *)out);
}

TEST(sanitise_path_collapses_dot_dot)
{
    char path[] = "a/b/../c";
    int ok = sanitise_path(path);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(path, "a/c");
}

TEST(sanitise_path_rejects_escape_above_root)
{
    char path[] = "../a";
    int ok = sanitise_path(path);
    ASSERT_TRUE(!ok);
}
