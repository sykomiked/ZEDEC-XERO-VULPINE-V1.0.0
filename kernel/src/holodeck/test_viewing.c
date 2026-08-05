/* test_viewing.c — a viewing governed by its den.
 * The point: the den's EXISTING rules govern the swarm; no second permission
 * system exists, so there is no second one to get wrong. */
#include <stdio.h>
#include <string.h>
#include "viewing.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static void mkkey(uint8_t k[DEN_KEY_LEN], uint8_t seed) {
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) k[i] = (uint8_t)(seed * 31u + i);
}

int main(void) {
    printf("=== a Holodeck viewing, governed by its den ===\n");
    uint8_t op[32], seeder[32], viewer[32], stranger[32], banned[32];
    mkkey(op,1); mkkey(seeder,2); mkkey(viewer,3); mkkey(stranger,4); mkkey(banned,5);

    den_server_t den; den_init(&den, op);
    /* guests may watch and chat but NOT seed */
    den_set_guest_privileges(&den, op, DEN_READ_CHAT | DEN_SEND_CHAT | DEN_DOWNLOAD);
    /* one account is granted upload rights */
    den_set_account(&den, op, seeder, "seeder", DEN_DOWNLOAD | DEN_UPLOAD | DEN_SEND_CHAT);
    den_ban(&den, op, banned);

    uint8_t root[32]; for (int i=0;i<32;i++) root[i]=(uint8_t)(i*3);
    viewing_t v;
    CHECK(viewing_open(&v, &den, root, 8, 1000), "a viewing opens inside the den");

    /* ---- the den's rules govern who may watch ---- */
    CHECK(viewing_may_watch(&v, viewer),  "a guest with DOWNLOAD may watch");
    CHECK(viewing_may_watch(&v, seeder),  "the seeder may watch");
    CHECK(!viewing_may_watch(&v, banned), "a BANNED identity cannot watch");

    /* ---- upload rights are separate from watch rights ---- */
    CHECK(viewing_may_seed(&v, seeder),  "the seeder may serve chunks");
    CHECK(!viewing_may_seed(&v, viewer), "a guest may watch but NOT seed");
    CHECK(viewing_may_speak(&v, viewer), "a guest may talk in the room");

    /* ---- joining credits upload ONLY for permitted seeders ---- */
    CHECK(viewing_join(&v, seeder, 1, 500) >= 0, "the seeder joins");
    CHECK(viewing_capacity_kbps(&v) == 1500, "and its 500 kbps IS credited");
    CHECK(viewing_join(&v, viewer, 2, 500) >= 0, "a guest joins to watch");
    CHECK(viewing_capacity_kbps(&v) == 1500,
          "but its upload is NOT credited — a spectator is never silently "
          "conscripted as a seeder");
    CHECK(viewing_join(&v, banned, 3, 500) < 0, "a banned identity is refused entry");

    /* ---- a PRIVATE den hides its viewing entirely ---- */
    {
        den_server_t priv; den_init(&priv, op);
        den_set_visibility(&priv, op, DEN_PRIVATE);
        viewing_t pv; viewing_open(&pv, &priv, root, 8, 1000);
        CHECK(viewing_may_watch(&pv, op), "the operator may watch their private viewing");
        CHECK(!viewing_may_watch(&pv, stranger),
              "a stranger cannot watch a PRIVATE den's viewing (the den's "
              "visibility tier already decided this)");
    }

    /* ---- a SELECT den is invite-only ---- */
    {
        den_server_t sel; den_init(&sel, op);
        den_set_visibility(&sel, op, DEN_SELECT);
        den_set_account(&sel, op, viewer, "invited", DEN_DOWNLOAD);
        viewing_t sv; viewing_open(&sv, &sel, root, 8, 1000);
        CHECK(viewing_may_watch(&sv, viewer), "an invited account may watch");
        CHECK(!viewing_may_watch(&sv, stranger), "an uninvited stranger may not");
    }

    /* ---- revoking access mid-viewing is recorded ---- */
    {
        uint32_t before = den_audit_length(&den);
        den_ban(&den, op, viewer);
        CHECK(!viewing_may_watch(&v, viewer), "revoked access takes effect immediately");
        CHECK(den_audit_length(&den) == before + 1 && den_audit_verify(&den),
              "and the revocation is recorded in the den's tamper-evident log");
    }

    /* ---- the swarm's physics are unchanged by access control ---- */
    {
        den_server_t open_den; den_init(&open_den, op);
        den_set_guest_privileges(&open_den, op, DEN_DOWNLOAD | DEN_UPLOAD);
        viewing_t ov; viewing_open(&ov, &open_den, root, 8, 1000);
        uint32_t caps[4];
        for (uint32_t i = 1; i <= 3; i++) {
            uint8_t k[32]; mkkey(k, (uint8_t)(50+i));
            viewing_join(&ov, k, i, 400);
            caps[i] = viewing_capacity_kbps(&ov);
        }
        CHECK(caps[3] == 1000 + 3*400,
              "in a den where everyone may seed, capacity still RISES with each "
              "viewer — access control changes who participates, not the physics");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
