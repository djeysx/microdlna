#pragma once
/* Minimal test harness for MicroDLNA unit tests.
 *
 * Tests self-register via a constructor attribute: writing TEST(name) { ... }
 * is enough, nothing else needs to be declared. test_main.c runs every
 * registered test and reports failures as "file:line: message".
 *
 * No external dependency (consistent with the project's "libc/pthreads/POSIX
 * only" policy, see docs/ARCHITECTURE.md).
 */

#include <stdio.h>
#include <string.h>

/* Set by the currently running test when an assertion fails; checked by
 * test_main.c after calling the test function. */
extern int test_failed;

void tests_register(const char *name, void (*fn)(void));

void tests_fail(const char *file, int line, const char *fmt, ...)
    __attribute__((__format__(__printf__, 3, 4)));

#define TEST(name)                                                      \
    static void name(void);                                            \
    __attribute__((constructor)) static void register_##name(void)     \
    { tests_register(#name, name); }                                   \
    static void name(void)

#define ASSERT_TRUE(cond)                                                \
    do {                                                                 \
        if (!(cond))                                                     \
            tests_fail(__FILE__, __LINE__, "expected true: %s", #cond);  \
    } while (0)

#define ASSERT_INT_EQ(a, b)                                               \
    do {                                                                  \
        long long _a = (a), _b = (b);                                    \
        if (_a != _b)                                                    \
            tests_fail(__FILE__, __LINE__,                                \
                       "expected %s == %s, got %lld != %lld",             \
                       #a, #b, _a, _b);                                   \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                \
    do {                                                                   \
        const char *_a = (a), *_b = (b);                                   \
        if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0)               \
            tests_fail(__FILE__, __LINE__,                                 \
                       "expected %s == %s, got \"%s\" != \"%s\"",          \
                       #a, #b, _a ? _a : "(null)", _b ? _b : "(null)");    \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                              \
    do {                                                                   \
        const char *_h = (haystack), *_n = (needle);                       \
        if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL)            \
            tests_fail(__FILE__, __LINE__,                                 \
                       "expected \"%s\" to contain \"%s\"",                \
                       _h ? _h : "(null)", _n ? _n : "(null)");            \
    } while (0)

#define ASSERT_PTR_EQ(a, b)                                               \
    do {                                                                  \
        const void *_a = (const void *)(a), *_b = (const void *)(b);      \
        if (_a != _b)                                                     \
            tests_fail(__FILE__, __LINE__,                                \
                       "expected %s == %s (same pointer), got %p != %p",  \
                       #a, #b, _a, _b);                                   \
    } while (0)

#define ASSERT_PTR_NE(a, b)                                               \
    do {                                                                  \
        const void *_a = (const void *)(a), *_b = (const void *)(b);      \
        if (_a == _b)                                                     \
            tests_fail(__FILE__, __LINE__,                                \
                       "expected %s != %s, both are %p", #a, #b, _a);     \
    } while (0)

#define ASSERT_NULL(p)                                                    \
    do {                                                                   \
        if ((p) != NULL)                                                   \
            tests_fail(__FILE__, __LINE__, "expected %s to be NULL", #p); \
    } while (0)

#define ASSERT_NOT_NULL(p)                                                 \
    do {                                                                   \
        if ((p) == NULL)                                                   \
            tests_fail(__FILE__, __LINE__, "expected %s to be non-NULL", #p); \
    } while (0)

#define ASSERT_MEM_EQ(a, b, len)                                           \
    do {                                                                   \
        if (memcmp((a), (b), (len)) != 0)                                  \
            tests_fail(__FILE__, __LINE__,                                 \
                       "expected %s and %s to have equal memory (%zu bytes)", \
                       #a, #b, (size_t)(len));                             \
    } while (0)
