/* test_denconnect_gov.c — sovereign-node governance.
 * The point: every node's operator sets its own rules, sovereignty stops at
 * the node boundary, and nobody can escalate their own privileges. */
#include <stdio.h>
#include <string.h>
#include "denconnect.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static void mkkey(uint8_t k[DEN_KEY_LEN], uint8_t seed)
{
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) k[i] = (uint8_t) (seed * 31u + i);
}

int main(void)
{
    printf("=== Den Connect sovereign-node governance ===\n");
    uint8_t op[32], alice[32], bob[32], mallory[32];
    mkkey(op, 1);
    mkkey(alice, 2);
    mkkey(bob, 3);
    mkkey(mallory, 4);

    den_server_t s;
    CHECK(den_init(&s, op), "node founded");

    /* ---- the operator is sovereign ---- */
    CHECK(den_is_operator(&s, op), "founder is the operator");
    CHECK(den_privileges(&s, op) == DEN_ALL, "operator holds ALL privileges");
    CHECK(den_can(&s, op, DEN_BAN | DEN_SET_ACCESS | DEN_UPLOAD),
          "operator can do anything on their node");

    /* ---- guests get the operator-chosen default set ---- */
    CHECK(den_can(&s, alice, DEN_SEND_CHAT), "a guest can chat by default");
    CHECK(!den_can(&s, alice, DEN_UPLOAD), "a guest cannot upload by default");
    CHECK(!den_can(&s, alice, DEN_SET_ACCESS), "a guest cannot change the rules");

    /* ---- the operator sets THIS node's rules ---- */
    CHECK(den_set_guest_privileges(&s, op, DEN_READ_CHAT),
          "operator restricts guests to read-only chat");
    CHECK(!den_can(&s, alice, DEN_SEND_CHAT), "now a guest cannot even send chat on THIS node");

    /* ---- everybody sets the rules of their OWN node ---- */
    {
        den_server_t s2;
        den_init(&s2, op);
        den_set_guest_privileges(&s2, op, DEN_ALL & ~DEN_SET_ACCESS & ~DEN_BAN);
        CHECK(den_can(&s2, alice, DEN_UPLOAD) && !den_can(&s, alice, DEN_UPLOAD),
              "the SAME guest has different rights on two different nodes "
              "(each operator is sovereign)");
    }

    /* ---- the operator grants a specific account a custom privilege set ---- */
    CHECK(den_set_account(&s, op, alice, "alice",
                          DEN_READ_CHAT | DEN_SEND_CHAT | DEN_UPLOAD | DEN_DOWNLOAD),
          "operator creates alice with upload rights");
    CHECK(den_can(&s, alice, DEN_UPLOAD) && den_can(&s, alice, DEN_SEND_CHAT),
          "alice has exactly the granted privileges");
    CHECK(!den_can(&s, alice, DEN_BAN) && !den_can(&s, alice, DEN_SET_ACCESS),
          "and no more than what she was granted");

    /* ---- NO privilege escalation ---- */
    CHECK(!den_set_guest_privileges(&s, alice, DEN_ALL),
          "alice (no SET_ACCESS) cannot rewrite the node's rules");
    CHECK(!den_set_account(&s, alice, mallory, "m", DEN_ALL),
          "alice cannot create accounts (no CREATE_ACCOUNT bit)");
    /* give alice CREATE_ACCOUNT but NOT SET_ACCESS; she still can't mint an admin */
    den_set_account(&s, op, alice, "alice", DEN_READ_CHAT | DEN_SEND_CHAT | DEN_CREATE_ACCOUNT);
    CHECK(den_set_account(&s, alice, bob, "bob", DEN_SEND_CHAT),
          "alice with CREATE_ACCOUNT can make an ordinary account");
    CHECK(!den_set_account(&s, alice, mallory, "m", DEN_SET_ACCESS),
          "but she CANNOT grant SET_ACCESS she does not hold (no escalation)");
    CHECK(!den_can(&s, mallory, DEN_SET_ACCESS), "mallory did not become an admin");

    /* ---- a delegated admin (operator grants SET_ACCESS) CAN govern ---- */
    den_set_account(&s, op, bob, "bob-admin", DEN_ALL);
    CHECK(den_set_guest_privileges(&s, bob, DEN_READ_CHAT | DEN_DOWNLOAD),
          "an account the operator gave SET_ACCESS can change the rules");

    /* ---- bans ---- */
    CHECK(den_ban(&s, op, mallory), "operator bans mallory");
    CHECK(den_is_banned(&s, mallory), "mallory is banned");
    CHECK(den_privileges(&s, mallory) == 0, "a banned identity has ZERO privileges");
    CHECK(!den_can(&s, mallory, DEN_READ_CHAT), "banned overrides even guest rights");
    CHECK(!den_ban(&s, op, op), "the operator cannot be banned");
    CHECK(den_unban(&s, op, mallory) && !den_is_banned(&s, mallory),
          "operator unbans mallory; guest rights return");

    /* ---- visibility tiers (public / private / select) ---- */
    CHECK(den_may_connect(&s, mallory), "PUBLIC node: a stranger may connect");
    den_set_visibility(&s, op, DEN_PRIVATE);
    CHECK(den_may_connect(&s, op) && !den_may_connect(&s, mallory),
          "PRIVATE node: only the operator may connect");
    den_set_visibility(&s, op, DEN_SELECT);
    CHECK(den_may_connect(&s, alice) && !den_may_connect(&s, mallory),
          "SELECT node: only named accounts (the invite list) may connect");

    /* ---- sovereignty stops at the node boundary ---- */
    CHECK(den_can(&s, op, DEN_SET_ACCESS),
          "operator power exists only within den_* — it names no kernel/other-node action");

    /* ---- ACCOUNTABILITY: every rule change is logged tamper-evidently ---- */
    {
        den_server_t a;
        den_init(&a, op);
        CHECK(den_audit_length(&a) == 0 && den_audit_verify(&a),
              "a fresh den has an empty, valid audit log");
        den_set_guest_privileges(&a, op, DEN_READ_CHAT);
        den_set_account(&a, op, alice, "alice", DEN_SEND_CHAT);
        den_ban(&a, op, mallory);
        CHECK(den_audit_length(&a) == 3, "three rule changes were logged");
        CHECK(den_audit_verify(&a), "the moderation log verifies");
        /* a DENIED action is not logged (nothing took effect) */
        den_set_guest_privileges(&a, alice, DEN_ALL); /* alice lacks SET_ACCESS */
        CHECK(den_audit_length(&a) == 3, "a denied action leaves NO audit entry");
        /* tamper: rewrite what the operator supposedly did */
        den_server_t t = a;
        t.audit[1].param = DEN_ALL; /* pretend alice was made an admin */
        CHECK(!den_audit_verify(&t),
              "editing the moderation history is DETECTED — the operator is accountable");
    }

    /* ---- a DEN FLEET: one person runs a series of isolated dens ---- */
    {
        den_fleet_t f;
        den_fleet_init(&f, op);
        int32_t lobby = den_fleet_found(&f, "public-lobby");
        int32_t family = den_fleet_found(&f, "family");
        int32_t work = den_fleet_found(&f, "work");
        CHECK(lobby >= 0 && family >= 0 && work >= 0 && den_fleet_count(&f) == 3,
              "one operator runs three dens at once");
        CHECK(den_fleet_find(&f, "family") == family && den_fleet_find(&f, "nope") < 0,
              "dens are addressable by label");

        /* give each den its own rules */
        den_set_visibility(den_fleet_get(&f, (uint32_t) family), op, DEN_PRIVATE);
        den_set_visibility(den_fleet_get(&f, (uint32_t) work), op, DEN_SELECT);
        den_set_account(den_fleet_get(&f, (uint32_t) work), op, alice, "alice", DEN_SEND_CHAT);

        /* PRIVACY: the dens are isolated — a stranger allowed in one is not in another */
        CHECK(den_may_connect(den_fleet_get(&f, (uint32_t) lobby), mallory),
              "a stranger may join the public lobby");
        CHECK(!den_may_connect(den_fleet_get(&f, (uint32_t) family), mallory),
              "but NOT the private family den");
        CHECK(den_may_connect(den_fleet_get(&f, (uint32_t) work), alice) &&
                  !den_may_connect(den_fleet_get(&f, (uint32_t) work), mallory),
              "and only invited accounts join the select work den");

        /* a ban in one den does NOT leak into another (no shared membership state) */
        den_ban(den_fleet_get(&f, (uint32_t) lobby), op, mallory);
        CHECK(den_is_banned(den_fleet_get(&f, (uint32_t) lobby), mallory),
              "mallory is banned from the lobby");
        CHECK(!den_is_banned(den_fleet_get(&f, (uint32_t) family), mallory),
              "but that ban does NOT appear in the family den — dens are isolated (privacy)");

        /* each den keeps its own accountable log */
        CHECK(den_audit_verify(den_fleet_get(&f, (uint32_t) work)) &&
                  den_audit_length(den_fleet_get(&f, (uint32_t) work)) >= 2,
              "each den has its OWN moderation log");

        /* closing a den stops running it */
        CHECK(den_fleet_close(&f, (uint32_t) work) && den_fleet_count(&f) == 2 &&
                  den_fleet_get(&f, (uint32_t) work) == 0,
              "an operator can stop running a den; the rest keep going");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
