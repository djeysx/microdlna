/* _GNU_SOURCE is already set by the Makefile's -D_GNU_SOURCE (BASE_CFLAGS,
 * shared with the production build) for memfd_create(). */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "helpers.h"
#include "utils.h"

struct capture *capture_open(void)
{
    struct capture *c = malloc(sizeof(struct capture));
    if (!c)
        abort();

    c->buf = NULL;
    c->len = 0;
    c->st.fh = open_memstream(&c->buf, &c->len);
    if (!c->st.fh)
        abort();
    c->st.pos = 0;

    return c;
}

const char *capture_text(struct capture *c)
{
    fflush(c->st.fh);
    return c->buf ? c->buf : "";
}

char *dechunk(const char *chunked)
{
    size_t cap = strlen(chunked) + 1;
    char *out = malloc(cap);
    if (!out)
        abort();
    size_t out_len = 0;

    const char *p = chunked;
    for (;;)
    {
        char *after_len;
        long len = strtol(p, &after_len, 16);
        if (after_len == p || after_len[0] != '\r' || after_len[1] != '\n' || len < 0)
            abort();   /* malformed chunk framing: a bug in the test, not production */
        p = after_len + 2;

        if (len == 0)
            break;

        memcpy(out + out_len, p, (size_t)len);
        out_len += (size_t)len;
        p += len;

        if (p[0] != '\r' || p[1] != '\n')
            abort();
        p += 2;
    }

    out[out_len] = '\0';
    return out;
}

void capture_close(struct capture *c)
{
    fclose(c->st.fh);
    free(c->buf);
    free(c);
}

int fd_from_bytes(const void *data, size_t len)
{
    int fd = memfd_create("test-input", 0);
    if (fd < 0)
        abort();

    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, (const char *)data + written, len - written);
        if (n < 0)
            abort();
        written += (size_t)n;
    }

    if (lseek(fd, 0, SEEK_SET) != 0)
        abort();

    return fd;
}

static char media_root[PATH_MAX];
static int media_root_ready = 0;

static void remove_tree(const char *path)
{
    /* best-effort recursive rm, good enough for a scratch test directory */
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf -- '%s'", path);
    system(cmd);
}

static void cleanup_media_root(void)
{
    if (media_root_ready)
        remove_tree(media_root);
}

const char *test_media_root(void)
{
    if (!media_root_ready)
    {
        strxcpy(media_root, "/tmp/microdlna-tests-XXXXXX", sizeof(media_root));
        if (!mkdtemp(media_root))
            abort();
        media_root_ready = 1;
        atexit(cleanup_media_root);
    }
    return media_root;
}

void test_mkpath(const char *relpath)
{
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", test_media_root(), relpath);

    /* create intermediate directories one by one (relpath has no "..") */
    for (char *p = full + strlen(test_media_root()) + 1; *p; p++)
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

void test_mkfile(const char *relpath, size_t size)
{
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", test_media_root(), relpath);

    FILE *fh = fopen(full, "wb");
    if (!fh)
        abort();

    if (size > 0)
    {
        if (fseek(fh, (long)(size - 1), SEEK_SET) != 0)
            abort();
        if (fputc('\0', fh) == EOF)
            abort();
    }

    fclose(fh);
}
