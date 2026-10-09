/* test_fs_snprintf.c — the kernel-wide bounded formatter.
 *
 * Every module in ZXV formats through fs_snprintf(). It had four defects:
 *
 *   1. `case '%'` wrote a byte with no bounds check.
 *   2. `default:` (an unrecognised conversion) wrote TWO bytes with no bounds
 *      check — reachable from any caller using a conversion it lacks.
 *   3. The '-' sign of a negative %d was written with no bounds check.
 *   4. max == 0 made the loop compare `pos < max - 1` against SIZE_MAX, so a
 *      zero-length buffer was treated as unbounded.
 *
 * ...plus a silent correctness bug: no 'l' length modifier, so "%llu" fell to
 * `default:`, emitted a literal "%l", left "lu" in the text, and never
 * consumed the 64-bit vararg — so every later conversion in the same call
 * read the wrong argument.
 *
 * And one introduced while fixing them, caught by these tests: the bounds
 * guard was a macro, and `FS_PUT(tmp[--t])` put the decrement INSIDE the
 * guard, so once the buffer filled the counter stopped moving and the digit
 * loop spun forever.
 *
 * The overrun tests work by poisoning the bytes past the caller's buffer and
 * checking they are untouched — an assertion on the return value alone would
 * not have caught any of the four.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freestanding.h"
#undef snprintf
#undef printf

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){fprintf(stderr,"[FAIL] %s\n",m);failures++;} \
    else fprintf(stderr,"[PASS] %s\n",m);}while(0)

#define GUARD(max_, ...) do {                                               \
    memset(arena, 0x5A, sizeof arena);                                      \
    int n = fs_snprintf(arena, (max_), __VA_ARGS__);                        \
    bool ok = true;                                                          \
    if ((max_) > 0) {                                                        \
        if (n < 0 || (size_t)n >= (max_)) ok = false;                        \
        else if (arena[n] != '\0') ok = false;                               \
    }                                                                        \
    for (size_t i = (max_); i < sizeof arena; i++)                           \
        if (arena[i] != 0x5A) { ok = false; break; }                         \
    if (!ok) { fprintf(stderr, "  overrun/format failure at max=%zu\n",      \
                       (size_t)(max_)); all_ok = false; }                    \
} while (0)

int main(void) {
    fprintf(stderr, "=== fs_snprintf: bounded, correct, terminating ===\n");
    static char arena[512];
    char b[128];

    /* ---- basic conversions ---- */
    {
        int n = fs_snprintf(b, sizeof b, "s=%s d=%d u=%u x=%x c=%c pct=%%",
                            "abc", -42, 7u, 0xBEEFu, 'Z');
        fprintf(stderr, "       '%s'\n", b);
        CHECK(strcmp(b, "s=abc d=-42 u=7 x=beef c=Z pct=%") == 0,
              "the basic conversions all render correctly");
        CHECK(n == (int)strlen(b), "the return value is the length written");
    }

    /* ---- the 'l' length modifier, which did not exist ---- */
    {
        fs_snprintf(b, sizeof b, "%llu", (unsigned long long)12345678901234ULL);
        CHECK(strcmp(b, "12345678901234") == 0,
              "%llu renders a 64-bit value (it used to print a literal \"%l\")");
        fs_snprintf(b, sizeof b, "%lld", (long long)-98765432109LL);
        CHECK(strcmp(b, "-98765432109") == 0, "%lld renders a signed 64-bit value");

        /* THE IMPORTANT ONE: a mishandled length modifier does not consume its
         * argument, so everything after it reads the wrong vararg. */
        fs_snprintf(b, sizeof b, "%llu|%u|%s", (unsigned long long)1ULL, 2u, "three");
        CHECK(strcmp(b, "1|2|three") == 0,
              "conversions AFTER a %llu still read the right arguments — "
              "a mishandled length modifier desynchronises the whole call");
    }

    /* ---- truncation ---- */
    {
        int n = fs_snprintf(b, 4, "abcdefgh");
        CHECK(n == 3 && strcmp(b, "abc") == 0,
              "output is truncated to max-1 and NUL-terminated");
        n = fs_snprintf(b, 1, "abcdefgh");
        CHECK(n == 0 && b[0] == '\0', "max=1 writes only the terminator");
    }

    /* ---- max == 0 must write NOTHING ---- */
    {
        memset(arena, 0x5A, sizeof arena);
        int n = fs_snprintf(arena, 0, "hello");
        CHECK(n == 0, "max=0 returns 0");
        bool clean = true;
        for (size_t i = 0; i < sizeof arena; i++) if (arena[i] != 0x5A) clean = false;
        CHECK(clean,
              "max=0 writes NOTHING — it used to compare against SIZE_MAX and "
              "treat a zero-length buffer as unbounded");
    }

    /* ---- nothing may ever be written past max ---- */
    {
        bool all_ok = true;
        for (size_t m = 0; m <= 80; m++) {
            GUARD(m, "plain text with no conversions at all");
            GUARD(m, "neg %d here", -1234567);
            GUARD(m, "big %llu here", (unsigned long long)18446744073709551615ULL);
            GUARD(m, "hex %x here", 0xDEADBEEFu);
            GUARD(m, "str %s here", "a moderately long string argument");
            GUARD(m, "pct %% here");
            GUARD(m, "unknown %q conversion", 1);   /* the `default:` path */
            GUARD(m, "trailing percent %");
            GUARD(m, "%c%c%c", 'x', 'y', 'z');
            GUARD(m, "%s%s%s%s", "aa", "bb", "cc", "dd");
        }
        CHECK(all_ok,
              "at EVERY buffer size from 0 to 80, across ten format shapes, "
              "nothing is written past the caller's buffer and the result is "
              "always terminated");
    }

    /* ---- an unrecognised conversion is rendered, not skipped ---- */
    {
        fs_snprintf(b, sizeof b, "a%qb", 1);
        CHECK(strcmp(b, "a%qb") == 0,
              "an unknown conversion is emitted literally");
        fs_snprintf(b, sizeof b, "ends with %");
        CHECK(strcmp(b, "ends with %") == 0,
              "a trailing '%' does not read past the end of the format string");
    }

    /* ---- a null string argument must not crash ---- */
    {
        fs_snprintf(b, sizeof b, "[%s]", (const char *)0);
        CHECK(strcmp(b, "[(null)]") == 0, "a NULL %s argument is handled");
        CHECK(fs_snprintf(0, 16, "x") == 0, "a NULL buffer is refused");
        CHECK(fs_snprintf(b, sizeof b, 0) == 0 && b[0] == 0,
              "a NULL format yields an empty string");
    }

    /* ---- the digit loop must TERMINATE when the buffer fills ----
     * This is the regression for the bug introduced while fixing the others:
     * the bounds guard was a macro and the digit index was decremented inside
     * it, so once the buffer filled the index stopped moving. Any of these
     * hanging means it is back. */
    {
        for (size_t m = 1; m <= 40; m++) {
            fs_snprintf(arena, m, "%llu", (unsigned long long)18446744073709551615ULL);
            fs_snprintf(arena, m, "%d", -2147483647);
            fs_snprintf(arena, m, "%x", 0xFFFFFFFFu);
        }
        CHECK(1, "the digit loops terminate at every buffer size, including "
                 "sizes that fill mid-number");
    }

    fprintf(stderr, "\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
