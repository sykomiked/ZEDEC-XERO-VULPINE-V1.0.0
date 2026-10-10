/* test_legal_response.c — legal_auto_respond must not assert things that did
 * not happen, and must not overflow the caller's buffer.
 *
 * The original emitted a formal notice stating in the PAST TENSE that six
 * enforcement actions "have been executed" — access revoked, node isolated,
 * an entry written to an immutable ledger, a phi seal applied, a notice
 * broadcast to the P2P mesh, friendliness adjusted across all connected
 * nodes. The function body was entirely string formatting; none of it
 * occurred. It also passed `buf_len - pos` to a size_t parameter, so once the
 * text exceeded the caller's buffer the size went negative and became
 * enormous.
 *
 * These tests fail if either returns.
 */
#include <stdio.h>
#include <string.h>
#include "legal_engine.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* case-insensitive substring search, so the assertions do not depend on the
 * exact capitalisation of the prose */
static bool has(const char *hay, const char *needle)
{
    if (!hay || !needle) return false;
    for (const char *p = hay; *p; p++) {
        const char *a = p, *b = needle;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char) (*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char) (*b + 32) : *b;
            if (ca != cb) break;
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0); /* unbuffered: a hang must still show progress */
    printf("=== legal_auto_respond: no unearned claims, no overflow ===\n");
    static legal_engine_ext_t eng;
    static char buf[4096];

    legal_engine_ext_init(&eng);
    eng.phase_tick = 4242;

    /* ---- a benign watcher gets no notice and no enforcement ---- */
    {
        legal_assess_risk(&eng, 7, 90);
        int n = legal_auto_respond(&eng, 7, buf, sizeof buf);
        CHECK(n > 0, "a benign watcher still produces a response");
        CHECK(!has(buf, "REVOKED"), "and it does not claim anything was revoked");
        CHECK(legal_access_level(&eng, 7) == LEGAL_ACCESS_FULL,
              "a benign watcher keeps FULL access");
        legal_enforcement_t e;
        CHECK(legal_enforcement_get(&eng, 7, &e) != 0,
              "and no enforcement record is created for them");
    }

    /* ---- a hostile watcher: the notice must match the state ---- */
    {
        legal_assess_risk(&eng, 42, -80);
        legal_risk_assessment_t risk;
        CHECK(legal_get_risk(&eng, 42, &risk) == 0, "the risk assessment exists");
        CHECK(risk.legal_action_recommended, "and recommends action");

        int n = legal_auto_respond(&eng, 42, buf, sizeof buf);
        CHECK(n > 0 && buf[0], "a notice is produced");
        printf("       notice is %d chars\n", n);

        /* THE CORE PROPERTY: anything the notice says was done, was done. */
        legal_enforcement_t e;
        CHECK(legal_enforcement_get(&eng, 42, &e) == 0,
              "an enforcement record EXISTS — the actions were actually applied, "
              "not merely described");
        CHECK(e.enforced_tick == 4242, "stamped with the tick it happened at");
        CHECK(e.notice_issued == 1, "and records that a notice was issued");

        if (has(buf, "REVOKED")) {
            CHECK(legal_access_level(&eng, 42) == LEGAL_ACCESS_REVOKED,
                  "the notice says REVOKED and the engine state agrees — the "
                  "document is verifiable against legal_enforcement_get()");
        } else if (has(buf, "GUEST")) {
            CHECK(legal_access_level(&eng, 42) == LEGAL_ACCESS_GUEST,
                  "the notice says GUEST and the engine state agrees");
        } else {
            CHECK(false, "the notice names an access level");
        }

        /* THE CLAIMS THAT MUST NOT COME BACK.
         * Each of these was asserted in the past tense by the original and
         * corresponded to no code at all. */
        CHECK(!has(buf, "have been executed"),
              "the notice does NOT assert that unspecified actions "
              "\"have been executed\"");
        CHECK(!has(buf, "immutable ledger"),
              "does NOT claim an entry was written to an immutable ledger");
        CHECK(!has(buf, "broadcast to P2P mesh") && !has(buf, "broadcast to the P2P"),
              "does NOT claim a notice was broadcast to the mesh");
        CHECK(!has(buf, "phi) seal applied") && !has(buf, "seal applied"),
              "does NOT claim a phi seal was applied");
        CHECK(!has(buf, "across all connected nodes"),
              "does NOT claim friendliness was adjusted across other nodes");
        CHECK(!has(buf, "No human intervention was required"),
              "does NOT advertise the absence of human review as a feature");
        CHECK(!has(buf, "cryptographic timestamps"),
              "does NOT claim cryptographic timestamping it does not do");

        /* And it must be honest about what it is. */
        CHECK(has(buf, "this node"), "the notice scopes its actions to THIS NODE");
        CHECK(has(buf, "requires operator") || has(buf, "OPERATOR ACTION"),
              "everything it cannot do is marked as requiring an operator");
        CHECK(has(buf, "not a finding of law") || has(buf, "not evidence"),
              "and it disclaims being a legal finding");
    }

    /* ---- the buffer must never be overrun ---- */
    {
        legal_assess_risk(&eng, 99, -90);
        static char small[4096];
        bool all_ok = true;
        for (uint16_t cap = 1; cap <= 2048; cap++) {
            memset(small, 0x7E, sizeof small); /* poison */
            int n = legal_auto_respond(&eng, 99, small, cap);
            if (n < 0) continue;
            if ((uint32_t) n >= cap) {
                all_ok = false;
                printf("  len %u >= cap %u\n", n, cap);
                break;
            }
            /* must be NUL-terminated inside the window */
            if (small[n] != 0) {
                all_ok = false;
                printf("  not terminated at cap %u\n", cap);
                break;
            }
            /* nothing may be written past the caller's buffer */
            for (uint32_t i = cap; i < sizeof small; i++)
                if (small[i] != 0x7E) {
                    all_ok = false;
                    break;
                }
            if (!all_ok) {
                printf("  wrote past cap %u\n", cap);
                break;
            }
        }
        CHECK(all_ok, "every buffer size from 1 to 2048 is respected exactly — the old "
                      "code passed a negative length to a size_t parameter");
    }

    /* ---- a watcher with no assessment is refused, not invented ---- */
    {
        int n = legal_auto_respond(&eng, 31337, buf, sizeof buf);
        CHECK(n < 0, "a watcher with no risk assessment produces an ERROR, "
                     "not a fabricated notice");
        CHECK(legal_access_level(&eng, 31337) == LEGAL_ACCESS_FULL,
              "and no enforcement is applied to them");
    }

    /* ---- null and zero-length arguments ---- */
    {
        CHECK(legal_auto_respond(0, 42, buf, sizeof buf) < 0, "null engine is refused");
        CHECK(legal_auto_respond(&eng, 42, 0, 100) < 0, "null buffer is refused");
        CHECK(legal_auto_respond(&eng, 42, buf, 0) < 0, "zero length is refused");
    }

    /* ---- repeated responses update rather than duplicate ---- */
    {
        uint32_t before = eng.enforcement_count;
        legal_auto_respond(&eng, 42, buf, sizeof buf);
        legal_auto_respond(&eng, 42, buf, sizeof buf);
        CHECK(eng.enforcement_count == before,
              "re-issuing a notice updates the existing enforcement record "
              "rather than consuming a new slot each time");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
