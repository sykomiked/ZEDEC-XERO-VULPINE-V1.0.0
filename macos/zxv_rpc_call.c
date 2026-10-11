/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_rpc_call.c — a command-line wrapper round zxv_rpc_client.c, for its
 * test (test_rpc_client.py) and for scripts:
 *   zxv-rpc-call SOCKET '{"jsonrpc":"2.0","id":1,"method":"zxvd.version"}'
 *   zxv-rpc-call --unread SOCKET      the unread count from notes.list
 * Prints the reply line; exit 0, or 1 when there is no reply. */
#include <stdio.h>
#include <string.h>

#include "zxv_rpc_client.h"

int main(int argc, char **argv)
{
    static char reply[1 << 20];
    if (argc == 3 && !strcmp(argv[1], "--unread")) {
        if (zxv_rpc_call(argv[2], "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"notes.list\"}", reply,
                         sizeof reply, 5000) < 0)
            return 1;
        printf("%ld\n", zxv_rpc_unread(reply));
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: zxv-rpc-call SOCKET JSON | zxv-rpc-call --unread SOCKET\n");
        return 2;
    }
    if (zxv_rpc_call(argv[1], argv[2], reply, sizeof reply, 5000) < 0) return 1;
    printf("%s\n", reply);
    return 0;
}
