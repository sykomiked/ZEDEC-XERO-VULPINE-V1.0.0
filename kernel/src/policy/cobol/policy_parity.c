/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* policy_parity.c — PLCYDEMO's records == the same contract from Sutra.
 *
 *   ./PLCYDEMO > demo.txt && policy_parity demo.txt
 *
 * The COBOL program fills ZXVCNTR field by field, proposes six transfers
 * and calls ZXV_POLICY for each. Here the same contract is written in
 * Sutra and emitted with zxp_emit_record. The contract record COBOL built
 * must match it byte for byte, every transfer record must parse, every
 * result record must equal what ZXV_POLICY gives the C-built contract for
 * that transfer, and the copybook lengths must match the C constants. */
#include "policy_cobol.h"

#include <stdio.h>
#include <string.h>

static char line[8192];

static const char SRC[] = "CONTRACT SUPPLY-2027 20261015\n"
                          "DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
                          "LAW KE\n"
                          "PARTY ACME US-DE\n"
                          "PARTY BETA KE\n"
                          "SIGN 2\n"
                          "WINDOW 20261015 20271231\n"
                          "LOCK 20261224 20261226\n"
                          "CAP USD 2000000\n"
                          "LIMIT ACME USD 1500000\n"
                          "ALLOW KE\n"
                          "ALLOW US\n"
                          "DENY US-NY\n"
                          "DUE T1 ACME BETA USD 1200000 20261231 PENALTY 12000\n"
                          "DUE T2 ACME BETA USD 300000 20270331 AFTER T1\n"
                          "GATE G1 ALL T1 T2\n"
                          "END\n";

int main(int argc, char **argv)
{
    static uint8_t crec[ZXP_REC_LEN], bad[ZXP_REC_LEN];
    static zxp_contract_t c;
    uint8_t res[POLICY_RES_REC_LEN];
    policy_txn_t t;
    int ok = 0, want = 15, nq = 0;
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    if (zxp_parse_sutra(SRC, (uint32_t) strlen(SRC), &c, NULL) != ZXP_OK ||
        zxp_emit_record(&c, crec, sizeof crec) != ZXP_OK)
        return 1;
    memcpy(bad, crec, sizeof bad);
    memcpy(bad + ZXP_REC_HEAD, "k?", 2); /* PLCYDEMO's sixth call */
    while (fgets(line, sizeof line, f)) {
        size_t n = strcspn(line, "\n");
        line[n] = 0;
        if (strncmp(line, "LEN ", 4) == 0) {
            unsigned a = 0, b = 0, d = 0;
            sscanf(line + 4, "%u %u %u", &a, &b, &d);
            if (a == ZXP_REC_LEN && b == POLICY_TXN_REC_LEN && d == POLICY_RES_REC_LEN)
                ok++;
            else
                printf("copybook lengths %u %u %u, want %u %u %u\n", a, b, d, ZXP_REC_LEN,
                       POLICY_TXN_REC_LEN, POLICY_RES_REC_LEN);
        } else if (line[0] == 'C') {
            if (n == 1 + ZXP_REC_LEN && memcmp(line + 1, crec, ZXP_REC_LEN) == 0) {
                ok++;
            } else {
                size_t i = 0;
                while (i < ZXP_REC_LEN && i + 1 < n && (uint8_t) line[1 + i] == crec[i]) i++;
                printf("contract record differs at byte %zu (length %zu):\n COBOL %.40s\n C     "
                       "%.40s\n",
                       i, n - 1, line + 1 + i, crec + i);
            }
        } else if (line[0] == 'Q') {
            nq++;
            if (n == 1 + POLICY_TXN_REC_LEN &&
                policy_parse_txn_record((const uint8_t *) line + 1, POLICY_TXN_REC_LEN, &t) == 0)
                ok++;
            else
                printf("transfer record %d does not parse: %s\n", nq, line + 1);
            ZXV_POLICY((const char *) (nq == 6 ? bad : crec), line + 1, (char *) res);
        } else if (line[0] == 'R') {
            if (n == 1 + POLICY_RES_REC_LEN && memcmp(line + 1, res, POLICY_RES_REC_LEN) == 0)
                ok++;
            else
                printf("result record %d differs:\n COBOL %s\n C     %.41s\n", nq, line + 1, res);
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
