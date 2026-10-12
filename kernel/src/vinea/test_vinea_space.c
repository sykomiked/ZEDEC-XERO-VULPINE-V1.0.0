/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vinea_space.c — public spaces and the watch hall (vna_space.h P1-P7)
 * on simulated Vinea nodes: real ML-DSA-87 signatures, real DHT lookups, a
 * lossless in-memory network. Run by `make test-vinea-space`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vna_space.h"

static int failures = 0, passes = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            passes++;                                                                              \
        else {                                                                                     \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
        }                                                                                          \
    } while (0)

#define NP   7
#define POOL 64
typedef struct {
    vna_identity_t idn;
    vna_node_t node;
    vna_contact_t pool[POOL];
    vna_keycache_ent_t kc[16];
    vna_replay_ent_t rp[16];
    uint8_t dd[64][32];
    vna_store_slot_t store[48];
    vna_spaces_t sp;
    bool down; /* drops everything sent to it */
    uint32_t heard;
    uint8_t last_kind;
    char last_text[64];
} peer_t;

static peer_t *P;
static uint64_t g_now = 7000000;
static uint8_t *g_obuf;
static vna_outbox_t g_ob;
typedef struct {
    uint32_t from, to, len;
    uint8_t d[VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD];
} pkt_t;
#define QMAX 1024
static pkt_t *Q, *QB;
static uint32_t qn;

static void addr_of(uint32_t i, uint8_t a[4])
{
    a[0] = (uint8_t) i;
    a[1] = 0x5B;
    a[2] = 0xAC;
    a[3] = 0xE5;
}

static void flush(uint32_t from)
{
    for (uint32_t i = 0; i < g_ob.n; i++) {
        const vna_out_ent_t *e = &g_ob.e[i];
        if (e->addr_len != 4 || e->addr[1] != 0x5B || e->addr[0] >= NP || qn >= QMAX ||
            e->len > sizeof Q[0].d)
            abort(); /* the harness never drops a datagram silently */
        Q[qn].from = from;
        Q[qn].to = e->addr[0];
        Q[qn].len = e->len;
        memcpy(Q[qn].d, g_obuf + e->off, e->len);
        qn++;
    }
    vna_outbox_clear(&g_ob);
}

static void run(void)
{
    for (int guard = 0; qn && guard < 64; guard++) {
        uint32_t k = qn;
        memcpy(QB, Q, sizeof Q[0] * k);
        qn = 0;
        for (uint32_t i = 0; i < k; i++) {
            if (P[QB[i].to].down) continue;
            uint8_t a[4];
            addr_of(QB[i].from, a);
            g_now++;
            (void) vna_node_handle(&P[QB[i].to].node, QB[i].d, QB[i].len, a, 4, g_now, &g_ob);
            flush(QB[i].to);
        }
    }
}

static void tick(uint64_t dt)
{
    g_now += dt;
    for (uint32_t i = 0; i < NP; i++) {
        if (P[i].down) continue;
        vna_node_tick(&P[i].node, g_now, &g_ob);
        vna_spaces_tick(&P[i].sp, g_now);
        flush(i);
    }
    run();
}

/* tick until every listed peer is IN the space (or give up) */
static bool settle_in(const uint8_t *cid, const uint32_t *who, uint32_t n, uint32_t rounds)
{
    for (uint32_t r = 0; r < rounds; r++) {
        bool all = true;
        for (uint32_t k = 0; k < n; k++) {
            const vna_space_t *s = vna_space_get(&P[who[k]].sp, cid);
            all &= s && s->phase == VNA_SPACE_IN;
        }
        if (all) return true;
        tick(500);
    }
    return false;
}

static void heard(void *ctx, const vna_space_t *s, const vna_space_logent_t *e)
{
    peer_t *p = (peer_t *) ctx;
    (void) s;
    p->heard++;
    p->last_kind = e->kind;
    uint32_t n = e->len < sizeof p->last_text - 1 ? e->len : (uint32_t) sizeof p->last_text - 1;
    if (e->kind == VNA_SPACE_EV_POST) {
        memcpy(p->last_text, e->data, n);
        p->last_text[n] = 0;
    }
}

