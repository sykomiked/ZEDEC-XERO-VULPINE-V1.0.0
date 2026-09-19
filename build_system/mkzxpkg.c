/* mkzxpkg.c — host build tool: wrap a compiled artifact into its native
 * ZXV Tri-Space triad (.zxvc / .cedez / .cedec).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is the step that renders a build in ZXV's native format. A conventional
 * build stops at one binary; this takes that binary as the positive space (S+,
 * what the program DOES), generates the negative space (S-, its rollback /
 * undo — labelled generated, an honest draft, powers a subset of S+), and the
 * neutral space (S0, the unresolved remainder — no production capability), then
 * SEALS the triad so a loader can refuse an incomplete or substituted release.
 *
 * It reuses the exact freestanding kernel code (src/zxpkg, src/trispace,
 * src/robin_debanks/sha256) compiled for the host, so the files it writes are
 * byte-identical to what the kernel itself would accept.
 *
 *   mkzxpkg <input-image> <output-basename> [inverse-kind]
 * writes:
 *   <output-basename>.zxvc   (S+)
 *   <output-basename>.cedez  (S-)
 *   <output-basename>.cedec  (S0)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/src/zxpkg/zxpkg.h"
#include "../kernel/src/robin_debanks/sha256.h"

static unsigned char *slurp(const char *path, uint32_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)n ? (size_t)n : 1);
    if (b && n) { if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; } }
    fclose(f);
    *len_out = (uint32_t)n;
    return b;
}

static int spew(const char *path, const unsigned char *b, uint32_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = (fwrite(b, 1, n, f) == n);
    fclose(f);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <input-image> <output-basename> [inverse-kind]\n"
            "  inverse-kind: exact|compensating|restoring|constraining|observational"
            "  (default: restoring — an OS install is undone by A/B rollback)\n", argv[0]);
        return 2;
    }
    const char *in = argv[1], *base = argv[2];
    tri_inverse_kind_t ik = TRI_INV_RESTORING;
    if (argc >= 4) {
        if      (!strcmp(argv[3], "exact"))         ik = TRI_INV_EXACT;
        else if (!strcmp(argv[3], "compensating"))  ik = TRI_INV_COMPENSATING;
        else if (!strcmp(argv[3], "restoring"))     ik = TRI_INV_RESTORING;
        else if (!strcmp(argv[3], "constraining"))  ik = TRI_INV_CONSTRAINING;
        else if (!strcmp(argv[3], "observational")) ik = TRI_INV_OBSERVATIONAL;
        else { fprintf(stderr, "mkzxpkg: unknown inverse-kind '%s'\n", argv[3]); return 2; }
    }

    uint32_t imgn = 0;
    unsigned char *img = slurp(in, &imgn);
    if (!img) { fprintf(stderr, "mkzxpkg: cannot read %s\n", in); return 1; }

    /* The negative space: a generated rollback descriptor. It is text now; a
     * real build emits an A/B-slot restore manifest. It is GENERATED and does
     * NOT claim to be a proven inverse (it is a draft), and it carries a subset
     * of the positive space's capabilities. */
    static const char undo_tmpl[] =
        "ZXV-ROLLBACK v2\n"
        "kind: restore-prior-slot\n"
        "action: on failure, boot the previous A/B slot and revoke the seals "
        "installed by this image; generated, not a proven inverse\n";
    /* The neutral space: what this build leaves unresolved. No capability. */
    static const char unresolved[] =
        "ZXV-UNRESOLVED v2\n"
        "pending: release signature (ZSP/Ed25519) is applied in a later, signed "
        "step; deployment profile (home/work/server/hpc) is selected at install\n";

    zxpkg_spec_t spec;
    memset(&spec, 0, sizeof spec);
    /* triad id = sha256(basename); source graph digest = sha256(the image) */
    sha256((const uint8_t *)base, (size_t)strlen(base), spec.triad_id);
    sha256(img, imgn, spec.source_graph_digest);
    spec.inverse_kind = ik;
    spec.irreversible = 0;   /* an A/B install is restorable */

    spec.payload[TRI_POSITIVE] = img; spec.payload_len[TRI_POSITIVE] = imgn;
    spec.capability[TRI_POSITIVE] = 0x0F;   /* install/boot/configure/seal */

    spec.payload[TRI_NEGATIVE] = (const uint8_t *)undo_tmpl;
    spec.payload_len[TRI_NEGATIVE] = (uint32_t)(sizeof undo_tmpl - 1);
    spec.capability[TRI_NEGATIVE] = 0x03;   /* subset: revoke + restore only */
    spec.generated[TRI_NEGATIVE] = 1;
    spec.claims_proven_inverse[TRI_NEGATIVE] = 0;

    spec.payload[TRI_NEUTRAL] = (const uint8_t *)unresolved;
    spec.payload_len[TRI_NEUTRAL] = (uint32_t)(sizeof unresolved - 1);
    spec.capability[TRI_NEUTRAL] = 0;       /* cannot act on the world */

    uint8_t seal[TRI_DIGEST_LEN];
    tri_quarantine_t q = zxpkg_seal(&spec, seal);
    if (q != TRI_Q_NONE) {
        fprintf(stderr, "mkzxpkg: triad did not seal: %s\n", tri_quarantine_reason(q));
        free(img); return 1;
    }

    /* Emit the seal so an OFFLINE signer can bind a release identity to it
     * (build_system/sign_release.sh -> a ZSP envelope the kernel verifies). */
    { char sp[4096]; snprintf(sp, sizeof sp, "%s.seal", base);
      if (spew(sp, seal, TRI_DIGEST_LEN) == 0) printf("  wrote %s (seal for offline signing)\n", sp); }

    /* write the three members */
    uint32_t cap = ZXPKG_HDR_LEN + imgn + 4096;
    unsigned char *out = (unsigned char *)malloc(cap);
    char path[4096];
    int rc = 0;
    const tri_role_t roles[3] = { TRI_POSITIVE, TRI_NEGATIVE, TRI_NEUTRAL };
    for (int i = 0; i < 3; i++) {
        uint32_t n = zxpkg_write(&spec, roles[i], seal, out, cap);
        if (!n) { fprintf(stderr, "mkzxpkg: write failed\n"); rc = 1; break; }
        snprintf(path, sizeof path, "%s%s", base, zxpkg_extension(roles[i]));
        if (spew(path, out, n) != 0) { fprintf(stderr, "mkzxpkg: cannot write %s\n", path); rc = 1; break; }
        printf("  wrote %s (%u bytes, %s)\n", path, n, tri_role_name(roles[i]));
    }

    /* self-check: read the three back and verify the triad, so the tool never
     * claims success for a triad the kernel would reject */
    if (rc == 0) {
        uint32_t lp=0, ln=0, lu=0;
        char pp[4096], pn[4096], pu[4096];
        snprintf(pp,sizeof pp,"%s%s",base,zxpkg_extension(TRI_POSITIVE));
        snprintf(pn,sizeof pn,"%s%s",base,zxpkg_extension(TRI_NEGATIVE));
        snprintf(pu,sizeof pu,"%s%s",base,zxpkg_extension(TRI_NEUTRAL));
        unsigned char *bp=slurp(pp,&lp), *bn=slurp(pn,&ln), *bu=slurp(pu,&lu);
        tri_quarantine_t v = (bp&&bn&&bu)
            ? zxpkg_verify_triad(bp,lp,bn,ln,bu,lu) : TRI_Q_MISSING_MEMBER;
        if (v != TRI_Q_NONE) {
            fprintf(stderr, "mkzxpkg: SELF-CHECK FAILED: %s\n", tri_quarantine_reason(v));
            rc = 1;
        } else {
            printf("  verified: triad is intact and releasable\n");
        }
        free(bp); free(bn); free(bu);
    }

    free(out); free(img);
    return rc;
}
