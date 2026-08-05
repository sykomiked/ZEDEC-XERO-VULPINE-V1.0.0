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
size_t strlen(const char *s) { return fs_strlen(s); }
int strcmp(const char *a, const char *b) { return fs_strcmp(a, b); }
char *strcpy(char *dst, const char *src) { return fs_strcpy(dst, src); }
void *malloc(size_t n) { return fs_malloc(n); }
void *calloc(size_t n, size_t s) { return fs_calloc(n, s); }
