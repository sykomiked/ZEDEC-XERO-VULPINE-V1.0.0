/* freestanding.c — Global libc symbol definitions for freestanding builds
 *
 * The kernel compiles with -Dmemcpy=fs_memcpy etc. so that all source
 * files use the freestanding helpers transparently. However, the compiler
 * may still emit real memcpy/memset calls for structure/array copies. This
 * file provides strong global symbols for those calls, all implemented in
 * terms of the inline fs_* helpers from freestanding.h.
 */

#include "freestanding.h"

#undef memset
#undef memcpy
#undef strlen
#undef strcmp
#undef strcpy
#undef malloc
#undef calloc

void *memset(void *dst, int c, size_t n) { return fs_memset(dst, c, n); }
void *memcpy(void *dst, const void *src, size_t n) { return fs_memcpy(dst, src, n); }
void *fs_realloc(void *p, size_t n) {
    /* the bump arena never moves a block: growing copies n bytes forward
     * (the arena is contiguous and mapped, so the read is safe); the old
     * block is left behind for the arena's lifetime */
    void *q = fs_malloc(n);
    if (!q) return 0;
    if (p) fs_memcpy(q, p, n);
    return q;
}
char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char) c) return (char *) s;
    return (char) c ? 0 : (char *) s;
}
void *fs_memmove(void *d, const void *s, size_t n) {
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) { for (size_t i = 0; i < n; i++) dd[i] = ss[i]; }
    else { for (size_t i = n; i; i--) dd[i - 1] = ss[i - 1]; }
    return d;
}
size_t strlen(const char *s) { return fs_strlen(s); }
int strcmp(const char *a, const char *b) { return fs_strcmp(a, b); }
char *strcpy(char *dst, const char *src) { return fs_strcpy(dst, src); }
void *malloc(size_t n) { return fs_malloc(n); }
void free(void *p) { (void) p; }
void *calloc(size_t n, size_t s) { return fs_calloc(n, s); }
