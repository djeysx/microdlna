/* Tests for mediadir.c. mediadir.o is excluded from this binary's link.
 *
 * chdir_to_media_dir() resolves media_dir with realpath() only once per
 * process (static is_real), so the whole lifecycle is exercised by a single
 * sequential test -- separate TEST()s would depend on run order. */

#include "../mediadir.c"

#include <limits.h>

#include "test.h"
#include "helpers.h"

TEST(chdir_to_media_dir_lifecycle)
{
    /* 1. a missing directory fails, leaves media_dir as given, and can be
     *    retried (nothing cached on failure) */
    media_dir = strdup("/no/such/microdlna/dir");
    ASSERT_INT_EQ(chdir_to_media_dir(), -1);
    ASSERT_STR_EQ(media_dir, "/no/such/microdlna/dir");
    ASSERT_INT_EQ(chdir_to_media_dir(), -1);
    free(media_dir);

    /* 2. a path through a symlink and "..": resolved to the canonical
     *    directory, and the process really chdir()s there */
    const char *root = test_media_root();
    test_mkpath("real/sub");
    char link_path[PATH_MAX], given[PATH_MAX];
    snprintf(link_path, sizeof(link_path), "%s/link", root);
    snprintf(given, sizeof(given), "%s/real", root);
    ASSERT_INT_EQ(symlink(given, link_path), 0);

    snprintf(given, sizeof(given), "%s/link/sub/..", root);
    media_dir = strdup(given);
    ASSERT_INT_EQ(chdir_to_media_dir(), 0);

    char expected[PATH_MAX], cwd[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/real", root);
    char *canon = realpath(expected, NULL);
    ASSERT_STR_EQ(media_dir, canon);
    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_STR_EQ(cwd, canon);

    /* 3. once resolved, the value is cached: later calls chdir() back to
     *    it even after the process moved elsewhere */
    ASSERT_INT_EQ(chdir("/"), 0);
    ASSERT_INT_EQ(chdir_to_media_dir(), 0);
    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_STR_EQ(cwd, canon);

    /* 4. if the directory disappears afterwards, the cached path is still
     *    used and the chdir() itself fails */
    char real_sub[PATH_MAX];
    snprintf(real_sub, sizeof(real_sub), "%s/sub", canon);
    ASSERT_INT_EQ(chdir("/"), 0);
    ASSERT_INT_EQ(rmdir(real_sub), 0);
    ASSERT_INT_EQ(rmdir(canon), 0);
    ASSERT_INT_EQ(chdir_to_media_dir(), -1);

    free(canon);
    free(media_dir);
    media_dir = NULL;
}
