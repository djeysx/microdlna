/* Tests for mime.c. mime.o is excluded from this binary's link. */

#include "../mime.c"

#include "test.h"

TEST(get_mime_type_known_extension)
{
    const struct ext_info *m = get_mime_type("movie.mp4");
    ASSERT_NOT_NULL(m);
    ASSERT_INT_EQ(m->type, M_VIDEO);
    ASSERT_STR_EQ(m->sub_type, "mp4");
}

TEST(get_mime_type_is_case_insensitive)
{
    const struct ext_info *m = get_mime_type("MOVIE.MP4");
    ASSERT_NOT_NULL(m);
    ASSERT_INT_EQ(m->type, M_VIDEO);
}

TEST(get_mime_type_mkv)
{
    const struct ext_info *m = get_mime_type("movie.mkv");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->sub_type, "x-matroska");
}

TEST(get_mime_type_five_char_extension)
{
    /* longest extension in the table (ext[6] buffer, see mime.c:241) */
    const struct ext_info *m = get_mime_type("movie.dtshd");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->sub_type, "vnd.dts.hd");
}

TEST(get_mime_type_six_char_extension_not_found)
{
    /* a 6-char extension puts the dot outside the last-6-bytes lookup
     * window (mime.c:273-277), so it's never even seen; same observable
     * result (NULL) as an unknown extension, different cause. */
    ASSERT_NULL(get_mime_type("movie.abcdef"));
}

TEST(get_mime_type_unknown_extension)
{
    ASSERT_NULL(get_mime_type("movie.zzz"));
}

TEST(get_mime_type_no_dot_returns_null)
{
    ASSERT_NULL(get_mime_type("noextension"));
}

TEST(get_mime_type_short_filename_without_dot_returns_null)
{
    /* get_mime_type() only looks at the last 6 bytes of the filename
     * (mime.c), clamped to the string's actual length - these used to be
     * out-of-bounds reads for any "no dot" filename shorter than 6 chars. */
    ASSERT_NULL(get_mime_type("abcde"));
    ASSERT_NULL(get_mime_type("a"));
    ASSERT_NULL(get_mime_type(""));
}

TEST(get_mime_type_hidden_file_with_extension)
{
    const struct ext_info *m = get_mime_type(".movie.mp4");
    ASSERT_NOT_NULL(m);
    ASSERT_INT_EQ(m->type, M_VIDEO);
}

TEST(get_mime_type_dot_in_directory_component_is_not_an_extension)
{
    /* the loop only looks at the last 6 bytes of the whole string, so a dot
     * followed by a slash is treated the same as any other "extension" and
     * simply fails to match the table. */
    ASSERT_NULL(get_mime_type("a.b/c"));
}

TEST(mime_type_to_text_covers_every_enum_value)
{
    ASSERT_STR_EQ(mime_type_to_text(M_VIDEO), "video");
    ASSERT_STR_EQ(mime_type_to_text(M_AUDIO), "audio");
    ASSERT_STR_EQ(mime_type_to_text(M_IMAGE), "image");
    ASSERT_STR_EQ(mime_type_to_text(M_TEXT), "text");
}

TEST(mime_type_to_text_default_case)
{
    ASSERT_STR_EQ(mime_type_to_text((enum MimeType)999), "");
}

/* Integrity of the static table: bsearch() in get_mime_type_from_ext()
 * (mime.c:246) requires types[] to be sorted in strcmp order, and
 * get_mime_type_from_ext()'s "char ext[6]" buffer (mime.c:241) requires
 * every extension to be at most 5 characters. Guards against a future
 * table entry breaking either invariant. */
TEST(mime_table_is_sorted_and_within_buffer_size)
{
    for (int i = 0; i < item_count; i++)
    {
        ASSERT_TRUE(strlen(types[i].ext) <= 5);
        if (i > 0)
            ASSERT_TRUE(strcmp(types[i - 1].ext, types[i].ext) < 0);
    }
}
