#pragma once
/* Per-test scratch media directories for functional tests. Unlike
 * tests/support/helpers.c's test_media_root() (one fixed root per *unit
 * test* process, required because chdir_to_media_dir() can only resolve
 * media_dir once per process), each call here returns a brand new
 * directory: every functional test starts its own microdlnad instance, so
 * there's no cross-test interference to guard against, and some scenarios
 * want more than one independent tree in the same test binary. */

#include <stddef.h>

/* Creates a new empty temp directory under /tmp and returns its path
 * (caller must not modify or free it; stays valid for the life of the
 * process, and is recursively removed at exit). */
const char *ftest_mkdir(void);

/* Creates relpath (and any intermediate directories) under root. */
void ftest_mkpath(const char *root, const char *relpath);

/* Creates a file at root/relpath of exactly `size` bytes, filled with a
 * deterministic repeating byte pattern (byte i % 256) -- unlike a sparse
 * file sized via fseek+fputc, this lets a test assert on the actual bytes
 * a request returns, not just on sizes. */
void ftest_mkfile_pattern(const char *root, const char *relpath, size_t size);
