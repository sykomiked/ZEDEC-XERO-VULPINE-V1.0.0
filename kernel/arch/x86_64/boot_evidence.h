/* boot_evidence.h — x86-64 boot evidence measurement system
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef X86_64_BOOT_EVIDENCE_H
#define X86_64_BOOT_EVIDENCE_H

#include <stdint.h>
#include <stddef.h>

void boot_evidence_init(void);
uint32_t boot_evidence_record(const char *msg);
void boot_evidence_final(void);

#endif