static void peer_up(uint32_t i)
{
    peer_t *p = &P[i];
    uint8_t seed[32] = {0};
    seed[0] = (uint8_t) (i + 1);
    seed[1] = 0x5B;
    if (vna_identity_create(&p->idn, seed, 0, 1) != VNA_OK) abort();
    vna_node_cfg_t cfg;
    vna_node_cfg_default(&cfg); /* degree ROUTE, as the hosted app runs */
    vna_node_mem_t mem = {p->pool, POOL, p->kc, 16, p->rp, 16, p->dd, 64, p->store, 48};
    seed[2] = 0x77;
    if (vna_node_init(&p->node, &p->idn, &cfg, &mem, seed) != VNA_OK) abort();
    seed[3] = 0x99;
    CHECK(vna_spaces_init(&p->sp, &p->node, &g_ob, seed) == VNA_OK);
    uint8_t a[4];
    addr_of(i, a);
    vna_spaces_set_addr(&p->sp, a, 4);
    vna_spaces_on_event(&p->sp, heard, p);
}

static const vna_space_logent_t *last_post(const vna_space_t *s)
{
    const vna_space_logent_t *ev[VNA_SPACE_LOG], *best = 0;
    uint32_t n = vna_space_events(s, 0, ev, VNA_SPACE_LOG);
    for (uint32_t i = 0; i < n; i++)
        if (ev[i]->kind == VNA_SPACE_EV_POST) best = ev[i];
    return best;
}

static uint32_t posts_in(const vna_space_t *s)
{
    const vna_space_logent_t *ev[VNA_SPACE_LOG];
    uint32_t n = vna_space_events(s, 0, ev, VNA_SPACE_LOG), k = 0;
    for (uint32_t i = 0; i < n; i++) k += ev[i]->kind == VNA_SPACE_EV_POST;
    return k;
}

static int post(uint32_t i, const uint8_t *cid, const char *text)
{
    int r = vna_space_post(&P[i].sp, cid, (const uint8_t *) text, (uint32_t) strlen(text), g_now);
    flush(i);
    run();
    return r;
}

static int hall(uint32_t i, const uint8_t *cid, uint8_t state, const char *media, uint64_t pos,
                uint32_t rate)
{
    int r = vna_space_hall_set(&P[i].sp, cid, state, (const uint8_t *) media,
                               media ? (uint32_t) strlen(media) : 0, pos, rate, g_now);
    flush(i);
    run();
    return r;
}

