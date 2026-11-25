/* Tests for dirlist.c (ContentDirectory directory listing: filtering,
 * sorting, pagination). dirlist.o is excluded from this binary's link.
 *
 * chdir_to_media_dir() (mediadir.c) resolves media_dir via realpath() and
 * chdir()s into it exactly once per process (a "static int is_real" guard),
 * so every test in this binary shares the one scratch root from
 * test_media_root() and only ever adds subdirectories under it - never
 * reassign the global media_dir after the first get_directory_listing()
 * call, it would be ignored anyway.
 *
 * media_dir is freed by chdir_to_media_dir() on its first call (mediadir.c),
 * so it must point at a malloc'd string, never at test_media_root()'s own
 * static buffer directly.
 */

#include "../dirlist.c"

#include "test.h"
#include "helpers.h"

static void ensure_media_dir(void)
{
    static int done = 0;
    if (!done)
    {
        media_dir = safe_strdup(test_media_root());
        done = 1;
    }
}

static void free_entries(content_entry **entries, int count)
{
    for (int i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
}

TEST(listing_filters_and_sorts_dirs_before_files)
{
    ensure_media_dir();
    test_mkpath("sort/sub_a");
    test_mkpath("sort/Sub_B");
    test_mkfile("sort/apple.mp4", 10);
    test_mkfile("sort/Banana.mkv", 20);
    test_mkfile("sort/.hidden.mp4", 5);     /* hidden: excluded */
    test_mkfile("sort/$tmp.mp4", 5);        /* '$': excluded */
    test_mkfile("sort/noextension", 5);     /* unknown type: excluded */
    test_mkfile("sort/unknown.xyz", 5);     /* unknown extension: excluded */

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("sort");
    h.requested_count = -1;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NOT_NULL(entries);
    ASSERT_INT_EQ(total, 4);
    ASSERT_INT_EQ(h.requested_count, 4);
    ASSERT_INT_EQ(entries[0]->type, T_DIR);
    ASSERT_STR_EQ(entries[0]->name, "sub_a");
    ASSERT_INT_EQ(entries[1]->type, T_DIR);
    ASSERT_STR_EQ(entries[1]->name, "Sub_B");
    ASSERT_INT_EQ(entries[2]->type, T_FILE);
    ASSERT_STR_EQ(entries[2]->name, "apple.mp4");
    ASSERT_INT_EQ((long long)entries[2]->size, 10);
    ASSERT_INT_EQ(entries[3]->type, T_FILE);
    ASSERT_STR_EQ(entries[3]->name, "Banana.mkv");

    free_entries(entries, h.requested_count);
    free(h.remote_dirpath);
}

TEST(pagination_returns_a_partial_window_shifted_to_the_front)
{
    ensure_media_dir();
    test_mkpath("page");
    test_mkfile("page/f1.mp4", 1);
    test_mkfile("page/f2.mp4", 1);
    test_mkfile("page/f3.mp4", 1);
    test_mkfile("page/f4.mp4", 1);

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("page");
    h.requested_count = 2;
    h.starting_index = 1;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NOT_NULL(entries);
    ASSERT_INT_EQ(total, 4);
    ASSERT_INT_EQ(h.requested_count, 2);
    ASSERT_STR_EQ(entries[0]->name, "f2.mp4");
    ASSERT_STR_EQ(entries[1]->name, "f3.mp4");

    free_entries(entries, h.requested_count);
    free(h.remote_dirpath);
}

TEST(requested_count_larger_than_total_is_clamped)
{
    ensure_media_dir();
    test_mkpath("clamp");
    test_mkfile("clamp/f1.mp4", 1);
    test_mkfile("clamp/f2.mp4", 1);

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("clamp");
    h.requested_count = 100;
    h.starting_index = 0;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_INT_EQ(total, 2);
    ASSERT_INT_EQ(h.requested_count, 2);

    free_entries(entries, h.requested_count);
    free(h.remote_dirpath);
}

TEST(starting_index_past_the_end_returns_nothing)
{
    /* A valid-but-empty result (requested_count ends up 0) must still come
     * back as a non-NULL array: get_directory_listing()'s final realloc
     * keeps at least one slot allocated for this case (dirlist.c), the same
     * way the "*file_count == 0" fast path above it already did - a NULL
     * return is reserved for its two actual error paths (403/503), both of
     * which already send a response via h->st before returning. The only
     * caller, browse_content_directory() (upnpsoap.c), relies on exactly
     * that: it treats NULL as "already handled" and just returns. */
    ensure_media_dir();
    test_mkpath("past-end");
    test_mkfile("past-end/f1.mp4", 1);
    test_mkfile("past-end/f2.mp4", 1);
    test_mkfile("past-end/f3.mp4", 1);

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("past-end");
    h.requested_count = -1;
    h.starting_index = 99;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NOT_NULL(entries);
    ASSERT_INT_EQ(total, 3);           /* total is unaffected */
    ASSERT_INT_EQ(h.requested_count, 0);  /* nothing actually returned */
    ASSERT_INT_EQ(h.starting_index, 0);   /* reset */

    free(entries);
    free(h.remote_dirpath);
}

TEST(requested_count_zero_returns_an_empty_but_valid_result)
{
    /* A client can also reach requested_count == 0 directly (RequestedCount
     * set to 0 in the Browse request) without starting_index being past the
     * end at all - the same fix covers both paths. */
    ensure_media_dir();
    test_mkpath("zero-count");
    test_mkfile("zero-count/f1.mp4", 1);
    test_mkfile("zero-count/f2.mp4", 1);

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("zero-count");
    h.requested_count = 0;
    h.starting_index = 0;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NOT_NULL(entries);
    ASSERT_INT_EQ(total, 2);
    ASSERT_INT_EQ(h.requested_count, 0);

    free(entries);
    free(h.remote_dirpath);
}

TEST(empty_directory_returns_zero_entries)
{
    ensure_media_dir();
    test_mkpath("empty-dir");

    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("empty-dir");
    h.requested_count = -1;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NOT_NULL(entries);
    ASSERT_INT_EQ(total, 0);
    ASSERT_INT_EQ(h.requested_count, 0);

    free(entries);
    free(h.remote_dirpath);
}

TEST(path_traversal_is_rejected_with_403)
{
    ensure_media_dir();

    struct capture *c = capture_open();
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("../..");
    h.st = &c->st;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NULL(entries);
    ASSERT_STR_CONTAINS(capture_text(c), "403");

    free(h.remote_dirpath);
    capture_close(c);
}

TEST(nonexistent_directory_returns_503)
{
    ensure_media_dir();

    struct capture *c = capture_open();
    struct upnphttp h;
    memset(&h, 0, sizeof(h));
    h.remote_dirpath = safe_strdup("this-does-not-exist");
    h.st = &c->st;

    unsigned int total;
    content_entry **entries = get_directory_listing(&h, &total);

    ASSERT_NULL(entries);
    ASSERT_STR_CONTAINS(capture_text(c), "503");

    free(h.remote_dirpath);
    capture_close(c);
}
