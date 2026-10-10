/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fs_heap_peer.c — a second translation unit that includes
 * kernel/include/freestanding.h, the way every kernel object does
 * (-include freestanding.h). axioms_alloc.c uses it to observe whether two
 * translation units share one fs_malloc heap or each get their own. */
#include <stddef.h>
#include "freestanding.h"

void *fs_heap_peer_alloc(size_t n);
void *fs_heap_peer_alloc(size_t n)
{
    return fs_malloc(n);
}
