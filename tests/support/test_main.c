/* Test runner: collects tests registered via TEST(), runs them all (or only
 * the ones whose name contains every word given on the command line), and
 * reports a summary. Exit code is non-zero if any test failed. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#define MAX_TESTS 512

struct test_case
{
    const char *name;
    void (*fn)(void);
};

static struct test_case tests[MAX_TESTS];
static int test_count = 0;

int test_failed;

void tests_register(const char *name, void (*fn)(void))
{
    if (test_count >= MAX_TESTS)
    {
        fprintf(stderr, "too many tests (max %d), skipping %s\n", MAX_TESTS, name);
        return;
    }
    tests[test_count].name = name;
    tests[test_count].fn = fn;
    test_count++;
}

void tests_fail(const char *file, int line, const char *fmt, ...)
{
    va_list va;

    fprintf(stderr, "  %s:%d: ", file, line);

    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
    test_failed = 1;
}

static int name_matches(const char *name, int argc, char *const *argv)
{
    for (int i = 1; i < argc; i++)
        if (strstr(name, argv[i]) == NULL)
            return 0;
    return 1;
}

int main(int argc, char *const *argv)
{
    int passed = 0, failed = 0, skipped = 0;

    for (int i = 0; i < test_count; i++)
    {
        if (!name_matches(tests[i].name, argc, argv))
        {
            skipped++;
            continue;
        }

        test_failed = 0;
        printf("  %-60s ", tests[i].name);
        fflush(stdout);

        tests[i].fn();

        if (test_failed)
        {
            printf("FAIL\n");
            failed++;
        }
        else
        {
            printf("ok\n");
            passed++;
        }
        /* Flush eagerly: if a test leaks memory, LeakSanitizer's end-of-run
         * check calls _exit() directly from an atexit hook, which skips the
         * normal stdio flush and would otherwise silently drop whatever is
         * still sitting in stdout's buffer. */
        fflush(stdout);
    }

    printf("%d passed, %d failed", passed, failed);
    if (skipped > 0)
        printf(", %d skipped (filtered out)", skipped);
    printf("\n");
    fflush(stdout);

    return failed > 0 ? 1 : 0;
}
