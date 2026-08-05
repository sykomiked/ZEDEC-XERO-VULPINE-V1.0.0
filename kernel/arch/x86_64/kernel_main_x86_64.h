/* kernel_main_x86_64.h — x86-64 kernel boot entry
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef KERNEL_MAIN_X86_64_H
#define KERNEL_MAIN_X86_64_H

#include <stdint.h>

void kernel_main_x86_64(uint32_t mb2_magic, uint64_t mb2_info);

#endif
