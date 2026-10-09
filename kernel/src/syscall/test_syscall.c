/* test_syscall.c — ZXV ABI tests.
 *
 * The headline tests are the two that make this a CONTRACT rather than a list:
 *   1. the permission decision is TABLE-DRIVEN, so a call cannot exist without
 *      a declared capability and the arches cannot drift apart on who may do
 *      what;
 *   2. syscall numbers are PERMANENT — the numbering is pinned here, so a
 *      renumbering that would silently redirect old binaries into a different
 *      kernel function fails the build instead of shipping.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "syscall.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

int main(void)
{
    zxv_syscall_info_t s;
    printf("ZXV syscall ABI v%u.%u\n", ZXV_ABI_MAJOR, ZXV_ABI_MINOR);

    printf("the table is internally consistent:\n");
    CHECK(zxv_syscall_selfcheck() == 0,
          "every call has a name, a legal capability, and a sane arity");
    CHECK(zxv_syscall_count() >= 13, "the ABI defines its full surface");

    printf("numbers are PERMANENT (pinned, so a renumber fails the build):\n");
    /* If any of these change, old binaries call a different function while
     * everything still links. That is why they are asserted, not assumed. */
    CHECK(ZXV_SYS_EXIT == 1 && ZXV_SYS_WRITE == 2 && ZXV_SYS_GETPID == 3,
          "1/2/3 are EXIT/WRITE/GETPID");
    CHECK(ZXV_SYS_YIELD == 4 && ZXV_SYS_SLEEP == 5 && ZXV_SYS_SEND == 6,
          "4/5/6 are YIELD/SLEEP/SEND");
    CHECK(ZXV_SYS_RECV == 7 && ZXV_SYS_OPEN == 8 && ZXV_SYS_CLOSE == 9,
          "7/8/9 are RECV/OPEN/CLOSE");
    CHECK(ZXV_SYS_READ == 10 && ZXV_SYS_EXEC == 11, "10/11 are READ/EXEC");
    CHECK(zxv_syscall_info(0, &s) && s.reserved,
          "0 is a permanent RESERVED hole, never dispatchable");

    printf("permission is table-driven, in ONE place:\n");
    CHECK(zxv_syscall_permit(ZXV_SYS_WRITE, ZSC_CAP_WRITE) == ZXV_OK,
          "WRITE with CAP_WRITE is allowed");
    CHECK(zxv_syscall_permit(ZXV_SYS_WRITE, ZSC_CAP_PROC) == ZXV_EPERM,
          "WRITE without CAP_WRITE is EPERM");
    CHECK(zxv_syscall_permit(ZXV_SYS_EXEC, ZSC_CAP_WRITE | ZSC_CAP_PROC) == ZXV_EPERM,
          "EXEC is refused to a process holding only WRITE+PROC");
    CHECK(zxv_syscall_permit(ZXV_SYS_EXEC, ZSC_CAP_WRITE | ZSC_CAP_PROC | ZSC_CAP_EXEC |
                                               ZSC_CAP_IPC | ZSC_CAP_FS) == ZXV_OK,
          "a trusted process may EXEC");
    CHECK(zxv_syscall_permit(ZXV_SYS_ABI_VERSION, 0) == ZXV_OK,
          "a call requiring NO capability works with none held");
    CHECK(zxv_syscall_permit(0, 0xFFFFFFFFu) == ZXV_ENOSYS,
          "the reserved hole is ENOSYS even for an all-powerful caller");
    CHECK(zxv_syscall_permit(9999, 0xFFFFFFFFu) == ZXV_ENOSYS,
          "a number outside the ABI is ENOSYS, not a crash");

    printf("least privilege actually restricts:\n");
    {
        uint32_t app = ZSC_CAP_WRITE | ZSC_CAP_PROC; /* the app default */
        int denied = 0, allowed = 0;
        for (uint32_t n = 1; n < zxv_syscall_count(); n++) {
            if (!zxv_syscall_info(n, &s) || s.reserved) continue;
            if (zxv_syscall_permit(n, app) == ZXV_OK)
                allowed++;
            else
                denied++;
        }
        CHECK(denied > 0 && allowed > 0,
              "a least-privilege app is allowed some calls and denied others");
        CHECK(zxv_syscall_permit(ZXV_SYS_OPEN, app) == ZXV_EPERM,
              "an untrusted app cannot OPEN files");
        printf("       (app default: %d allowed, %d denied)\n", allowed, denied);
    }

    printf("ABI version negotiation:\n");
    CHECK(zxv_abi_compatible(ZXV_ABI_VERSION), "our own version is accepted");
    CHECK(zxv_abi_compatible((1u << 16) | 0u), "an older MINOR is accepted");
    CHECK(!zxv_abi_compatible((1u << 16) | 99u),
          "a caller from the FUTURE is refused, not half-served");
    CHECK(!zxv_abi_compatible((2u << 16) | 0u), "a different MAJOR is refused");

    printf("every call is documented and named:\n");
    {
        int ok = 1;
        for (uint32_t n = 0; n < zxv_syscall_count(); n++) {
            if (!zxv_syscall_info(n, &s)) {
                ok = 0;
                break;
            }
            if (!s.name || !s.doc) {
                ok = 0;
                break;
            }
        }
        CHECK(ok, "info() answers for every number in the ABI");
        zxv_syscall_info(ZXV_SYS_WRITE, &s);
        CHECK(strcmp(s.name, "WRITE") == 0 && s.arity == 2,
              "names and arity come from the same row as the number");
    }

    printf("\n%s syscall: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