int main(void)
{
    size_t obcap = (size_t) (VNA_OUTBOX_MAX + 1u) * (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD);
    g_obuf = malloc(obcap);
    P = calloc(NP, sizeof *P);
    Q = malloc(sizeof *Q * QMAX);
    QB = malloc(sizeof *QB * QMAX);
    if (!g_obuf || !P || !Q || !QB) return 1;
    vna_outbox_init(&g_ob, g_obuf, (uint32_t) obcap);
    for (uint32_t i = 0; i < NP; i++) peer_up(i);
    for (uint32_t i = 1; i < NP; i++) {
        uint8_t a[4];
        addr_of(0, a);
        CHECK(vna_node_bootstrap(&P[i].node, a, 4, g_now, &g_ob) == VNA_OK);
        flush(i);
        run();
    }
    tick(10);
    for (uint32_t i = 1; i < NP; i++) CHECK(vna_rt_find(&P[0].node.rt, &P[i].idn.id) != 0);

    printf("[1] P1: the genesis block, its CID and the keys it gives\n");
    uint8_t film[VNA_CID_RAW_LEN];
    CHECK(vna_space_create(&P[0].sp, VNA_SPACE_HALL, VNA_GOV_OWNER, 0, "Film night", "film", g_now,
                           film) == VNA_OK);
    flush(0);
    run();
    const vna_space_t *a0 = vna_space_get(&P[0].sp, film);
    CHECK(a0 && a0->phase == VNA_SPACE_IN && a0->have_gen && vna_cid_is_raw(film));
    {
        uint8_t c2[VNA_CID_RAW_LEN];
        vna_space_genesis_t g;
        vna_cid_raw(a0->gen_bytes, a0->gen_len, c2);
        CHECK(!memcmp(c2, film, sizeof c2)); /* the CID names exactly the genesis bytes */
        CHECK(vna_space_genesis_decode(a0->gen_bytes, a0->gen_len, &g) == VNA_OK);
        CHECK(g.kind == VNA_SPACE_HALL && g.gov == VNA_GOV_OWNER && g.quorum_pct == 0);
        CHECK(vna_id_eq(&g.creator, &P[0].idn.id) && g.name_len == 10 && g.topic_len == 4);
        char txt[VNA_CID_STR_MAX];
        CHECK(vna_cid_to_string(film, txt, sizeof txt) > 0 && txt[0] == 'b');
        /* bad genesis fields are refused */
        uint8_t buf[VNA_SPACE_GEN_MAX];
        g.topic[0] = 'F';
        int32_t l = vna_space_genesis_encode(&g, buf, sizeof buf);
        CHECK(l > 0 && vna_space_genesis_decode(buf, (uint32_t) l, &g) == VNA_ERR_PARSE);
        vna_space_genesis_decode(a0->gen_bytes, a0->gen_len, &g);
        g.gov = VNA_GOV_QUORUM; /* quorum below 51% */
        g.quorum_pct = 50;
        l = vna_space_genesis_encode(&g, buf, sizeof buf);
        CHECK(l > 0 && vna_space_genesis_decode(buf, (uint32_t) l, &g) == VNA_ERR_PARSE);
        vna_id_t k0, k1, d0;
        vna_space_host_key(film, 0, &k0);
        vna_space_host_key(film, 1, &k1);
        vna_space_dir_key((const uint8_t *) "film", 4, 0, &d0);
        CHECK(!vna_id_eq(&k0, &k1) && !vna_id_eq(&k0, &d0));
    }
    CHECK(vna_space_create(&P[0].sp, VNA_SPACE_ROOM, VNA_GOV_OWNER, 0, "", "x", g_now, film) ==
          VNA_ERR_ARG);
    CHECK(vna_space_create(&P[0].sp, VNA_SPACE_ROOM, VNA_GOV_OWNER, 0, "Bad", "Caps", g_now,
                           (uint8_t[VNA_CID_RAW_LEN]){0}) == VNA_ERR_ARG);
    CHECK(P[0].sp.published == 2); /* host record and directory entry */
    tick(100);
    /* members' nodes keep space records for each other even at degree ROUTE */
    CHECK(P[0].node.space_store_cap > 0);

    printf("[2] P2/P4: a visitor with only the CID finds a host and joins\n");
    CHECK(vna_space_join(&P[1].sp, film, g_now) == VNA_OK);
    flush(1);
    {
        uint32_t who[] = {1};
        CHECK(settle_in(film, who, 1, 40));
    }
    const vna_space_t *b1 = vna_space_get(&P[1].sp, film);
    CHECK(b1 && b1->have_gen && !memcmp(b1->gen_bytes, a0->gen_bytes, a0->gen_len));
    CHECK(P[1].sp.hosts_found >= 1 && P[1].sp.welcomes_in == 1 && P[0].sp.joins_in == 1);
    CHECK(b1->nm == 1 && vna_id_eq(&b1->m[0].id, &P[0].idn.id));
    tick(500);
    CHECK(a0->nm == 1 && vna_id_eq(&a0->m[0].id, &P[1].idn.id));
    CHECK(P[1].sp.published == 2 && P[1].node.space_store_cap > 0); /* the visitor hosts too */
    CHECK(vna_space_join(&P[1].sp, film, g_now) == VNA_OK); /* already in: nothing new */

    printf("[3] P5: posts reach every member, once, signed by their author\n");
    CHECK(post(1, film, "hello from B") == VNA_OK);
    CHECK(P[0].heard == 1 && !strcmp(P[0].last_text, "hello from B"));
    {
        const vna_space_logent_t *l = last_post(a0);
        CHECK(l && vna_id_eq(&l->author, &P[1].idn.id) && l->len == 12);
    }
    CHECK(post(0, film, "welcome, B") == VNA_OK && !strcmp(P[1].last_text, "welcome, B"));
    CHECK(post(1, film, "") == VNA_ERR_ARG);
    CHECK(vna_space_post(&P[2].sp, film, (const uint8_t *) "x", 1, g_now) == VNA_ERR_ARG);

    printf("[4] P6: the watch hall: everyone computes the same playhead\n");
    CHECK(hall(0, film, VNA_HALL_STOP, "bafkreidemo-movie", 0, VNA_SPACE_Q16) == VNA_OK);
    CHECK(b1->hall_set && b1->hall.state == VNA_HALL_STOP && b1->hall.media_len == 17);
    CHECK(hall(0, film, VNA_HALL_PLAY, "bafkreidemo-movie", 0, VNA_SPACE_Q16) == VNA_OK);
    uint64_t t0 = g_now;
    CHECK(b1->hall.state == VNA_HALL_PLAY);
    tick(2000);
    uint64_t pa = vna_space_hall_pos(&a0->hall, g_now), pb = vna_space_hall_pos(&b1->hall, g_now);
    CHECK(pa == pb && pa >= 2000 && pa <= g_now - t0 + 1);
    /* double speed, paused, seek */
    CHECK(hall(0, film, VNA_HALL_PLAY, "bafkreidemo-movie", 10000, 2 * VNA_SPACE_Q16) == VNA_OK);
    uint64_t t1 = b1->hall.at_ms;
    CHECK(vna_space_hall_pos(&b1->hall, t1 + 1000) == 12000);
    CHECK(hall(0, film, VNA_HALL_PAUSE, "bafkreidemo-movie", 30000, VNA_SPACE_Q16) == VNA_OK);
    CHECK(vna_space_hall_pos(&b1->hall, g_now + 99999) == 30000);
    CHECK(hall(0, film, VNA_HALL_PLAY, "m", 0, 0) == VNA_ERR_ARG);              /* rate 0 */
    CHECK(hall(0, film, VNA_HALL_PLAY, "m", 0, 5 * VNA_SPACE_Q16) == VNA_ERR_ARG); /* > 4x */
    /* an owner space: only its creator controls playback */
    CHECK(!vna_space_may_control(b1, &P[1].idn.id) && vna_space_may_control(b1, &P[0].idn.id));
    CHECK(hall(1, film, VNA_HALL_PLAY, "bafkreidemo-movie", 0, VNA_SPACE_Q16) == VNA_ERR_DENIED);
    CHECK(a0->hall.state == VNA_HALL_PAUSE);

    printf("[5] P4: a newcomer gets the playback state and the recent posts\n");
    CHECK(vna_space_join(&P[2].sp, film, g_now) == VNA_OK);
    flush(2);
    {
        uint32_t who[] = {2};
        CHECK(settle_in(film, who, 1, 40));
    }
    const vna_space_t *c2 = vna_space_get(&P[2].sp, film);
    tick(200);
    CHECK(c2->hall_set && c2->hall.state == VNA_HALL_PAUSE && c2->hall.pos_ms == 30000);
    CHECK(posts_in(c2) == 2);
    CHECK(c2->nm == 2); /* the host, and the member the welcome named */
    tick(500);
    CHECK(b1->nm == 2 && a0->nm == 2); /* presence: everyone knows everyone */
    CHECK(post(2, film, "C is here") == VNA_OK);
    CHECK(!strcmp(P[0].last_text, "C is here") && !strcmp(P[1].last_text, "C is here"));

    printf("[6] P5: an event is applied once, and only a valid one\n");
    {
        uint32_t before = P[1].heard;
        const vna_space_logent_t *l = last_post(c2);
        /* C sends B its own post again: dedupe */
        CHECK(vna_node_send_space_at(&P[2].node, &P[1].idn.id, 0, 0, VNA_SPACE_M_EVENT, &c2->sid,
                                     l->rec, l->rec_len, g_now, &g_ob) == VNA_OK);
        flush(2);
        run();
        CHECK(P[1].heard == before);
        /* the same record for another space: refused */
        const vna_space_t *x = vna_space_get(&P[2].sp, film);
        vna_id_t other = x->sid;
        other.b[0] ^= 1;
        uint32_t ref = P[1].sp.refused;
        CHECK(vna_node_send_space_at(&P[2].node, &P[1].idn.id, 0, 0, VNA_SPACE_M_EVENT, &other,
                                     l->rec, l->rec_len, g_now, &g_ob) == VNA_OK);
        flush(2);
        run();
        CHECK(P[1].sp.refused == ref + 1);
        /* a tampered record: refused (the author's signature) */
        static uint8_t bad[VNA_REC_MAX];
        memcpy(bad, l->rec, l->rec_len);
        bad[60] ^= 0x40;
        uint32_t er = P[1].sp.events_refused;
        CHECK(vna_node_send_space_at(&P[2].node, &P[1].idn.id, 0, 0, VNA_SPACE_M_EVENT, &x->sid,
                                     bad, l->rec_len, g_now, &g_ob) == VNA_OK);
        flush(2);
        run();
        CHECK(P[1].sp.events_refused == er + 1 && P[1].heard == before);
    }
    /* B forges a playback change in A's owner space: refused by everyone */
    {
        vna_rec_t r;
        vna_space_ev_t e;
        vna_hall_t h;
        static uint8_t rec[VNA_REC_MAX];
        uint8_t hb[128], rnd[32] = {1};
        memset(&h, 0, sizeof h);
        h.state = VNA_HALL_PLAY;
        h.at_ms = g_now;
        h.rate_q16 = VNA_SPACE_Q16;
        memset(&e, 0, sizeof e);
        e.kind = VNA_SPACE_EV_HALL;
        e.lamport = 1000;
        int32_t hl = vna_schema_pack(&vna_hall_schema, &h, hb, sizeof hb, true);
        e.len = (uint16_t) hl;
        memcpy(e.data, hb, (size_t) hl);
        memset(&r, 0, sizeof r);
        r.rtype = VNA_REC_SPACE_EV;
        r.key = b1->sid;
        r.created = g_now;
        r.expires = g_now + 60000;
        int32_t pl = vna_schema_pack(&vna_space_ev_schema, &e, r.payload, VNA_PAYLOAD_MAX, true);
        r.payload_len = (uint16_t) pl;
        int32_t n = vna_rec_seal(&r, &P[1].idn, rnd, rec, sizeof rec);
        CHECK(n > 0);
        uint32_t er = P[0].sp.events_refused;
        CHECK(vna_node_send_space_at(&P[1].node, &P[0].idn.id, 0, 0, VNA_SPACE_M_EVENT, &b1->sid,
                                     rec, (uint32_t) n, g_now, &g_ob) == VNA_OK);
        flush(1);
        run();
        CHECK(P[0].sp.events_refused == er + 1 && a0->hall.state == VNA_HALL_PAUSE);
    }

    printf("[7] P5: welcomes only from the host asked, with the genesis the CID names\n");
    {
        uint32_t ref = P[3].sp.refused;
        /* D is not joining: a welcome is refused (and D has no such space) */
        CHECK(vna_node_send_space_at(&P[0].node, &P[3].idn.id, 0, 0, VNA_SPACE_M_WELCOME, &a0->sid,
                                     a0->gen_bytes, a0->gen_len, g_now, &g_ob) == VNA_OK);
        flush(0);
        run();
        CHECK(P[3].sp.refused == ref + 1 && !vna_space_get(&P[3].sp, film));
        /* a JOIN for a space the receiver is not in: refused */
        ref = P[4].sp.refused;
        CHECK(vna_node_send_space_at(&P[3].node, &P[4].idn.id, 0, 0, VNA_SPACE_M_JOIN, &a0->sid, 0,
                                     0, g_now, &g_ob) == VNA_OK);
        flush(3);
        run();
        CHECK(P[4].sp.refused == ref + 1);
    }

    printf("[8] P6: a quorum space lets any member steer; the latest change wins\n");
    uint8_t jam[VNA_CID_RAW_LEN];
    CHECK(vna_space_create(&P[3].sp, VNA_SPACE_HALL, VNA_GOV_QUORUM, 0, "Jam", "music", g_now,
                           jam) == VNA_OK);
    flush(3);
    run();
    CHECK(vna_space_get(&P[3].sp, jam)->gen.quorum_pct == 51);
    CHECK(vna_space_join(&P[4].sp, jam, g_now) == VNA_OK);
    flush(4);
    {
        uint32_t who[] = {4};
        CHECK(settle_in(jam, who, 1, 40));
    }
    const vna_space_t *e4 = vna_space_get(&P[4].sp, jam), *d3 = vna_space_get(&P[3].sp, jam);
    CHECK(vna_space_may_control(e4, &P[4].idn.id));
    CHECK(hall(4, jam, VNA_HALL_PLAY, "bafkreisong", 500, VNA_SPACE_Q16) == VNA_OK);
    CHECK(d3->hall_set && d3->hall.pos_ms == 500 && vna_id_eq(&d3->hall_author, &P[4].idn.id));
    CHECK(hall(3, jam, VNA_HALL_PAUSE, "bafkreisong", 900, VNA_SPACE_Q16) == VNA_OK);
    CHECK(e4->hall.state == VNA_HALL_PAUSE && e4->hall.pos_ms == 900);
    CHECK(e4->hall_lamport == d3->hall_lamport && e4->hall_lamport >= 2);
    CHECK(vna_space_create(&P[3].sp, VNA_SPACE_ROOM, VNA_GOV_QUORUM, 60, "Q", "", g_now, jam) ==
              VNA_OK &&
          vna_space_get(&P[3].sp, jam)->gen.quorum_pct == 60);
    /* rooms have no playhead */
    CHECK(hall(3, jam, VNA_HALL_PLAY, "x", 0, VNA_SPACE_Q16) == VNA_ERR_DENIED);

    printf("[9] P3: the directory lists spaces by topic\n");
    CHECK(vna_space_find(&P[5].sp, "film", g_now) == VNA_OK);
    for (int r = 0; r < 60 && !vna_space_find_done(&P[5].sp); r++) tick(200);
    CHECK(vna_space_find_done(&P[5].sp) && P[5].sp.nfound == 1);
    CHECK(!memcmp(P[5].sp.found[0].cid, film, VNA_CID_RAW_LEN));
    CHECK(P[5].sp.found[0].gen.name_len == 10 && !memcmp(P[5].sp.found[0].gen.name, "Film", 4));
    CHECK(vna_space_find(&P[5].sp, "nothing-here", g_now) == VNA_OK);
    for (int r = 0; r < 60 && !vna_space_find_done(&P[5].sp); r++) tick(200);
    CHECK(vna_space_find_done(&P[5].sp) && P[5].sp.nfound == 0);
    CHECK(vna_space_find(&P[5].sp, "Bad Topic", g_now) == VNA_ERR_ARG);

    printf("[10] P2: the space outlives its creator: visitors host it\n");
    CHECK(vna_space_leave(&P[0].sp, film, g_now) == VNA_OK);
    flush(0);
    run();
    CHECK(!vna_space_get(&P[0].sp, film) && P[1].sp.leaves_in >= 1);
    CHECK(P[0].node.space_store_cap == 0); /* in no space: keeps no space records */
    tick(500);
    CHECK(b1->nm == 1 && c2->nm == 1); /* A said goodbye */
    CHECK(vna_space_join(&P[5].sp, film, g_now) == VNA_OK);
    flush(5);
    {
        uint32_t who[] = {5};
        CHECK(settle_in(film, who, 1, 200));
    }
    const vna_space_t *f5 = vna_space_get(&P[5].sp, film);
    CHECK(f5 && f5->phase == VNA_SPACE_IN && !vna_id_eq(&f5->via, &P[0].idn.id));
    CHECK(f5->hall_set && f5->hall.state == VNA_HALL_PAUSE); /* the state lives on */

    printf("[11] P4: a member that goes silent is dropped\n");
    tick(1000);
    CHECK(b1->nm == 2 && c2->nm == 2);
    P[2].down = true;
    for (int r = 0; r < 12; r++) tick(10000);
    CHECK(b1->nm == 1 && vna_id_eq(&b1->m[0].id, &P[5].idn.id));
    P[2].down = false;

    printf("[12] the node keeps space records only while asked, never events\n");
    {
        uint8_t z[VNA_CID_RAW_LEN];
        CHECK(vna_space_create(&P[6].sp, VNA_SPACE_ROOM, VNA_GOV_OWNER, 0, "Solo", "", g_now, z) ==
              VNA_OK);
        flush(6);
        run();
        CHECK(P[6].sp.published == 1); /* no topic: no directory entry */
        vna_space_t *s = (vna_space_t *) vna_space_get(&P[6].sp, z);
        /* a SPACE record whose key its genesis does not give is refused */
        vna_rec_t r;
        vna_space_host_t h;
        static uint8_t rec[VNA_REC_MAX];
        uint8_t rnd[32] = {2};
        memset(&h, 0, sizeof h);
        h.gen_len = s->gen_len;
        memcpy(h.gen, s->gen_bytes, s->gen_len);
        memset(&r, 0, sizeof r);
        r.rtype = VNA_REC_SPACE;
        vna_space_host_key(film, 0, &r.key); /* another space's key */
        r.created = g_now;
        r.expires = g_now + 60000;
        r.payload_len =
            (uint16_t) vna_schema_pack(&vna_space_host_schema, &h, r.payload, VNA_PAYLOAD_MAX, true);
        int32_t n = vna_rec_seal(&r, &P[6].idn, rnd, rec, sizeof rec);
        vna_rec_t out;
        CHECK(n > 0 && vna_rec_verify(&P[6].node.v, rec, (uint32_t) n, g_now, 30000, &out) ==
                           VNA_ERR_PARSE);
        vna_space_host_key(z, 3, &r.key);
        n = vna_rec_seal(&r, &P[6].idn, rnd, rec, sizeof rec);
        CHECK(n > 0 &&
              vna_rec_verify(&P[6].node.v, rec, (uint32_t) n, g_now, 30000, &out) == VNA_OK);
        CHECK(vna_space_leave(&P[6].sp, z, g_now) == VNA_OK);
        CHECK(vna_space_leave(&P[6].sp, z, g_now) == VNA_ERR_ARG);
        /* an event record is never stored, even at degree STORE */
        const vna_space_logent_t *l = last_post(b1);
        CHECK(l != 0);
        P[6].node.cfg.degree = VNA_DEG_STORE;
        uint64_t ttl = P[6].node.cfg.rec_max_ttl_ms;
        P[6].node.cfg.rec_max_ttl_ms = UINT64_MAX; /* refused for its type, not its lifetime */
        uint32_t ok = P[6].node.stores_ok, bad = P[6].node.stores_refused;
        vna_b_rec_t br;
        br.len = l->rec_len;
        memcpy(br.rec, l->rec, l->rec_len);
        /* P1 sends P6 a STORE carrying the event, by hand */
        static vna_msg_t m;
        static uint8_t wire[VNA_MSG_WIRE_MAX];
        memset(&m, 0, sizeof m);
        m.type = VNA_MSG_STORE;
        m.dst = P[6].idn.id;
        m.seq = ++P[1].node.seq;
        m.ts = g_now;
        m.rpc = 77;
        m.body_len = (uint16_t) vna_schema_pack(&vna_b_rec_schema, &br, m.body, VNA_BODY_MAX, true);
        uint8_t r32[32] = {9};
        int32_t wl = vna_msg_seal(&m, &P[1].idn, r32, false, wire, sizeof wire);
        CHECK(wl > 0);
        uint8_t a1[4];
        addr_of(1, a1);
        (void) vna_node_handle(&P[6].node, wire, (uint32_t) wl, a1, 4, g_now, &g_ob);
        CHECK(P[6].node.stores_ok == ok && P[6].node.stores_refused == bad + 1);
        /* and its author cannot publish it into the DHT either */
        uint32_t au = 0;
        while (au < NP && !vna_id_eq(&P[au].idn.id, &l->author)) au++;
        CHECK(au < NP && vna_node_publish(&P[au].node, l->rec, l->rec_len, g_now, &g_ob) ==
                             VNA_ERR_DENIED);
        P[6].node.cfg.degree = VNA_DEG_ROUTE;
        P[6].node.cfg.rec_max_ttl_ms = ttl;
        vna_outbox_clear(&g_ob);
    }

    printf("[13] bounded tables\n");
    {
        uint8_t c[VNA_CID_RAW_LEN];
        int made = 0;
        while (vna_space_create(&P[6].sp, VNA_SPACE_ROOM, VNA_GOV_OWNER, 0, "R", "", g_now, c) ==
               VNA_OK)
            made++;
        CHECK(made == (int) VNA_SPACE_MAX);
        CHECK(vna_space_join(&P[6].sp, film, g_now) == VNA_ERR_SPACE);
        flush(6);
        run();
    }

    printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
