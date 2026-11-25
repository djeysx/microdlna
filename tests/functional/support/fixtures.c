/* _GNU_SOURCE is already set by the Makefile's -D_GNU_SOURCE (BASE_CFLAGS,
 * shared with the production build), which is what makes mkdtemp()
 * visible from <stdlib.h> here. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fixtures.h"

#define MAX_ROOTS 32

static char roots[MAX_ROOTS][PATH_MAX];
static int root_count = 0;
static int atexit_registered = 0;

static void remove_tree(const char *path)
{
    /* best-effort recursive rm, good enough for a scratch test directory */
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf -- '%s'", path);
    system(cmd);
}

static void cleanup_roots(void)
{
    for (int i = 0; i < root_count; i++)
        remove_tree(roots[i]);
}

const char *ftest_mkdir(void)
{
    if (root_count >= MAX_ROOTS)
        abort();

    char *slot = roots[root_count];
    snprintf(slot, PATH_MAX, "/tmp/microdlna-ftests-XXXXXX");
    if (!mkdtemp(slot))
        abort();

    if (!atexit_registered)
    {
        atexit(cleanup_roots);
        atexit_registered = 1;
    }
    root_count++;

    return slot;
}

void ftest_mkpath(const char *root, const char *relpath)
{
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", root, relpath);

    /* create intermediate directories one by one (relpath has no "..") */
    for (char *p = full + strlen(root) + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(full, 0755);
            *p = '/';
        }
    }
    if (mkdir(full, 0755) != 0 && errno != EEXIST)
        abort();
}

void ftest_mkfile_pattern(const char *root, const char *relpath, size_t size)
{
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", root, relpath);

    FILE *fh = fopen(full, "wb");
    if (!fh)
        abort();

    unsigned char buf[4096];
    size_t written = 0;
    while (written < size)
    {
        size_t chunk = (size - written) < sizeof(buf) ? (size - written) : sizeof(buf);
        for (size_t i = 0; i < chunk; i++)
            buf[i] = (unsigned char)((written + i) % 256);
        if (fwrite(buf, 1, chunk, fh) != chunk)
            abort();
        written += chunk;
    }

    fclose(fh);
}
