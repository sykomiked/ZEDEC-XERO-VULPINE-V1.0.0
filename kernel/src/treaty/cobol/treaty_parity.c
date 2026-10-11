/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* treaty_parity.c — TRTYDEMO's records == the same treaty from Sutra.
 *
 *   ./TRTYDEMO > demo.txt && treaty_parity demo.txt
 *
 * The COBOL program fills ZXVTRTY field by field and calls ZXV_ENGINE.
 * Here the same treaty is written in Sutra, lowered to a record with
 * zxt_emit_record and run with zxt_exec. The treaty record COBOL built
 * and the result record ZXV_ENGINE gave it must match these byte for
 * byte, and the copybook lengths must be ZXT_REC_LEN and ZXT_RES_LEN. */
#include "zxt_exec.h"

#include <stdio.h>
#include <string.h>

static zxv_engine_t ws;
static char line[8192];

static const char TRI[] = "TREATY CORRIDOR-7 USD SETL\n"
                          "PARTY ACME 5000000 4000 1/3\n"
                          "PARTY BETA 3000000 3000 1/7\n"
                          "PARTY GAMMA 900000 0 11/21\n"
                          "OWE ACME BETA 1200000\n"
                          "OWE BETA GAMMA 700000\n"
                          "OWE GAMMA ACME 100000\n"
                          "END\n";

int main(int argc, char **argv)
{
    static uint8_t trec[ZXT_REC_LEN], res[ZXT_RES_LEN];
    zxt_treaty_t t;
    zxt_result_t r;
    int ok = 0, want = 5;
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    if (zxt_parse_sutra(TRI, (uint32_t) strlen(TRI), &t, NULL) != ZXT_OK ||
        zxt_emit_record(&t, trec, sizeof trec) != ZXT_OK)
        return 1;
    if (zxt_exec(&t, &ws, trec + 4, &r) != ZXT_OK || zxt_emit_result(&r, res, sizeof res) != ZXT_OK)
        return 1;
    while (fgets(line, sizeof line, f)) {
        size_t n = strcspn(line, "\n");
        line[n] = 0;
        if (strncmp(line, "LEN ", 4) == 0) {
            unsigned a = 0, b = 0;
            sscanf(line + 4, "%u %u", &a, &b);
            if (a == ZXT_REC_LEN && b == ZXT_RES_LEN)
                ok++;
            else
                printf("copybook lengths %u %u, want %u %u\n", a, b, ZXT_REC_LEN, ZXT_RES_LEN);
        } else if (strncmp(line, "RC ", 3) == 0) {
            if (strcmp(line + 3, "+000000000") == 0)
                ok++;
            else
                printf("RETURN-CODE %s\n", line + 3);
        } else if (line[0] == 'T') {
            if (n == 1 + ZXT_REC_LEN && memcmp(line + 1, trec, ZXT_REC_LEN) == 0)
                ok++;
            else
                printf("treaty record differs:\n COBOL %.120s\n C     %.120s\n", line + 1, trec);
        } else if (line[0] == 'R') {
            if (n == 1 + ZXT_RES_LEN && memcmp(line + 1, res, ZXT_RES_LEN) == 0)
                ok++;
            else
                printf("result record differs:\n COBOL %.120s\n C     %.120s\n", line + 1, res);
        } else if (strcmp(line, "OK") == 0) {
            ok++;
        } else {
            printf("COBOL: %s\n", line);
        }
    }
    fclose(f);
    printf("%s: %d of %d COBOL/C parity checks\n", ok == want ? "PASS" : "FAIL", ok, want);
    return ok == want ? 0 : 1;
}
