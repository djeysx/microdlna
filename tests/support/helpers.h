#pragma once
/* Shared test helpers: capturing what a struct stream writes, feeding bytes
 * to functions that read(2) from a fd, and a scratch media directory tree. */

#include <stddef.h>

#include "stream.h"

/* Wraps a struct stream over open_memstream(), so code that writes "to the
 * socket" via stream_printf/chunk_print/... can be inspected afterwards. */
struct capture
{
    struct stream st;   /* must be first: tests pass &c->st as struct stream * */
    char *buf;
    size_t len;
};

struct capture *capture_open(void);

/* Flushes pending chunk data and NUL-terminates; valid until capture_close(). */
const char *capture_text(struct capture *c);

void capture_close(struct capture *c);

/* Strips HTTP chunked-transfer-coding framing ("<hex>\r\n<data>\r\n"...
 * "0\r\n\r\n", as written by stream.c's chunk_print*()/chunk_print_end())
 * from `chunked`, returning just the concatenated payload. Needed whenever
 * a test inspects content as a whole (e.g. matching a tag across what may
 * be two chunks): the payload can be split anywhere a 1024-byte buffer
 * (BUFFER_SIZE, stream.h) filled up mid-write, so capture_text()'s raw
 * bytes are not reliably searchable/parseable as-is. Caller must free()
 * the result. */
char *dechunk(const char *chunked);

/* Creates an anonymous in-memory fd (memfd_create) containing data, rewound
 * to offset 0, ready to be read(2) from (e.g. by readline() or
 * process_post_content()). */
int fd_from_bytes(const void *data, size_t len);

/* Returns the canonical path to a scratch media directory, created once for
 * the whole test process (chdir_to_media_dir() can only resolve media_dir
 * once per process, see mediadir.c) and removed at exit. Safe to call from
 * multiple tests in the same binary. */
const char *test_media_root(void);

/* Convenience helpers to populate test_media_root(): relative to it, create
 * a subdirectory (mkpath) or a regular file of a given size (mkfile). */
void test_mkpath(const char *relpath);

void test_mkfile(const char *relpath, size_t size);
