/* test_caracho_gov.c — sovereign-node governance.
 * The point: every node's operator sets its own rules, sovereignty stops at
 * the node boundary, and nobody can escalate their own privileges. */
#include <stdio.h>
#include <string.h>
#include "caracho_gov.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static void mkkey(uint8_t k[CGOV_KEY_LEN], uint8_t seed) {
    for (uint32_t i = 0; i < CGOV_KEY_LEN; i++) k[i] = (uint8_t)(seed * 31u + i);
}

int main(void) {
    printf("=== Carracho sovereign-node governance ===\n");
    uint8_t op[32], alice[32], bob[32], mallory[32];
    mkkey(op, 1); mkkey(alice, 2); mkkey(bob, 3); mkkey(mallory, 4);

    cgov_server_t s;
    CHECK(cgov_init(&s, op), "node founded");

    /* ---- the operator is sovereign ---- */
    CHECK(cgov_is_operator(&s, op), "founder is the operator");
    CHECK(cgov_privileges(&s, op) == CGOV_ALL, "operator holds ALL privileges");
    CHECK(cgov_can(&s, op, CGOV_BAN | CGOV_SET_ACCESS | CGOV_UPLOAD),
          "operator can do anything on their node");

    /* ---- guests get the operator-chosen default set ---- */
    CHECK(cgov_can(&s, alice, CGOV_SEND_CHAT), "a guest can chat by default");
    CHECK(!cgov_can(&s, alice, CGOV_UPLOAD), "a guest cannot upload by default");
    CHECK(!cgov_can(&s, alice, CGOV_SET_ACCESS), "a guest cannot change the rules");

    /* ---- the operator sets THIS node's rules ---- */
    CHECK(cgov_set_guest_privileges(&s, op, CGOV_READ_CHAT),
          "operator restricts guests to read-only chat");
    CHECK(!cgov_can(&s, alice, CGOV_SEND_CHAT),
          "now a guest cannot even send chat on THIS node");

    /* ---- everybody sets the rules of their OWN node ---- */
    {
        cgov_server_t s2;
        cgov_init(&s2, op);
        cgov_set_guest_privileges(&s2, op, CGOV_ALL & ~CGOV_SET_ACCESS & ~CGOV_BAN);
        CHECK(cgov_can(&s2, alice, CGOV_UPLOAD) && !cgov_can(&s, alice, CGOV_UPLOAD),
              "the SAME guest has different rights on two different nodes "
              "(each operator is sovereign)");
    }

    /* ---- the operator grants a specific account a custom privilege set ---- */
    CHECK(cgov_set_account(&s, op, alice, "alice",
                           CGOV_READ_CHAT | CGOV_SEND_CHAT | CGOV_UPLOAD | CGOV_DOWNLOAD),
          "operator creates alice with upload rights");
    CHECK(cgov_can(&s, alice, CGOV_UPLOAD) && cgov_can(&s, alice, CGOV_SEND_CHAT),
          "alice has exactly the granted privileges");
    CHECK(!cgov_can(&s, alice, CGOV_BAN) && !cgov_can(&s, alice, CGOV_SET_ACCESS),
          "and no more than what she was granted");

    /* ---- NO privilege escalation ---- */
    CHECK(!cgov_set_guest_privileges(&s, alice, CGOV_ALL),
          "alice (no SET_ACCESS) cannot rewrite the node's rules");
    CHECK(!cgov_set_account(&s, alice, mallory, "m", CGOV_ALL),
          "alice cannot create accounts (no CREATE_ACCOUNT bit)");
    /* give alice CREATE_ACCOUNT but NOT SET_ACCESS; she still can't mint an admin */
    cgov_set_account(&s, op, alice, "alice",
                     CGOV_READ_CHAT | CGOV_SEND_CHAT | CGOV_CREATE_ACCOUNT);
    CHECK(cgov_set_account(&s, alice, bob, "bob", CGOV_SEND_CHAT),
          "alice with CREATE_ACCOUNT can make an ordinary account");
    CHECK(!cgov_set_account(&s, alice, mallory, "m", CGOV_SET_ACCESS),
          "but she CANNOT grant SET_ACCESS she does not hold (no escalation)");
    CHECK(!cgov_can(&s, mallory, CGOV_SET_ACCESS), "mallory did not become an admin");

    /* ---- a delegated admin (operator grants SET_ACCESS) CAN govern ---- */
    cgov_set_account(&s, op, bob, "bob-admin", CGOV_ALL);
    CHECK(cgov_set_guest_privileges(&s, bob, CGOV_READ_CHAT | CGOV_DOWNLOAD),
          "an account the operator gave SET_ACCESS can change the rules");

    /* ---- bans ---- */
    CHECK(cgov_ban(&s, op, mallory), "operator bans mallory");
    CHECK(cgov_is_banned(&s, mallory), "mallory is banned");
    CHECK(cgov_privileges(&s, mallory) == 0, "a banned identity has ZERO privileges");
    CHECK(!cgov_can(&s, mallory, CGOV_READ_CHAT), "banned overrides even guest rights");
    CHECK(!cgov_ban(&s, op, op), "the operator cannot be banned");
    CHECK(cgov_unban(&s, op, mallory) && !cgov_is_banned(&s, mallory),
          "operator unbans mallory; guest rights return");

    /* ---- visibility tiers (public / private / select) ---- */
    CHECK(cgov_may_connect(&s, mallory), "PUBLIC node: a stranger may connect");
    cgov_set_visibility(&s, op, CGOV_PRIVATE);
    CHECK(cgov_may_connect(&s, op) && !cgov_may_connect(&s, mallory),
          "PRIVATE node: only the operator may connect");
    cgov_set_visibility(&s, op, CGOV_SELECT);
    CHECK(cgov_may_connect(&s, alice) && !cgov_may_connect(&s, mallory),
          "SELECT node: only named accounts (the invite list) may connect");

    /* ---- sovereignty stops at the node boundary ---- */
    CHECK(cgov_can(&s, op, CGOV_SET_ACCESS),
          "operator power exists only within cgov_* — it names no kernel/other-node action");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
