/* kernel_main_x86_64.h — x86-64 kernel boot entry
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef KERNEL_MAIN_X86_64_H
#define KERNEL_MAIN_X86_64_H

#include <stdint.h>

void kernel_main_x86_64(uint32_t mb2_magic, uint64_t mb2_info);

#endif
