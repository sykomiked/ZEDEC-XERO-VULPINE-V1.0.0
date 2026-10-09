/* boot_evidence.h — x86-64 boot evidence measurement system
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef X86_64_BOOT_EVIDENCE_H
#define X86_64_BOOT_EVIDENCE_H

#include <stdint.h>
#include <stddef.h>

void boot_evidence_init(void);
uint32_t boot_evidence_record(const char *msg);
void boot_evidence_final(void);

#endif
