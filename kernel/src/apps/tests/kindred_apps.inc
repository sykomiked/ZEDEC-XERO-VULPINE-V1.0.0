/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kindred's model against zxvd's real dispatcher (LINK): contacts written
 * with files.write into the test's scratch Hoard and read back, cards
 * checked by web4.card_verify on a card zxvd made from a test seed, the
 * peers from net.peers through view ops that list the two below (and none
 * set: networking off). chat.* is served in the daemon by zxvd_chat.c,
 * which this test does not link (the dispatcher answers -32601), so
 * chat.me's answer is made up in its documented shape (FAKE). */
#include "kindred_model.h"

#define KD_ID_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define KD_ID_B "bb00000000000000000000000000000000000000000000000000000000000002"
#define KD_ME   "cc00000000000000000000000000000000000000000000000000000000000003"
#define KD_PEER(id, member)                                                                        \
    "{\"id\":\"" id "\",\"addr\":\"192.168.1.7:8723\",\"bucket\":3,\"ubh\":true,"                  \
    "\"last_seen_ms\":\"1700000000123\",\"in_book\":false,\"member\":" #member ",\"held\":\"0\","  \
    "\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}"

static const char *KD_PEERS;

static void kd_peers(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, KD_PEERS, strlen(KD_PEERS));
}

/* the numbers of the contacts the last search found, in order */
static bool kd_hits(const kindred_t *k, const int64_t *want, uint32_t n)
{
    if (k->nhits != n) return false;
    for (uint32_t i = 0; i < n; i++)
        if (k->c[k->hit[i]].contact != want[i]) return false;
    return true;
}

APPS_TEST(kindred)
{
    static kindred_t KD, KD2;
    static char long_name[KINDRED_NAME_MAX + 2], long_note[KINDRED_NOTE_MAX + 2];
    static char pk[KINDRED_PK_HEX + 1], sig[KINDRED_SIG_HEX + 1], id[KINDRED_HEX_ID + 1];
    static const zxvd_view_ops_t VIEW = {kd_peers, NULL, NULL};
    int64_t n1 = 0, n2 = 0, n3 = 0, n4 = 0, nx = 0;
    memset(long_name, 'x', KINDRED_NAME_MAX + 1);
    memset(long_note, 'y', KINDRED_NOTE_MAX + 1);

    /* the rules (KR1-KR7) */
    int64_t len = 0;
    CHECK(kindred_fields_ok(1, 15, 254, 1024) && !kindred_fields_ok(129, 0, 0, 0) &&
              kindred_name_code("Ada", 3, &len) == 10401000000000000 && len == 3 &&
              kindred_prefix_hit(10401000000000000, 10400000000000000, 2) == 1 &&
              kindred_prefix_hit(10401000000000000, 10500000000000000, 2) == 0 &&
              kindred_record_ok(1, 0, 0, 1, 0, 1) && !kindred_index_ok(1, 1000000, 1) &&
              kindred_id_code(KD_ID_A) == INT64_C(0xaa0000000000) + 1,
          "kindred: the rules");

    /* KM1, KM2: nothing before the load, and fields past their limits never sent */
    kindred_init(&KD, &LINK);
    int before = sent;
    CHECK(kindred_add(&KD, "Ada", "", "", "", 1, &nx) == ZXM_E_STATE &&
              kindred_add(&KD, "", "", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, long_name, "", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "1234567890123456", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "12-34", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "+", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "ada@", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "a b@c", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "a@b@c", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "", long_note, 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "", "two\nlines", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "A\xc3", "", "", "", 1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, "Ada", "", "", "", -1, &nx) == ZXM_E_ARG &&
              kindred_add(&KD, NULL, "", "", "", 1, &nx) == ZXM_E_ARG && sent == before,
          "kindred: not loaded, or a field past its limit: nothing sent");
    CHECK(kindred_load(&KD) == ZXM_OK && KD.loaded && KD.n == 0 && !KD.folder,
          "kindred: nothing kept yet: no contacts: %s", LINK.err);

    /* KM1: four contacts kept in the Hoard */
    CHECK(kindred_add(&KD, "Ada Lovelace", "+442071234567", "ada@example.org",
                      "Met at the engine works", 1792627200000, &n1) == ZXM_OK &&
              n1 == 1 && KD.n == 1 && KD.folder,
          "kindred: the first contact: %s", LINK.err);
    CHECK(kindred_add(&KD, "Adaline Byron", "", "", "", 1792627200001, &n2) == ZXM_OK &&
              kindred_add(&KD, "ada lovelace", "07700900123", "", "Same person?", 1792627200002,
                          &n3) == ZXM_OK &&
              kindred_add(&KD, "\xc3\x89mile Bob", "", "emile@example.org", "", 1792627200003,
                          &n4) == ZXM_OK &&
              n2 == 2 && n3 == 3 && n4 == 4 && KD.n == 4 && KD.c[3].name_code == 371309120502150200,
          "kindred: three more, numbered in order: %s", LINK.err);

    /* KM6: the name-prefix search (KR3) */
    static const int64_t all[4] = {1, 2, 3, 4}, ada[3] = {1, 2, 3}, adali[1] = {2}, emi[1] = {4};
    CHECK(kindred_search(&KD, "") == ZXM_OK && kd_hits(&KD, all, 4) &&
              kindred_search(&KD, "ada") == ZXM_OK && kd_hits(&KD, ada, 3) &&
              kindred_search(&KD, "ADA L") == ZXM_OK && kd_hits(&KD, ada, 3) &&
              kindred_search(&KD, "adali") == ZXM_OK && kd_hits(&KD, adali, 1) &&
              kindred_search(&KD, "\xc3\xa9mi") == ZXM_OK && kd_hits(&KD, emi, 1) &&
              kindred_search(&KD, "zz") == ZXM_OK && KD.nhits == 0 &&
              kindred_search(&KD, "A\xc3") == ZXM_E_ARG && kindred_search(&KD, NULL) == ZXM_E_ARG,
          "kindred: search by what the name begins with");

    /* KM6: likely duplicates (KR4): 1 and 3 have one name code */
    CHECK(kindred_find_dupes(&KD) == ZXM_OK && KD.dupes[0] == 1 && KD.dupes[1] == 0 &&
              KD.dupes[2] == 1 && KD.dupes[3] == 0,
          "kindred: the same name twice: likely duplicates, nothing merged");

    /* KM3: a card kept only when zxvd says it verifies */
    char seed[65];
    memset(seed, '5', 64);
    seed[64] = 0;
    zxm_begin(&LINK, "web4.agent_card");
    zxm_p_str(&LINK, "seed_hex", seed);
    zxm_p_str(&LINK, "name", "kindred-1");
    zxm_p_u64(&LINK, "bound_ms", 1700000000000u);
    CHECK(zxm_call(&LINK) == ZXM_OK && !zxm_get_str(&LINK, LINK.result, "pk_hex", pk, sizeof pk) &&
              !zxm_get_str(&LINK, LINK.result, "bind_sig_hex", sig, sizeof sig) &&
              !zxm_get_str(&LINK, LINK.result, "agent_id", id, sizeof id),
          "kindred: a card to check: %s", LINK.err);
    kindred_card_t card = {"kindred-1", 1700000000000u, pk, sig};
    CHECK(kindred_card(&KD, n4, &card, 1792627300000) == ZXM_OK && !strcmp(KD.c[3].card, id) &&
              KD.c[3].verified == 1 && KD.c[3].card_code == kindred_id_code(id) &&
              KD.c[3].stamp == 1792627300000 && !KD.notice[0],
          "kindred: the card verifies and is kept: %s", LINK.err);
    card.name = "kindred-2";
    CHECK(kindred_card(&KD, n2, &card, 1792627300001) == ZXM_OK && !KD.c[1].card[0] &&
              KD.c[1].verified == 0 && strstr(KD.notice, "does not verify"),
          "kindred: another name: not kept (%s)", KD.notice);
    card.name = "kindred-1";
    CHECK(kindred_card(&KD, n2, &card, 1792627300002) == ZXM_OK && !strcmp(KD.c[1].card, id) &&
              kindred_find_dupes(&KD) == ZXM_OK && KD.dupes[1] == 1 && KD.dupes[3] == 1 &&
              KD.dupes[0] == 1,
          "kindred: one card on two contacts: likely duplicates");
    before = sent;
    card.name = "Kindred";
    CHECK(kindred_card(&KD, n2, &card, 1) == ZXM_E_ARG, "kindred: an upper-case card name");
    card.name = "kindred-1";
    card.pk_hex = "00";
    CHECK(kindred_card(&KD, n2, &card, 1) == ZXM_E_ARG &&
              kindred_card(&KD, n2, NULL, 1) == ZXM_E_ARG && sent == before,
          "kindred: a key of the wrong length: nothing sent");
    card.pk_hex = pk;
    CHECK(kindred_card(&KD, 99, &card, 1) == ZXM_E_ARG && sent == before,
          "kindred: no such contact: nothing sent");

    /* KM4: the node only from the peers zxvd has verified */
    zxvd_rpc_set_view_ops(NULL);
    CHECK(kindred_peers(&KD) == ZXM_E_REMOTE && LINK.err_code == -32601 && KD.npeers == 0,
          "kindred: no networking: zxvd says so: %s", LINK.err);
    CHECK(kindred_node(&KD, n1, KD_ID_A, 1792627400000) == ZXM_E_STATE && !KD.c[0].node[0] &&
              strstr(KD.notice, "not among the peers"),
          "kindred: no peers read: no node linked");
    KD_PEERS = "{\"peers\":[" KD_PEER(KD_ID_A, true) "," KD_PEER(KD_ID_B, false) "],\"count\":2}";
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(kindred_peers(&KD) == ZXM_OK && KD.npeers == 2 && !strcmp(KD.peer[0].id, KD_ID_A) &&
              KD.peer[0].member && !KD.peer[1].member && KD.peer[0].last_seen_ms == 1700000000123u,
          "kindred: two peers: %s", LINK.err);
    CHECK(kindred_node(&KD, n1, KD_ID_A, 1792627400000) == ZXM_OK &&
              !strcmp(KD.c[0].node, KD_ID_A) && KD.c[0].node_code == kindred_id_code(KD_ID_A) &&
              kindred_node(&KD, n3, KD_ME, 1792627400001) == ZXM_E_STATE && !KD.c[2].node[0] &&
              kindred_node(&KD, n3, "AA00", 1) == ZXM_E_ARG,
          "kindred: a peer's node is linked, any other is not: %s", LINK.err);
    zxvd_rpc_set_view_ops(NULL);

    /* KM5: the owner's chat identity */
    CHECK(kindred_me(&KD) == ZXM_E_REMOTE && LINK.err_code == -32601 && !KD.me[0],
          "kindred: chat.me is not linked here: -32601 (%s)", LINK.err);
    KD.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"node_id\":\"" KD_ME
             "\",\"saved\":true,\"records\":2,\"following\":[\"" KD_ID_A "\"]}}";
    CHECK(kindred_me(&KD) == ZXM_OK && !strcmp(KD.me, KD_ME) && KD.nfollowing == 1 &&
              kindred_followed(&KD, n1) && !kindred_followed(&KD, n2) && !kindred_followed(&KD, 99),
          "kindred: the owner, and the one contact followed: %s", FAKE.err);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"node_id\":\"abc\",\"following\":[]}}";
    CHECK(kindred_me(&KD) == ZXM_E_PROTOCOL && !strcmp(KD.me, KD_ME) && KD.nfollowing == 1,
          "kindred: a short NodeID refused, the owner kept");
    CANNED = NULL;
    KD.link = &LINK;

    /* KM1: an edit, kept on disk */
    CHECK(kindred_edit(&KD, n2, "Adelaide Byron", "", "adelaide@example.org", "", 1792627500000) ==
                  ZXM_OK &&
              !strcmp(KD.c[1].name, "Adelaide Byron") && !strcmp(KD.c[1].card, id) &&
              kindred_search(&KD, "adal") == ZXM_OK && KD.nhits == 2 &&
              kindred_edit(&KD, 99, "X", "", "", "", 1) == ZXM_E_ARG &&
              kindred_edit(&KD, n2, "", "", "", "", 1) == ZXM_E_ARG,
          "kindred: an edit: %s", LINK.err);

    /* KM1: read back by another model */
    kindred_init(&KD2, &LINK);
    const kindred_contact_t *c1, *c2, *c4;
    CHECK(kindred_load(&KD2) == ZXM_OK && KD2.n == 4 && KD2.last == 4 &&
              (c1 = kindred_get(&KD2, n1)) != NULL && !strcmp(c1->name, "Ada Lovelace") &&
              !strcmp(c1->phone, "+442071234567") && !strcmp(c1->mail, "ada@example.org") &&
              !strcmp(c1->note, "Met at the engine works") && !strcmp(c1->node, KD_ID_A) &&
              (c2 = kindred_get(&KD2, n2)) != NULL && !strcmp(c2->name, "Adelaide Byron") &&
              c2->verified == 1 && !strcmp(c2->card, id) && (c4 = kindred_get(&KD2, n4)) != NULL &&
              !strcmp(c4->name, "\xc3\x89mile Bob") && c4->stamp == 1792627300000,
          "kindred: the contacts read back: %u %s", KD2.n, LINK.err);

    /* KM1: removed by the owner, into the Trash */
    CHECK(kindred_remove(&KD2, n3) == ZXM_OK && KD2.n == 3 && !kindred_get(&KD2, n3) &&
              kindred_remove(&KD2, n3) == ZXM_E_ARG,
          "kindred: a contact removed: %s", LINK.err);
    CHECK(kindred_load(&KD) == ZXM_OK && KD.n == 3 && !kindred_get(&KD, n3) &&
              kindred_add(&KD, "Grace", "", "", "", 1792627600000, &nx) == ZXM_OK && nx == 5,
          "kindred: read back without it, and the numbering goes on: %s", LINK.err);

    /* an index that is not one is refused, and the contacts kept stay */
    uint64_t size = 0;
    CHECK(zxf_write(KINDRED_INDEX, (const uint8_t *) "1 0 0 7 0 1\n", 12, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              kindred_load(&KD) == ZXM_E_PROTOCOL && KD.n == 4,
          "kindred: a name code that is not the contact's is no index");
    CHECK(zxf_write(KINDRED_INDEX, (const uint8_t *) "9 0 0 0 0 1\n", 12, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              kindred_load(&KD) == ZXM_E_PROTOCOL,
          "kindred: an index naming a contact not kept is no index");
    CHECK(zxf_write(KINDRED_INDEX, (const uint8_t *) "1 0 0 0 2 1\n", 12, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              kindred_load(&KD) == ZXM_E_PROTOCOL && KD.n == 4,
          "kindred: a verified flag of 2 is no record");
}
