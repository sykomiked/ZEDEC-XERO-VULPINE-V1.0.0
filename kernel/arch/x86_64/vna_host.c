/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_host.c — the bare-metal Vinea host; see vna_host.h. */

#include "vna_host.h"
#include "uart_16550.h"
#include "robin_debanks/sha256.h"
#include "robin_debanks/ed25519_verify.h"
#include "seedbank/catalog.h"
#include "cisex_idx/cisex_rt.h"
#include "vinea/vna_file.h"
#include "vinea/vna_session.h"
#include "vinea/vna_exchange.h"
#include "smic/smic_core.h"
#include "smic/smic_verify.h"
#include "scp144/scp144.h"
#include "porter_house/porter_house.h"
#include "immigration/immigration.h"
#include "ipfs_node/ipfs_node.h"
#include "aipi/aipi.h"
#include "aipi/aipi_vna.h"
#include "timesvc/sntp.h"
#include "timesvc/timesvc.h"
#include "event_space/event_space.h"
#include "ipfs_node/ipfs_node.h"
#include "swarm/swarm_denom.h"
#include "chiglet/chiglet.h"  /* the node's evidence judge (ISF mixture) */
#include "chiglet/chg_kin.h"  /* the chiglet's social layer: DNA, hobbies, visits */
#include "chiglet/chg_bond.h" /* its habit model of this node's own rhythms */
#include "oseq/oseq.h"        /* causal ordering + replay detection */
#include "tensor/zt_gguf.h"
#include "tensor/zt_model.h"
#include "tensor/zt_tok.h"
#include "zxv_doorway_blobs.h"   /* the doorway files, generated into the build */
#include "zxv_curriculum_blob.h" /* the deterministic curriculum CAR, generated */

/* The door's file table: the committed doorway files as compiled-in blobs
 * (boot.json and boot.pack are seal-time artifacts — the browser fetches
 * them from the gateways cross-origin, so the seed needs only these). */
static int door_path(const char *path, uint32_t plen, const zxv_door_file_t **out)
{
    uint32_t i, j;
    for (i = 0; i < ZXV_DOOR_FILE_N; i++) {
        const zxv_door_file_t *f = &zxv_door_files[i];
        for (j = 0; j < plen && f->path[j] && path[j] == f->path[j]; j++);
        if (j == plen && f->path[j] == 0) {
            *out = f;
            return 1;
        }
    }
    return 0;
}

static int is_boot_src(const char *path, uint32_t plen)
{
    static const char bs[] = "/boot.src";
    uint32_t i;
    if (plen != sizeof bs - 1) return 0;
    for (i = 0; i < plen; i++)
        if (path[i] != bs[i]) return 0;
    return 1;
}

static const char g_curriculum_cid[] =
    "bafybeihgn75im5323qabyicaba64w7trb6irsrd32z3v4quj6qhoifvvt4\n";

static int is_curriculum_cid(const char *path, uint32_t plen)
{
    static const char cc[] = "/curriculum.cid";
    uint32_t i;
    if (plen != sizeof cc - 1) return 0;
    for (i = 0; i < plen; i++)
        if (path[i] != cc[i]) return 0;
    return 1;
}

/* The CISEX delegated-routing index this seed answers for (cisex_rt.h):
 * bounded like the host itself, one instance per machine. The door's POST
 * /cisex route is the :80 ingress; the same responder is the call a UDP
 * wrapper would make. Its probe slot is the seed's epistemic shard. */
#define VNAH_CISEX_N 65536u
static cisex_ent_t g_cisex_ents[VNAH_CISEX_N];
static cisex_t g_cisex;
static uint8_t g_cisex_region;
static uint32_t g_car_indexed; /* CAR sections harvested into the index */

/* The machine-integrity ring (smic_core.h) and the stateless UBH-168
 * exchange order book (vna_exchange.h): like the CISEX index, one bounded
 * instance per machine, filled by envelopes the fleet propagates as
 * VNA_REC_ENV DHT records and by POST /smic through the door. */
#define VNAH_SMIC_N 16u
static smic_node_t g_smic_ents[VNAH_SMIC_N];
static smic_t g_smic;
static vna_exchange_t g_exch;
static smic_hw_t g_self_hw;
static uint8_t g_cur_digest[32];
/* The causal registry: envelope dedup rides its 128-entry replay window,
 * and each admitted record is a registered event so happens-before and
 * causal-cycle queries exist over what the node has seen. */
static oseq_registry_t g_oseq;
static int32_t g_oseq_vna = -1;

/* ===== sessions + files: the authenticated channel and what rides it ======
 * Handshake datagrams (VNH1/2/3) and sealed session records share the Vinea
 * port: the demux is the datagram's first four bytes — VNA_MSG and the VNH
 * magics route by name, everything else is tried against sessions by sender
 * address. Inside a session, record type 2 is a CHUNK_REQ and type 3 the
 * CHUNK reply — the file layer's designed carrier (vna_file F6). The node
 * carves the embedded curriculum CAR into eight shard files and also shares
 * its ML-DSA-87 attestation; it announces its own slot's root and fetches
 * whatever else providers announce. */
#define VNAH_SLICE_N   VNAH_SHARD_N
#define VNAH_SLICE_LEN ((CUR_CAR_LEN + VNAH_SLICE_N - 1u) / VNAH_SLICE_N)
#define VNAH_SF_N      (VNAH_SLICE_N + 1u) /* shards + the attestation file */
#define VNAH_SF_ATTEST VNAH_SLICE_N
#define VNAH_SESS_N    9u
#define VNAH_SESS_PT   (VNA_CHUNK_SIZE + 640u) /* chunk + proof + header */
#define VNAH_REC_F_REQ 2u
#define VNAH_REC_F_REP 3u
/* chiglet visit frames inside a session (the warm layer below) */
#define VNAH_REC_V_REQ   4u  /* dna + grant + visitor's ML-DSA-87 signature */
#define VNAH_REC_V_ACK   5u  /* status + host dna when opened */
#define VNAH_REC_V_CLAIM 6u  /* one chgk claim/verdict, 67 bytes */
#define VNAH_REC_V_CACK  7u  /* status + xp gained, little-endian u32 */
#define VNAH_REC_V_END   8u  /* close the visit */
#define VNAH_REC_CISEX_Q 9u  /* CISEX datagram request over the PQ session */
#define VNAH_REC_CISEX_R 10u /* CISEX datagram reply over the PQ session */
#define VNAH_HS_POW      0u
#define VNAH_FETCH_MS    4000ull /* give one fetch a bounded window */

static uint8_t g_shard_leaves[VNAH_SLICE_N][1][32];
static vna_shared_file_t g_sf[VNAH_SF_N];
static vna_id_t g_sf_root[VNAH_SF_N];
static uint32_t g_shard_len[VNAH_SLICE_N];
static uint8_t g_shard_data[VNAH_SLICE_N][VNAH_SLICE_LEN];
static vna_fetch_t g_fetch;
static uint8_t g_fetch_bitmap[1]; /* (8 chunks + 7) / 8 */
static uint64_t g_fetch_deadline;
static uint8_t g_fetch_active;
static vna_id_t g_fetch_root;
static uint8_t g_fetch_data[VNA_CHUNK_SIZE * 8]; /* fetched file, verified chunks */
static uint32_t g_fetch_size;

/* What a node fetches it may re-serve: completed files land in a bounded
 * external-share set and are announced like the node's own shard — that is
 * the self-propagation loop, fetch to seeder. */
#define VNAH_EXT_N 4u
static vna_shared_file_t g_ext_sf[VNAH_EXT_N];
static uint8_t g_ext_data[VNAH_EXT_N][VNA_CHUNK_SIZE * 8];
static uint8_t g_ext_leaves[VNAH_EXT_N][8][32];
static uint8_t g_ext_used[VNAH_EXT_N];

/* Demand-pinning (active information trading): query velocity is a fast
 * local signal (per-entry decayed counter bumped by every KQ/DQ hit),
 * the envelope `wants` is the slow global one. A root whose velocity
 * crosses the fetch threshold and whose provider parameters are retained
 * gets fetched into the staging bucket (g_ext); when the bucket is full
 * the lowest-velocity entry is evicted — velocity, not LRU, so a rising
 * micro-trend displaces a fading one. The decay (255/256 per tick) is
 * what stops a scraper burst from thrashing the bucket: an instant spike
 * decays below the threshold within minutes, while sustained demand
 * stays pinned. */
#define VNAH_PROV_N    8u   /* retained provider-params table */
#define VNAH_VEL_FETCH 24u  /* velocity at which a fetch fires */
#define VNAH_VEL_DECAY 255u /* vel = vel * 255 / 256 per tick */
#define VNAH_VEL_MAX   0xFFFFu
static uint16_t g_vel[VNAH_CISEX_N]; /* per-entry demand velocity */
static struct {
    vna_id_t root;
    vna_id_t publisher;
    vna_provider_t pv;
    uint64_t seen_ms;
    uint8_t used;
} g_prov[VNAH_PROV_N];
static uint32_t g_pins, g_pin_evicts;
static void cisex_vel_bump(const uint8_t *reply, uint32_t len);
static vna_sess_t g_sess[VNAH_SESS_N];
static vna_hs_t g_hs[VNAH_SESS_N];
static uint8_t g_hs_used[VNAH_SESS_N];
static uint64_t g_hs_deadline[VNAH_SESS_N];
static uint8_t g_sess_addr[VNAH_SESS_N][6];
static vna_chunk_t g_chunk;
static uint8_t g_sess_pt[VNAH_SESS_PT];
static uint8_t g_sess_wire[VNA_SESS_REC_HDR + VNAH_SESS_PT + VNA_SESS_TAG];
static uint8_t g_hs_wire[12288];
static uint32_t g_sess_est, g_chunks_in, g_chunks_served, g_fetch_done;

/* ===== chiglet: the node's evidence judge =================================
 * Each audit cycle, every book member is scored by four INDEPENDENT evidence
 * sources — verified settlements, agreement usage, SMIC ring admission and
 * contact recency — each on its own axis so distinct sources stay distinct
 * directions. DECIDED needs R >= 2 independent directions AND a clear
 * margin; conflicting or thin evidence is honestly UNCERTAIN, never silently
 * trusted or condemned. The verdict is advisory (CHG_CAP_ADVISE): a DECIDED
 * SUSPECT peer is refused new sessions — the enforcement stays where it
 * always was, the chiglet's read is the evidence. */
#define CHG_L_CLEAR   0u
#define CHG_L_SUSPECT 1u
static chiglet_t g_chg;
static uint32_t g_chg_decided, g_chg_uncertain;
/* per book slot the judge's verdict: 0 undecided, 1 decided-clear,
 * 2 decided-suspect — the value the chiglet's own verdict claims carry */
static uint8_t g_chg_verdict[VNAH_BOOK_CAP];

static void vna_chg_model(void)
{
    static chg_model_t m;
    uint32_t d;
    vna_zero(&m, sizeof m);
    m.K = CHG_MAX_EXPERTS;
    m.D = CHG_DIM;
    m.L = 2;
    /* evidence axes live on dims 3..6; label prototypes read them */
    for (d = 3; d < 7; d++) {
        m.proto[CHG_L_CLEAR][d] = SR_ONE;
        m.proto[CHG_L_SUSPECT][d] = -SR_ONE;
    }
    m.R_min = SR_FROM_INT(2);
    m.margin_min = SR_FROM_FLOAT(0.4);
    vna_copy(m.label_name[CHG_L_CLEAR], "clear", 6);
    vna_copy(m.label_name[CHG_L_SUSPECT], "suspect", 8);
    m.loaded = false; /* the loader sets it */
    (void) chg_load_model(&g_chg, &m);
}

/* one source's read in [-1, 1] Q32.32 onto its own axis */
static void chg_axis(surplus_real_t ev[CHG_DIM], uint32_t axis, int32_t q)
{
    uint32_t d;
    for (d = 0; d < CHG_DIM; d++) ev[d] = SR_ZERO;
    ev[axis] = q > 0 ? SR_ONE : (q < 0 ? -SR_ONE : SR_ZERO);
}

/* Judge every book member on the audit cadence. */
static void vna_chg_audit(vna_host_t *h, uint64_t now)
{
    surplus_real_t ev[4][CHG_DIM];
    uint32_t i, r;
    int32_t q;
    for (i = 0; i < h->book.cap && i < VNAH_BOOK_CAP; i++) {
        const vna_account_t *ac = &h->book.acct[i];
        chg_result_t res;
        if (!ac->used || !ac->member) {
            g_chg_verdict[i] = 0;
            continue;
        }
        /* source 1: verified settlements with this member (value they have
         * issued to us, plus this cycle's contribution) */
        {
            uint64_t got = ac->contrib_total;
            for (r = 0; r < VNA_FORMS; r++) got += ac->held[r];
            q = got > 0 ? 1 : 0;
        }
        {
            uint32_t knz = 0;
            if (q != 0) chg_axis(ev[knz++], 3, q);
            {
                const vna_usage_ent_t *ue = NULL;
                uint32_t j;
                for (j = 0; j < h->us.ucap; j++)
                    if (h->us.u[j].used && vna_id_eq(&h->us.u[j].peer, &ac->id)) ue = &h->us.u[j];
                q = ue && (ue->chunks || ue->records || ue->compute) ? 1 : 0;
                if (q != 0) chg_axis(ev[knz++], 4, q);
            }
            {
                const smic_node_t *sn = smic_find(&g_smic, ac->id.b);
                q = !sn                                                                       ? 0
                    : sn->admitted == LPRES_STATE_TRUE                                        ? 1
                    : (sn->admitted == LPRES_STATE_FALSE || sn->admitted == LPRES_STATE_BOTH) ? -1
                                                                                              : 0;
                if (q != 0) chg_axis(ev[knz++], 5, q);
            }
            {
                const vna_contact_t *ct = vna_rt_find(&h->node.rt, &ac->id);
                q = !ct ? 0 : (now - ct->last_seen_ms) < 300000u ? 1 : -1;
                if (q != 0) chg_axis(ev[knz++], 6, q);
            }
            if (knz == 0 || chg_infer(&g_chg, ev, knz, &res) != CHG_OK) continue;
        }
        if (res.state == CHG_DECIDED) {
            g_chg_decided++;
            g_chg_verdict[i] = res.label == CHG_L_SUSPECT ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
        } else {
            g_chg_uncertain++;
            /* LOW_MARGIN means the evidence pulled both ways (BOTH); nothing
             * or nothing independent enough is honestly NEITHER — never a
             * collapse to a boolean */
            g_chg_verdict[i] =
                res.reason == CHG_REASON_LOW_MARGIN ? LPRES_STATE_BOTH : LPRES_STATE_NEITHER;
        }
    }
}

/* ===== the chiglet's warm layer: a social agent per node ==================
 * Every node births one chiglet: a hashed DNA genome (owner = the node's
 * ML-DSA-87 identity, seed = its fleet slot), a hobby profile the genome
 * picks, a bond model learning this node's own action rhythms, and the
 * visit machinery — signed grants, opened visits, four-valued verdict
 * claims — by which the nine chiglets meet each other over the PQ session
 * channel. Nothing here runs unsupervised: grants need BOTH owners' ML-DSA
 * signatures, a claim is accepted only while a visit is open and in scope,
 * and a foreign verdict is taken only when our own judgment does not
 * already hold a contradictory DECIDED verdict. A chiglet's claims about a
 * member are its judge's verdict, not gossip: the evidence hashes carry
 * the axis bundle it scored, and the verdict byte is the lpres state. */
#define VNAH_CHG_SEED_BASE 0xC461ull
#define VNAH_CHG_VISIT_S   900ull /* a visit's grant window, seconds */
#define VNAH_CHG_TOPIC_MEM 0u     /* topic: member standing */
#define VNAH_CHG_TOPIC_CNT 1u     /* topic: content availability */
#define VNAH_CHG_TOPICS    3u     /* topics bitmap: member + content */
#define VNAH_PD_N          16u     /* peer DNA table */
static chgk_registry_t g_kreg;
static chgk_dna_t g_self_dna;
static chgk_hobbies_t g_hobby;
static chgk_xp_t g_xp;
static chgk_grantbook_t g_gbook;
static chgb_t g_bond;
static chgk_visit_t g_visit[VNAH_SESS_N];
static chgk_dna_t g_visit_dna[VNAH_SESS_N];
static chgk_grant_t g_visit_out[VNAH_SESS_N]; /* the grant we sent, until acked */
static uint8_t g_visit_wait[VNAH_SESS_N];     /* 1: VISIT_REQ in flight */
static struct {
    vna_id_t publisher;
    chgk_dna_t dna;
    uint8_t used;
} g_peer_dna[VNAH_PD_N];
static uint32_t g_chg_visits, g_chg_claims_in, g_chg_claims_out, g_chg_xp_gain;
static uint32_t g_bond_obs, g_chg_dna_in;
static uint16_t g_bond_prev = CHGB_NONE;
static chgb_suggestion_t g_bond_sug;
static bool g_bond_sug_ok;
static bool g_chg_alive;

static void sha256_tag(const char *s, uint8_t out[32])
{
    sha256_ctx_t c;
    uint32_t n = 0;
    sha256_init(&c);
    while (s[n]) n++;
    sha256_update(&c, (const uint8_t *) s, n);
    sha256_final(&c, out);
}

static bool byte_eq32(const uint8_t *a, const uint8_t *b)
{
    uint32_t i;
    for (i = 0; i < 32 && a[i] == b[i]; i++) {
    }
    return i == 32;
}

/* The bond model's context: hour-of-week when the wall clock is honest, and
 * the previous action — that is what the three experts read. */
static void bond_obs(uint16_t app, uint16_t act, uint64_t now)
{
    chgb_ctx_t c;
    if (!g_chg_alive) return;
    c.slot = (uint16_t) ((now / 3600000ull) % 168u);
    c.app = app;
    c.prev = g_bond_prev;
    if (chgb_observe(&g_bond, &c, act) == CHGB_OK) {
        g_bond_prev = act;
        g_bond_obs++;
    }
}
#define BOND_APP_VNA    1u
#define BOND_APP_IDX    2u /* cisex */
#define BOND_APP_SEC    3u /* smic */
#define BOND_APP_KIN    4u /* chiglet social */
#define BOND_ACT_SESS   1u
#define BOND_ACT_SERVE  2u
#define BOND_ACT_FETCH  3u
#define BOND_ACT_ENV    4u
#define BOND_ACT_REFUSE 5u
#define BOND_ACT_VISIT  6u
#define BOND_ACT_CLAIM  7u
#define BOND_ACT_PIN    8u /* staging bucket: pin or evict */

/* ML-DSA-87 over the grant body, both owners' signature the grant demands. */
static bool chg_pk_verify(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg, uint32_t msg_len,
                          const uint8_t *sig, uint32_t sig_len, void *ctx)
{
    (void) ctx;
    return pk_len == VNA_PK_LEN && sig_len == VNA_SIG_LEN &&
           vna_verify(pk, "zxv/chgk/grant/v1", msg, msg_len, sig);
}

/* A peer's verdict claim passes our own checker unless we already hold the
 * contradictory DECIDED read on the same member: acceptance is "our
 * evidence does not already refute it", not blind trust. */
static bool chg_claim_checker(const vna_host_t *h, const chgk_claim_t *c)
{
    uint32_t i, j;
    if (c->kind == CHGK_K_VERDICT) {
        if (c->verdict > 3u) return false;
        for (i = 0; i < h->book.cap && i < VNAH_BOOK_CAP; i++) {
            const vna_account_t *ac = &h->book.acct[i];
            if (!ac->used || !ac->member) continue;
            for (j = 0; j < 32 && ac->id.b[j] == c->claim[j]; j++) {
            }
            if (j == 32 && g_chg_verdict[i] == LPRES_STATE_FALSE)
                /* we hold DECIDED-SUSPECT; only F or BOTH is consistent with
                 * it — a T claim contradicts our decided evidence */
                return c->verdict == LPRES_STATE_FALSE || c->verdict == LPRES_STATE_BOTH;
            if (j == 32 && g_chg_verdict[i] == LPRES_STATE_TRUE)
                /* and symmetric: our DECIDED-clear refutes an F claim */
                return c->verdict != LPRES_STATE_FALSE;
        }
        return true;
    }
    return c->kind == CHGK_K_CLAIM;
}

static void vna_chg_warm_init(vna_host_t *h)
{
    uint8_t sp[32], ln[32];
    uint64_t seed;
    sha256_tag("zxv/chg/species/node/v1", sp);
    sha256_tag("zxv/chg/lineage/fleet/0", ln);
    chgk_registry_init(&g_kreg);
    seed = (uint64_t) (h->peer_index == 0xFFu ? h->shard_slot : h->peer_index) + VNAH_CHG_SEED_BASE;
    if (chgk_dna_birth(&g_kreg, h->idn.id.b, sp, ln, NULL, seed, &g_self_dna) != CHGK_OK) return;
    (void) chgk_hobbies_for(&g_kreg, &g_self_dna, &g_hobby);
    chgk_xp_init(&g_xp, &g_self_dna);
    chgk_grantbook_init(&g_gbook);
    chgb_init(&g_bond, h->idn.id.b);
    chgb_set_learning(&g_bond, true);
    g_chg_alive = true;
}

/* Publish this node's chiglet genome so peers can address grants to it. */
static void vna_chg_dna_publish(vna_host_t *h, uint64_t now)
{
    static uint8_t buf[VNA_REC_MAX];
    vna_id_t key;
    int32_t len;
    uint8_t rnd[32];
    vna_rec_t *r = &h->node.rec;
    if (!g_chg_alive) return;
    r->rtype = VNA_REC_CHG;
    vna_copy(key.b, g_self_dna.id, 32);
    r->key = key;
    r->created = now;
    r->expires = now + 30ull * 24ull * 3600ull * 1000ull;
    r->payload_len = (uint16_t) chgk_dna_encode(&g_self_dna, r->payload, sizeof r->payload);
    if (r->payload_len != CHGK_DNA_ENC) return;
    vna_drbg_gen(&h->node.rng, rnd, 32);
    len = vna_rec_seal(r, h->node.idn, rnd, buf, sizeof buf);
    if (len > 0) (void) vna_node_publish(&h->node, buf, (uint32_t) len, now, &h->ob);
}

/* ===== zt executor: integer-only inference as the AIPI RUN substrate ======
 * A served function's `module` is a GGUF model (AIPI_MODULE_MAX already
 * bounds it to one raw block); the call input is the prompt, the output the
 * generated text. The engine is deterministic integer math, so check()
 * re-runs the generate and compares — a real CHECKER. certify() refuses:
 * generative output has no certificate to issue. One shared arena, the host
 * is single-threaded, so runs are exclusive by construction. */
#define VNAH_ZT_ARENA (8u * 1024u * 1024u)
#define VNAH_ZT_NCTX  384u
#define VNAH_ZT_MAXT  160u
static uint8_t g_zt_arena[VNAH_ZT_ARENA];
static uint32_t g_zt_runs, g_zt_refused;

typedef struct {
    uint8_t *out;
    uint32_t cap, len;
    bool full;
} zt_out_t;

static bool zt_on_token(void *vctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    zt_out_t *o = (zt_out_t *) vctx;
    (void) id;
    if (o->len > o->cap || len > o->cap - o->len) {
        o->full = true;
        return false;
    }
    vna_copy(o->out + o->len, piece, len);
    o->len += len;
    return true;
}

static int zt_run(void *vctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    zt_gguf_t g;
    zt_model_t m;
    zt_tok_t t;
    zt_model_state_t s;
    zt_gen_params_t gp;
    zt_gen_result_t res;
    zt_out_t o;
    uint64_t need, st_need, wk_need, off;
    (void) vctx;
    g_zt_runs++;
    if (!code || !in || in_len == 0 || !out || !out_len) return AIPI_ERR_ARG;
    if (zt_gguf_open(&g, code->module, code->module_len) != 0) return AIPI_ERR_MALFORMED;
    need = zt_model_arena_bytes(&g);
    if (!need || need > VNAH_ZT_ARENA / 2u) {
        g_zt_refused++;
        return AIPI_ERR_SPACE;
    }
    if (zt_model_load(&m, &g, g_zt_arena, need, NULL) != 0) return AIPI_ERR_MALFORMED;
    off = need;
    st_need = zt_model_state_bytes(&m, VNAH_ZT_NCTX, 8u, ZT_KV_Q16);
    if (!st_need || off + st_need > VNAH_ZT_ARENA) {
        g_zt_refused++;
        return AIPI_ERR_SPACE;
    }
    if (zt_model_state_init(&s, &m, VNAH_ZT_NCTX, 8u, ZT_KV_Q16, g_zt_arena + off, st_need) != 0)
        return AIPI_ERR_MALFORMED;
    off += st_need;
    need = zt_tok_arena_bytes(&g);
    if (off + need > VNAH_ZT_ARENA || zt_tok_load(&t, &g, g_zt_arena + off, need) != 0)
        return AIPI_ERR_MALFORMED;
    off += need;
    wk_need = zt_model_gen_work_bytes(&m, &t, in_len, VNAH_ZT_MAXT);
    if (!wk_need || off + wk_need > VNAH_ZT_ARENA) {
        g_zt_refused++;
        return AIPI_ERR_SPACE;
    }
    vna_zero(&gp, sizeof gp);
    gp.max_tokens = VNAH_ZT_MAXT;
    gp.on_token = zt_on_token;
    o.out = out;
    o.cap = cap;
    o.len = 0;
    o.full = false;
    gp.ctx = &o;
    if (zt_model_generate(&m, &s, &t, in, in_len, &gp, g_zt_arena + off, wk_need, &res) != 0)
        return AIPI_ERR_EXEC;
    *out_len = o.len;
    return AIPI_OK;
}

/* A generative model is a deterministic map (module, input) -> output on this
 * integer engine, so the honest check is recompute-and-compare. */
static int zt_check(void *vctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                    const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len)
{
    static uint8_t again[AIPI_IO_MAX];
    uint32_t again_len = 0;
    int r;
    (void) cert;
    (void) cert_len;
    if (out_len > sizeof again) return AIPI_ERR_SPACE;
    r = zt_run(vctx, code, in, in_len, again, sizeof again, &again_len);
    if (r != AIPI_OK) return r;
    return again_len == out_len && (out_len == 0 || vna_ct_eq(again, out, out_len)) ? 1 : 0;
}

static int zt_certify(void *vctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                      const uint8_t *out, uint32_t out_len, uint8_t *cert, uint32_t cap,
                      uint32_t *cert_len)
{
    (void) vctx;
    (void) code;
    (void) in;
    (void) in_len;
    (void) out;
    (void) out_len;
    (void) cert;
    (void) cap;
    (void) cert_len;
    return AIPI_ERR_UNSUPP;
}

static void sess_fetch_begin(vna_host_t *h, const vna_id_t *root, const vna_id_t *provider,
                             const vna_provider_t *pv, uint64_t now);

/* The record's replay identity: fold key|publisher|created into 32 bytes so
 * oseq's window answers "have we already admitted this exact record". */
static void env_replay_hash(const vna_id_t *key, const vna_id_t *pub, uint64_t created,
                            uint8_t ph[32])
{
    uint32_t i;
    for (i = 0; i < 32; i++) ph[i] = (uint8_t) (key->b[i] ^ pub->b[i]);
    for (i = 0; i < 8; i++) ph[i] ^= (uint8_t) (created >> (i * 8));
    for (i = 0; i < 8; i++) ph[8 + i] ^= (uint8_t) (created >> (i * 8));
}

static int env_seen(const vna_id_t *key, const vna_id_t *pub, uint64_t created)
{
    uint8_t ph[32];
    env_replay_hash(key, pub, created, ph);
    if (oseq_check_replay(&g_oseq, ph, 32)) return 1;
    oseq_record_replay(&g_oseq, ph, 32);
    if (g_oseq_vna >= 0) {
        int32_t e = oseq_register_event(&g_oseq, (uint32_t) g_oseq_vna, "ubh168", 1, ph, 32,
                                        (const uint32_t *) 0, 0);
        if (e >= 0) (void) oseq_commit_event(&g_oseq, (uint32_t) e);
    }
    return 0;
}

/* Seal one UBH-168 envelope into a VNA_REC_ENV record and publish it under
 * key — the fleet's propagation carrier: every record is ML-DSA-87-signed
 * by its publisher and lands in the store of whoever holds the key's
 * neighbourhood. Same pattern vna_node_announce uses. */
static void vna_env_publish(vna_host_t *h, const vna_id_t *key, const uint8_t *env, uint64_t now,
                            uint64_t ttl_ms)
{
    static uint8_t buf[VNA_REC_MAX];
    uint8_t rnd[32];
    int32_t len;
    vna_rec_t *r = &h->node.rec;
    r->rtype = VNA_REC_ENV;
    r->key = *key;
    r->created = now;
    r->expires = now + ttl_ms;
    vna_copy(r->payload, env, SMIC_ENV_BYTES);
    r->payload_len = (uint16_t) SMIC_ENV_BYTES;
    vna_drbg_gen(&h->node.rng, rnd, 32);
    len = vna_rec_seal(r, h->node.idn, rnd, buf, sizeof buf);
    if (len > 0 && vna_node_publish(&h->node, buf, (uint32_t) len, now, &h->ob) >= 0) h->env_pubs++;
}

/* One envelope payload into the ring: SMIC manifests drive the admission
 * table, exchange envelopes the order book, CISX index records merge into
 * the index — and all three feed CISEX. */
static void vna_env_ingest(vna_host_t *h, const uint8_t *payload, uint32_t len, uint64_t now)
{
    smic_hw_t hw;
    vna_exch_rec_t er;
    uint16_t mtype;
    lpx_action_t a;
    cisex_wire_t cw;
    if (len != SMIC_ENV_BYTES) return;
    /* the strictest schema first: a peer's index record joins its regional
     * observations into ours (idempotent — same record twice is a no-op) */
    if (cisex_unpack_idx(payload, &cw) == IPFSN_OK) {
        if (cisex_merge(&g_cisex, payload) == IPFSN_OK) h->idx_in++;
        return;
    }
    if (smic_manifest_unpack(payload, &mtype, &hw) == SMIC_OK) {
        a = smic_admit(&g_smic, &hw, now);
        if (a == LPX_ACT_ESCALATE || a == LPX_ACT_ROLLBACK) h->smic_quar++;
        h->smic_in++;
        (void) cisex_observe(&g_cisex, hw.hw_digest, hw.region, LPRES_STATE_TRUE);
        return;
    }
    if (vna_exch_unpack(payload, &er) == VNA_OK) {
        (void) cisex_provide(&g_cisex, er.root_digest, er.domain, er.addr);
        h->exch_in++;
    }
}

/* A sealed record off the wire: schema-unpack and, when it carries an
 * envelope, dispatch it. The node verified it before it landed. */
static void vna_env_ingest_rec(vna_host_t *h, const uint8_t *bytes, uint32_t len, uint64_t now)
{
    vna_rec_t r;
    uint32_t i;
    if (vna_schema_unpack(&vna_rec_schema, bytes, len, &r, 0) < 0) return;
    if (r.rtype == VNA_REC_ENV) {
        if (env_seen(&r.key, &r.publisher, r.created)) return;
        vna_env_ingest(h, r.payload, r.payload_len, now);
        bond_obs(BOND_APP_IDX, BOND_ACT_ENV, now);
        return;
    }
    if (r.rtype == VNA_REC_CHG) {
        /* a peer's chiglet genome: register it against its publisher so a
         * visit's grant can name it. A node may only publish its own
         * chiglet — the record signature binds the payload to it, and the
         * DNA's owner field must be the same identity. */
        chgk_dna_t pd;
        if (!g_chg_alive || chgk_dna_decode(r.payload, r.payload_len, &pd) != CHGK_OK ||
            !byte_eq32(pd.owner, r.publisher.b))
            return;
        for (i = 0; i < VNAH_PD_N; i++)
            if (g_peer_dna[i].used && vna_id_eq(&g_peer_dna[i].publisher, &r.publisher)) break;
        if (i == VNAH_PD_N)
            for (i = 0; i < VNAH_PD_N; i++)
                if (!g_peer_dna[i].used) break;
        if (i < VNAH_PD_N) {
            g_peer_dna[i].publisher = r.publisher;
            g_peer_dna[i].dna = pd;
            g_peer_dna[i].used = 1;
            g_chg_dna_in++;
        }
        return;
    }
    if (r.rtype == VNA_REC_PROVIDER) {
        /* a file we do not share, offered by someone: fetch it over a
         * session — self-propagating content beyond the curriculum. The
         * parameters are also retained, so demand later (velocity) can
         * fetch what an earlier announcement could not. */
        vna_provider_t pv;
        for (i = 0; i < VNAH_SF_N; i++)
            if (vna_id_eq(&r.key, &g_sf_root[i])) return;
        if (vna_schema_unpack(&vna_provider_schema, r.payload, r.payload_len, &pv, 0) < 0) return;
        if (pv.visibility != VNA_VIS_PUBLIC) return;
        {
            uint32_t p = VNAH_PROV_N;
            for (i = 0; i < VNAH_PROV_N; i++)
                if (g_prov[i].used && vna_id_eq(&g_prov[i].root, &r.key)) p = i;
            if (p == VNAH_PROV_N)
                for (i = 0; i < VNAH_PROV_N; i++)
                    if (!g_prov[i].used) p = i;
            if (p < VNAH_PROV_N) {
                g_prov[p].root = r.key;
                g_prov[p].publisher = r.publisher;
                g_prov[p].pv = pv;
                g_prov[p].seen_ms = now;
                g_prov[p].used = 1;
            }
        }
        sess_fetch_begin(h, &r.key, &r.publisher, &pv, now);
    }
}

/* What peers stored at this node, plus every finished lookup's found
 * value — both are propagation ingress for the ring programs. */
static void vna_env_sweep(vna_host_t *h, uint64_t now)
{
    uint32_t i;
    for (i = 0; i < h->node.store_cap; i++) {
        const vna_store_slot_t *s = &h->node.store[i];
        if (s->used) vna_env_ingest_rec(h, s->rec, s->len, now);
    }
}

/* The node's own SMIC manifest: identity = NodeID, inventory = this
 * machine's profile. Filled once at start. */
static const uint8_t g_geo_routes[9][4] = {
    {8, 2, 4, 5}, {6, 5, 7, 8}, {8, 5, 0, 4},
    {7, 4, 6, 8}, {8, 3, 0, 7}, {2, 0, 1, 8},
    {7, 3, 1, 8}, {6, 3, 4, 8}, {4, 2, 0, 7},
};

static uint32_t vna_cell_pin_batch(const vna_host_t *h)
{
    static const uint8_t batch[9] = {1, 1, 2, 2, 2, 3, 3, 5, 8};
    if (h && h->peer_index < 9u) return batch[h->peer_index];
    return 2u;
}

static void vna_smic_self(vna_host_t *h)
{
    sha256_ctx_t sc;
    vna_zero(&g_self_hw, sizeof g_self_hw);
    for (uint32_t i = 0; i < 32; i++) g_self_hw.node_id[i] = h->idn.id.b[i];
    sha256_init(&sc);
    sha256_update(&sc, (const uint8_t *) "zxv/smic/hw/v1", 14);
    sha256_update(&sc, h->l->nic->mac, NET_MAC_LEN);
    sha256_final(&sc, g_self_hw.hw_digest);
    g_self_hw.caps = SMIC_CAP_VMXNET3 | SMIC_CAP_VIRTIO_PCI | SMIC_CAP_INDEX |
                     SMIC_CAP_IPFS_PIN | SMIC_CAP_VERIFIED;
    g_self_hw.region = h->peer_index == 0xFFu ? h->shard_slot : h->peer_index;
    g_self_hw.role = SMIC_ROLE_RELAY;
    g_self_hw.disk_mb = 409600;
    g_self_hw.net_mbps = 400;
    g_self_hw.octane = SWARM_OCTANE_MIDGRADE;
    switch (h->peer_index) {
    case 0: case 1:
        g_self_hw.role = SMIC_ROLE_RELAY; g_self_hw.disk_mb = 307200;
        g_self_hw.net_mbps = 200; g_self_hw.octane = SWARM_OCTANE_REGULAR; break;
    case 2: case 3: case 4:
        g_self_hw.role = SMIC_ROLE_RELAY; g_self_hw.disk_mb = 409600;
        g_self_hw.net_mbps = 400; g_self_hw.octane = SWARM_OCTANE_MIDGRADE; break;
    case 5:
        g_self_hw.role = SMIC_ROLE_WAREHOUSE; g_self_hw.disk_mb = 409600;
        g_self_hw.net_mbps = 600; g_self_hw.octane = SWARM_OCTANE_PREMIUM; break;
    case 6:
        g_self_hw.role = SMIC_ROLE_WAREHOUSE; g_self_hw.disk_mb = 204800;
        g_self_hw.net_mbps = 600; g_self_hw.octane = SWARM_OCTANE_PREMIUM; break;
    case 7:
        g_self_hw.caps |= SMIC_CAP_NET_1G; g_self_hw.role = SMIC_ROLE_WAREHOUSE;
        g_self_hw.disk_mb = 307200; g_self_hw.net_mbps = 1000; g_self_hw.octane = SWARM_OCTANE_RACING; break;
    case 8:
        g_self_hw.caps |= SMIC_CAP_DISK_900G | SMIC_CAP_NET_1G; g_self_hw.role = SMIC_ROLE_MAINFRAME;
        g_self_hw.disk_mb = 921600; g_self_hw.net_mbps = 1000; g_self_hw.octane = SWARM_OCTANE_MAX; break;
    default: break;
    }
    g_self_hw.arch = SMIC_ARCH_X86_64;
}

/* Our own epistemic listing on the order book: the curriculum root we
 * serve, announced under the listing key so every peer's exchange learns
 * the offer. witness stays zero until a spot audit fills it. */
static void vna_exch_publish(vna_host_t *h, uint64_t now)
{
    vna_exch_rec_t er;
    uint8_t env[SMIC_ENV_BYTES];
    vna_id_t k;
    uint32_t i;
    vna_zero(&er, sizeof er);
    er.kind = VNA_EXCH_T_LISTING;
    vna_copy(er.root_digest, g_cur_digest, 32);
    er.provider = h->idn.id;
    er.file_size = 69763; /* zxv-curriculum.car */
    er.n_chunks = 3;
    for (i = 0; i < 4; i++) er.addr[i] = h->l->eth->ip[i];
    er.addr[4] = (uint8_t) (VNAH_PORT >> 8);
    er.addr[5] = (uint8_t) VNAH_PORT;
    er.form = SWARM_CAP_INTELLECTUAL;
    er.octane = g_self_hw.octane;
    er.domain = (uint16_t) h->shard_slot;
    er.armada = (uint8_t) (g_self_hw.region < 8 ? (1u << g_self_hw.region) : 0u);
    if (vna_exch_pack(&er, env) == VNA_OK) {
        vna_htag("zxv/exchange/listing/v1", er.root_digest, 32, k.b);
        vna_env_publish(h, &k, env, now, VNAH_ENV_TTL);
    }
}

/* ===== AIPI: the call broker and its bounded ipfs node =====================
 * The node runs a RAM blockstore and the AIPI context so calls can be
 * broadcast, claims verified, bounties escrowed and provider announcements
 * exchanged — the broker's proper capacity on a node with no local model
 * executor yet (zt_* inference lands as the exec backend). */
#define VNAH_AIPI_BS_N   2048u
#define VNAH_AIPI_PINS   2048u
#define VNAH_AIPI_JOBS   8u
#define VNAH_AIPI_MEMO   32u
#define VNAH_AIPI_STORE  (8u << 20)
#define VNAH_AIPI_BUDGET 1000000ull /* bounty escrow ceiling per node */

static uint8_t g_aipi_store[VNAH_AIPI_STORE];
static uint8_t g_aipi_net[IPFSN_BLOCK_MAX + 256u];
static uint8_t g_aipi_mod[AIPI_MODULE_MAX];
static uint8_t g_aipi_con[AIPI_CONTRACT_MAX];
static ipfsn_memstore_t g_aipi_ms;
static ipfsn_storage_ops_t g_aipi_sops;
static ipfsn_bs_t g_aipi_bs;
static ipfsn_bs_entry_t g_aipi_tab[VNAH_AIPI_BS_N];
static ipfsn_pin_t g_aipi_pins[VNAH_AIPI_PINS];
static ipfsn_node_t g_ipfs;
static aipi_ctx_t g_aipi;
static aipi_job_t g_aipi_jobs[VNAH_AIPI_JOBS];
static aipi_memo_t g_aipi_memo[VNAH_AIPI_MEMO];
static aipi_vna_t g_av;
static uint64_t g_aipi_escrow;
static bool g_aipi_on;
static uint8_t g_self_attest[SMIC_ATTEST_BYTES];
static ipfsn_cid_t g_attest_cid;
static bool g_attest_ok;

static int vna_book_learn(vna_host_t *h, const vna_id_t *id);
static int vna_is_seed(const vna_host_t *h, const uint8_t addr[6]);
static int vna_sendto(vna_host_t *h, const uint8_t addr[6], const uint8_t *data, uint32_t len);
static int vna_contact_addr(vna_host_t *h, const vna_id_t *id, uint8_t addr[6]);
static void vna_sess_rx(vna_host_t *h, const uint8_t a[6], const uint8_t *data, uint32_t len,
                        uint64_t now);
static void vna_sess_poll(vna_host_t *h, uint64_t now);
static void vna_shard_setup(void);

/* ===== timesvc: SNTP samples from three servers, Byzantine agreement =====
 * Until the commit lands the node runs on its monotonic boot clock exactly
 * as before; once it does, `now` becomes Unix ms — record created/expires
 * are then comparable across nodes instead of each node counting from its
 * own power-on. One lying or broken server cannot skew the epoch: commit
 * needs two samples agreeing within tolerance and a strict majority, and
 * the agreed time can never go below the build floor. */
#define VNAH_SNTP_LPORT   49724u
#define VNAH_SNTP_PIVOT_S 1740000000ll /* era anchor: within 68y of the truth */
#define VNAH_SNTP_NSERV   3u
#define VNAH_SNTP_TRIES   20u /* one round at boot, then a retry each settle */
static const uint8_t g_sntp_serv[VNAH_SNTP_NSERV][NET_IP_LEN] = {
    {162, 159, 200, 1}, /* Cloudflare time.cloudflare.com */
    {216, 239, 35, 0},  /* Google time.google.com */
    {129, 6, 15, 28},   /* NIST time.nist.gov */
};
static int32_t g_sntp_sock = -1;
static uint64_t g_sntp_nonce[VNAH_SNTP_NSERV];
static int64_t g_sntp_t1_us[VNAH_SNTP_NSERV];
static int64_t g_wall_off_ms; /* unix_ms - mono_ms once synced */
static bool g_sntp_done;
static uint32_t g_sntp_tries;
static timesvc_t g_ts;

/* ===== event space: the node's self-supervision registry ===================
 * Each running subsystem is a domain; the audit/heal cycle runs on the host
 * cadence so queue, budget, fault-rate and quarantine invariants are checked
 * continuously and healing actions (warn/suspend/quarantine/recover) are
 * recorded by the framework itself. */
static ev_sequencer_t g_seq;
static ev_audit_t g_audit;
static ev_healing_t g_heal;
static int32_t g_dom_vna = -1, g_dom_door = -1, g_dom_aipi = -1, g_dom_smic = -1, g_dom_exch = -1;
static uint32_t g_ev_worst; /* worst EV_HEAL_* action seen this boot */
static scp144_cavity_t g_casimir_cavity;
static porter_house_t g_porter;
static immigration_t g_immig;
static uint32_t g_bunker_bounces;

static bool immig_kernel_sig_ok(const struct immig_daemon *d)
{
    return d != NULL && d->visa == IMMIG_VISA_RESIDENT;
}

static uint64_t vna_now(vna_host_t *h)
{
    return h->l->clk.ms(h->l->clk.ctx);
}

static uint64_t vna_wall_sec(vna_host_t *h)
{
    uint64_t m = h->l->clk.ms(h->l->clk.ctx);
    uint64_t w = g_sntp_done ? (uint64_t) ((int64_t) m + g_wall_off_ms) : m;
    return w / 1000ull;
}

static void sntp_rx(void *ctx, const uint8_t src_ip[NET_IP_LEN], uint16_t src_port,
                    const uint8_t *data, uint32_t len)
{
    vna_host_t *h = (vna_host_t *) ctx;
    sntp_sample_t s;
    uint32_t i;
    (void) src_ip;
    (void) src_port;
    if (g_sntp_done || !data || len < SNTP_PKT_LEN) return;
    for (i = 0; i < VNAH_SNTP_NSERV; i++) {
        if (sntp_parse_reply(data, len, g_sntp_nonce[i], g_sntp_t1_us[i],
                             (int64_t) h->l->clk.ms(h->l->clk.ctx) * 1000, VNAH_SNTP_PIVOT_S,
                             &s) != SNTP_OK)
            continue;
        (void) timesvc_add_sample(&g_ts, &s);
        /* commit as soon as the service can agree; failures keep samples */
        if (g_ts.n_samples >= 2u &&
            timesvc_commit(&g_ts, (int64_t) h->l->clk.ms(h->l->clk.ctx) * 1000) == TIMESVC_SYNCED) {
            g_wall_off_ms = g_ts.offset_us / 1000;
            g_sntp_done = true;
        }
        return;
    }
}

static int ah_reserve(void *ctx, uint64_t amount)
{
    (void) ctx;
    if (amount > VNAH_AIPI_BUDGET || g_aipi_escrow > VNAH_AIPI_BUDGET - amount) return -1;
    g_aipi_escrow += amount;
    return 0;
}

static void ah_refund(void *ctx, uint64_t amount)
{
    (void) ctx;
    g_aipi_escrow = amount > g_aipi_escrow ? 0 : g_aipi_escrow - amount;
}

static int ah_pay(void *ctx, const vna_id_t *executor, uint64_t amount, const ipfsn_cid_t *call)
{
    vna_host_t *h = (vna_host_t *) ctx;
    (void) call;
    ah_refund(ctx, amount); /* leaves escrow: owed to the executor now */
    if (!vna_book_learn(h, executor)) return -1;
    return vna_link_want_from(&h->link, executor, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 1, amount) >= 0
               ? 0
               : -1;
}

static void ah_paid(void *ctx, const vna_id_t *caller, uint64_t amount, const ipfsn_cid_t *call)
{
    vna_host_t *h = (vna_host_t *) ctx;
    (void) call;
    if (amount == 0 || !vna_book_learn(h, caller)) return;
    (void) vna_link_sell(&h->link, caller, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1,
                         amount, "offer(compute, units: 1)", g_av.now);
}

/* Carve the embedded CAR into VNAH_SLICE_N contiguous shard files; each is
 * its own Merkle-rooted vna_file (one 16 KiB chunk per slice on this
 * archive). Slot VNAH_SF_ATTEST is filled at start with the attestation. */
static void vna_shard_setup(void)
{
    uint32_t off = 0, i;
    vna_zero(g_sf, sizeof g_sf);
    for (i = 0; i < VNAH_SLICE_N; i++) {
        uint32_t n = off < CUR_CAR_LEN
                         ? (CUR_CAR_LEN - off < VNAH_SLICE_LEN ? CUR_CAR_LEN - off : VNAH_SLICE_LEN)
                         : 0;
        g_shard_len[i] = n;
        vna_copy(g_shard_data[i], cur_car + off, n);
        (void) vna_file_build(g_shard_data[i], n, VNA_CHUNK_SIZE, g_shard_leaves[i], 1,
                              &g_sf_root[i]);
        g_sf[i].root = g_sf_root[i];
        g_sf[i].size = n;
        g_sf[i].chunk_size = VNA_CHUNK_SIZE;
        g_sf[i].n = 1;
        g_sf[i].leaves = (const uint8_t(*)[32]) g_shard_leaves[i];
        g_sf[i].data = g_shard_data[i];
        off += n;
    }
}

/* Harvest the embedded CAR into the index: every [varint][cid][block]
 * section's dag-pb payload is verified against its CID then crawled, so
 * the curriculum's own links become citation/keyword evidence. Raw leaves
 * get the first-chunk classifier (kind + header entropy). Returns the
 * sections indexed. */
#define VNAH_PIN_Q_CAP 4096u
static ipfsn_cid_t g_pin_q[VNAH_PIN_Q_CAP];
static uint8_t g_pin_q_used[VNAH_PIN_Q_CAP];
static uint8_t g_pin_q_done[VNAH_PIN_Q_CAP];
static uint32_t g_pin_q_count;
static uint32_t g_pin_q_cursor;
static uint8_t g_door_ipfs_buf[IPFSN_BLOCK_MAX];
static ipfsn_cid_t g_door_ipfs_cid;
static uint32_t g_door_ipfs_len;
static uint8_t g_door_ipfs_valid;
static uint32_t g_ipfs_downloads;
static uint64_t g_ipfs_bytes_served;

static void pin_q_enqueue(const ipfsn_cid_t *cid)
{
    uint32_t i;
    if (!cid || cid->mh_code != IPFSN_MH_SHA2_256 || cid->digest_len != 32u) return;
    if (ipfsn_bs_has(&g_aipi_bs, cid)) return;
    for (i = 0; i < g_pin_q_count; i++) {
        if (g_pin_q_used[i] && ipfsn_cid_equal(&g_pin_q[i], cid)) return;
    }
    if (g_pin_q_count < VNAH_PIN_Q_CAP) {
        g_pin_q[g_pin_q_count] = *cid;
        g_pin_q_used[g_pin_q_count] = 1;
        g_pin_q_done[g_pin_q_count] = 0;
        g_pin_q_count++;
        return;
    }
    for (i = 0; i < VNAH_PIN_Q_CAP; i++) {
        if (g_pin_q_done[i]) {
            g_pin_q[i] = *cid;
            g_pin_q_used[i] = 1;
            g_pin_q_done[i] = 0;
            return;
        }
    }
}

static int pin_ingest_block(const ipfsn_cid_t *cid, const uint8_t *blk, uint32_t blen,
                            uint8_t region)
{
    uint32_t i, n_links = 0;
    uint8_t kind = 0;
    uint16_t ent = 0;
    bool already;
    if (!cid || !blk || blen > IPFSN_BLOCK_MAX) return -1;
    already = ipfsn_bs_has(&g_aipi_bs, cid);
    if (!already) {
        if (ipfsn_bs_put(&g_aipi_bs, cid, blk, blen, IPFSN_VIS_PUBLIC, NULL) != IPFSN_OK)
            return -1;
        (void) ipfsn_pin(&g_ipfs, cid, IPFSN_VIS_PUBLIC, cid);
        g_pins++;
    }
    for (i = 0; i < g_pin_q_count; i++) {
        if (g_pin_q_used[i] && ipfsn_cid_equal(&g_pin_q[i], cid)) g_pin_q_done[i] = 1;
    }
    if (cid->codec == IPFSN_MC_DAG_PB) {
        ipfsn_pbnode_t pb;
        (void) cisex_index_dagpb(&g_cisex, cid, blk, blen, region, &n_links);
        if (ipfsn_dagpb_parse(blk, blen, &pb) == IPFSN_OK) {
            uint32_t pos = 0;
            ipfsn_pblink_t lnk;
            while (ipfsn_dagpb_next_link(&pb, &pos, &lnk) == IPFSN_OK) {
                pin_q_enqueue(&lnk.cid);
            }
        }
    } else {
        (void) cisex_peek(&g_cisex, cid->digest, blk,
                          blen > CISEX_HDR_PEEK ? CISEX_HDR_PEEK : blen, region, &kind, &ent);
    }
    (void) cisex_observe(&g_cisex, cid->digest, region, LPRES_STATE_TRUE);
    return 0;
}

static uint32_t cisex_car_harvest(uint8_t region)
{
    ipfsn_car_t car;
    ipfsn_cid_t c;
    const uint8_t *blk;
    uint32_t blen, done = 0;
    if (CUR_CAR_LEN > UINT32_MAX || ipfsn_car_open(&car, cur_car, CUR_CAR_LEN) != IPFSN_OK)
        return 0;
    while (ipfsn_car_next(&car, &c, &blk, &blen) == IPFSN_OK) {
        if (c.mh_code != IPFSN_MH_SHA2_256 || c.digest_len != 32u) continue;
        (void) pin_ingest_block(&c, blk, blen, region);
        done++;
    }
    return done;
}

/* The attestation datagram as a shared file: peers fetch it by its root to
 * verify this node's manifest signature offline. */
static void vna_attest_share(void)
{
    static uint8_t att_leaves[1][32];
    (void) vna_file_build(g_self_attest, SMIC_ATTEST_BYTES, VNA_CHUNK_SIZE, att_leaves, 1,
                          &g_sf_root[VNAH_SF_ATTEST]);
    g_sf[VNAH_SF_ATTEST].root = g_sf_root[VNAH_SF_ATTEST];
    g_sf[VNAH_SF_ATTEST].size = SMIC_ATTEST_BYTES;
    g_sf[VNAH_SF_ATTEST].chunk_size = VNA_CHUNK_SIZE;
    g_sf[VNAH_SF_ATTEST].n = 1;
    g_sf[VNAH_SF_ATTEST].leaves = (const uint8_t(*)[32]) att_leaves;
    g_sf[VNAH_SF_ATTEST].data = g_self_attest;
}

static int sess_slot(const vna_id_t *peer)
{
    uint32_t i;
    for (i = 0; i < VNAH_SESS_N; i++)
        if (g_sess[i].established && vna_id_eq(&g_sess[i].peer, peer)) return (int) i;
    return -1;
}

static int sess_slot_addr(const uint8_t a[6])
{
    uint32_t i, j;
    for (i = 0; i < VNAH_SESS_N; i++) {
        if (!g_sess[i].established) continue;
        for (j = 0; j < 6 && g_sess_addr[i][j] == a[j]; j++) {
        }
        if (j == 6) return (int) i;
    }
    return -1;
}

static int sess_free(void)
{
    uint32_t i;
    for (i = 0; i < VNAH_SESS_N; i++)
        if (!g_sess[i].established && !g_hs_used[i]) return (int) i;
    return -1;
}

static uint8_t g_ac_stage[VNAH_SESS_PT];
static void sess_send(vna_host_t *h, int i, uint8_t type, const uint8_t *pt, uint32_t len)
{
    int32_t n;
    uint64_t cyc = g_sess[i].tx_seq + 1ull;
    if (len > sizeof g_ac_stage) return;
    if (len > 0) {
        vna_copy(g_ac_stage, pt, len);
        scp144_tunnel_contract(g_ac_stage, len, g_sess[i].session_id, cyc);
        g_casimir_cavity.parts_sealed++;
    }
    n = vna_sess_seal(&g_sess[i], type, g_ac_stage, len, g_sess_wire, sizeof g_sess_wire);
    if (n > 0) (void) vna_sendto(h, g_sess_addr[i], g_sess_wire, (uint32_t) n);
}

/* The chiglet's decided verdict on a book member: only a DECIDED-suspect
 * (LPRES FALSE) refuses a session; T, BOTH and NEITHER all get the normal
 * path — uncertainty condemns nobody. */
static bool chg_suspect(const vna_host_t *h, const vna_id_t *peer)
{
    uint32_t i;
    for (i = 0; i < h->book.cap && i < VNAH_BOOK_CAP; i++)
        if (h->book.acct[i].used && vna_id_eq(&h->book.acct[i].id, peer) &&
            g_chg_verdict[i] == LPRES_STATE_FALSE)
            return true;
    return false;
}

/* Open a session to a peer when none exists and none is in flight. */
static void sess_ensure(vna_host_t *h, const vna_id_t *peer, uint64_t now)
{
    uint8_t addr[6], rnd[128];
    int32_t l;
    int i;
    (void) now;
    /* the chiglet's decided verdict: a member it judged SUSPECT on
     * independent evidence gets no new session (advice enforced at the
     * edge; the evidence itself stays in the audit record) */
    if (chg_suspect(h, peer)) {
        bond_obs(BOND_APP_SEC, BOND_ACT_REFUSE, now);
        return;
    }
    if (sess_slot(peer) >= 0) return;
    if (!vna_contact_addr(h, peer, addr)) return;
    for (i = 0; i < (int) VNAH_SESS_N; i++) {
        if (g_hs_used[i] && g_sess_addr[i][0] == addr[0] && g_sess_addr[i][1] == addr[1] &&
            g_sess_addr[i][2] == addr[2] && g_sess_addr[i][3] == addr[3] &&
            g_sess_addr[i][4] == addr[4] && g_sess_addr[i][5] == addr[5])
            return;
    }
    i = sess_free();
    if (i < 0) return;
    vna_drbg_gen(&h->node.rng, rnd, sizeof rnd);
    l = vna_hs_initiate(&g_hs[i], &h->idn, peer, VNAH_HS_POW, VNA_FEAT_UBH168, rnd, g_hs_wire,
                        sizeof g_hs_wire);
    if (l <= 0) return;
    vna_copy(g_sess_addr[i], addr, 6);
    g_hs_used[i] = 1;
    g_hs_deadline[i] = now + 10000ull;
    (void) vna_sendto(h, addr, g_hs_wire, (uint32_t) l);
}

/* Ask the session's peer for the next chunk the active fetch still needs. */
static void sess_fetch_step(vna_host_t *h, int si)
{
    int32_t next, ql;
    vna_chunk_req_t req;
    if (!g_fetch_active || si < 0) return;
    next = vna_fetch_next(&g_fetch);
    if (next < 0) return;
    vna_zero(&req, sizeof req);
    req.magic = VNA_CHUNKREQ_MAGIC;
    req.root = g_fetch_root;
    req.index = (uint32_t) next;
    ql = vna_schema_pack(&vna_chunk_req_schema, &req, g_sess_pt, sizeof g_sess_pt, true);
    if (ql > 0) sess_send(h, si, VNAH_REC_F_REQ, g_sess_pt, (uint32_t) ql);
}

/* Armada index sync: each cycle the top-ranked entries go out as CISX
 * envelopes under digest-derived keys — every node's index converges on
 * the same verified evidence, and a peer's merge kjoins our regional
 * observations into its own. */
/* The scenic mesh bypass: the record plane crosses the open HTTP while
 * the UDP sessions wait on the firewall. Every settle, the node posts
 * its top CISEX envelopes (CISEX_OP_IDX) to each seed's door /cisex —
 * the peers merge them, so the armada masks and the index propagate
 * over port 80 today. The UDP path takes over when the firewall opens. */
static void vna_http_mesh(vna_host_t *h, uint64_t now)
{
    static sn_http_t mh;
    uint32_t top[CISEX_RT_MAX_ENV];
    uint64_t tr[CISEX_RT_MAX_ENV];
    uint32_t i, n = 0, k, p;
    static const char *why;
    (void) now;
    for (i = 0; i < g_cisex.count; i++) {
        const cisex_ent_t *e = cisex_at(&g_cisex, i);
        uint64_t r;
        uint32_t j;
        if (!e || !e->used) continue;
        r = cisex_rank(e);
        if (r == 0) continue;
        for (j = n; j > 0 && tr[j - 1] < r; j--) {
            if (j < CISEX_RT_MAX_ENV) {
                tr[j] = tr[j - 1];
                top[j] = top[j - 1];
            }
        }
        if (j < CISEX_RT_MAX_ENV) {
            tr[j] = r;
            top[j] = i;
            if (n < CISEX_RT_MAX_ENV) n++;
        }
    }
    if (!n) return;
    for (p = 0; p < h->nseed_addr; p++) {
        const uint8_t *s = h->seed_addr[p];
        uint32_t d = 0;
        for (i = 0; i < 4; i++) d = d * 256 + s[i];
        mh.l = h->l;   /* the link itself, not its address (h->l is sn_link_t *) */
        mh.port = 80; /* the door */
        mh.ip[0] = s[0];
        mh.ip[1] = s[1];
        mh.ip[2] = s[2];
        mh.ip[3] = s[3];
        mh.resolved = 1;
        mh.sock = -1;
        mh.stall_ms = 1000; /* hard bound: the mesh never blocks the settle */
        mh.bad = 0;
        {
            unsigned q = 0;
            uint8_t *hst = (uint8_t *) mh.host;
            for (i = 0; i < 4; i++) {
                uint32_t v = s[i];
                char tmp[4];
                unsigned t = 0;
                if (!v) tmp[t++] = '0';
                while (v) {
                    tmp[t++] = (char) ('0' + (v % 10));
                    v /= 10;
                }
                while (t) hst[q++] = tmp[--t];
                if (i < 3) hst[q++] = '.';
            }
            hst[q] = 0;
        }
        for (k = 0; k < n && k < 4; k++) {
            const cisex_ent_t *e = cisex_at(&g_cisex, top[k]);
            uint8_t env[CISEX_ENV_BYTES];
            uint8_t body[2 + CISEX_ENV_BYTES];
            uint8_t resp[4];
            uint32_t rlen = 0;
            if (!e) continue;
            if (cisex_pack_idx(e, g_cisex_region, env) != IPFSN_OK) continue;
            body[0] = CISEX_OP_IDX;
            body[1] = g_cisex_region;
            for (i = 0; i < CISEX_ENV_BYTES; i++) body[2 + i] = env[i];
            if (sn_http_post(&mh, "/cisex", body, 2 + CISEX_ENV_BYTES, resp, sizeof resp, &rlen,
                             &why) == 0) {
                h->mesh_posts++;
            } else {
                break; /* the peer is away; try the next seed */
            }
        }
    }
}

static void cisex_idx_sync(vna_host_t *h, uint64_t now)
{
    uint32_t top[CISEX_RT_MAX_ENV];
    uint64_t tr[CISEX_RT_MAX_ENV];
    uint32_t i, n = 0, k;
    for (i = 0; i < g_cisex.count; i++) {
        const cisex_ent_t *e = cisex_at(&g_cisex, i);
        uint64_t r;
        uint32_t j;
        if (!e || !e->used) continue;
        r = cisex_rank(e);
        if (r == 0) continue;
        for (j = n; j > 0 && tr[j - 1] < r; j--) {
            if (j < CISEX_RT_MAX_ENV) {
                tr[j] = tr[j - 1];
                top[j] = top[j - 1];
            }
        }
        if (j < CISEX_RT_MAX_ENV) {
            tr[j] = r;
            top[j] = i;
            if (n < CISEX_RT_MAX_ENV) n++;
        }
    }
    for (k = 0; k < n; k++) {
        const cisex_ent_t *e = cisex_at(&g_cisex, top[k]);
        uint8_t env[CISEX_ENV_BYTES];
        vna_id_t key;
        if (e && cisex_pack_idx(e, g_cisex_region, env) == IPFSN_OK) {
            vna_htag("zxv/cisex/idx/v1", e->digest, 32, key.b);
            vna_env_publish(h, &key, env, now, VNAH_ENV_TTL);
            h->idx_out++;
        }
    }
}

/* A provider record for a file we do not share: start a fetch through the
 * provider's session (established on demand). */
static void sess_fetch_begin(vna_host_t *h, const vna_id_t *root, const vna_id_t *provider,
                             const vna_provider_t *pv, uint64_t now)
{
    int si;
    if (g_fetch_active || pv->chunk_size != VNA_CHUNK_SIZE || pv->n_chunks > 8u ||
        pv->file_size > sizeof g_fetch_data)
        return;
    if (vna_fetch_init(&g_fetch, root, pv->file_size, pv->chunk_size, g_fetch_bitmap,
                       sizeof g_fetch_bitmap) != VNA_OK)
        return;
    g_fetch_root = *root;
    g_fetch_size = (uint32_t) pv->file_size;
    g_fetch_deadline = now + VNAH_FETCH_MS;
    g_fetch_active = 1;
    /* demand is evidence too: the wantlist stub carries it until blocks
     * arrive and observations join in */
    (void) cisex_want(&g_cisex, g_fetch_root.b, g_cisex_region);
    si = sess_slot(provider);
    if (si < 0)
        sess_ensure(h, provider, now); /* the poll asks once the session lands */
    else
        sess_fetch_step(h, si);
}

static void vna_sess_rx(vna_host_t *h, const uint8_t a[6], const uint8_t *data, uint32_t len,
                        uint64_t now)
{
    uint32_t magic = (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) |
                     ((uint32_t) data[3] << 24);
    uint8_t rnd[128];
    int32_t l;
    int i, j;
    if (magic == VNA_HS1_MAGIC) {
        vna_id_t claimed;
        i = -1;
        for (j = 0; j < (int) VNAH_SESS_N; j++) {
            if (g_hs_used[j] && !g_hs[j].initiator && g_sess_addr[j][0] == a[0] &&
                g_sess_addr[j][1] == a[1] && g_sess_addr[j][2] == a[2] &&
                g_sess_addr[j][3] == a[3] && g_sess_addr[j][4] == a[4] &&
                g_sess_addr[j][5] == a[5]) {
                i = j;
                break;
            }
        }
        if (i < 0) i = sess_free();
        if (i < 0) return;
        if (vna_hs_peek_initiator(data, len, &claimed) == VNA_OK) {
            (void) vna_book_learn(h, &claimed);
            if (chg_suspect(h, &claimed)) return;
            if (!vna_is_seed(h, a) &&
                vna_agree_check(&h->agr, &h->us, &claimed, vna_book_trust(&h->book, &claimed),
                                VNA_RES_RECORD, NULL, 1, 0, now, false) != VNA_OK)
                return;
        }
        vna_drbg_gen(&h->node.rng, rnd, sizeof rnd);
        l = vna_hs_respond(&g_hs[i], &h->idn, VNAH_HS_POW, VNA_FEAT_UBH168, data, len, rnd,
                           g_hs_wire, sizeof g_hs_wire);
        if (l > 0) {
            g_hs_used[i] = 1;
            g_hs_deadline[i] = now + 10000ull;
            vna_copy(g_sess_addr[i], a, 6);
            (void) vna_sendto(h, a, g_hs_wire, (uint32_t) l);
        }
        return;
    }
    if (magic == VNA_HS2_MAGIC) {
        for (i = 0; i < (int) VNAH_SESS_N; i++) {
            if (!g_hs_used[i] || !g_hs[i].initiator || g_hs[i].state != VNA_HS_SENT_M1) continue;
            if (len >= 41u && !vna_id_eq(&g_hs[i].peer, (const vna_id_t *) (data + 9))) continue;
            vna_drbg_gen(&h->node.rng, rnd, 32);
            l = vna_hs_initiator_finish(&g_hs[i], data, len, rnd, g_hs_wire, sizeof g_hs_wire,
                                        &g_sess[i]);
            if (l > 0) {
                vna_copy(g_sess_addr[i], a, 6);
                (void) vna_sendto(h, a, g_hs_wire, (uint32_t) l);
                /* the authenticated identity now: a DECIDED-suspect peer
                 * never gets the established channel */
                if (chg_suspect(h, &g_sess[i].peer)) {
                    bond_obs(BOND_APP_SEC, BOND_ACT_REFUSE, now);
                    vna_zero(&g_sess[i], sizeof g_sess[i]);
                } else {
                    g_sess[i].established = true;
                    g_sess_est++;
                    bond_obs(BOND_APP_VNA, BOND_ACT_SESS, now);
                }
            }
            vna_hs_clear(&g_hs[i]);
            g_hs_used[i] = 0;
            return;
        }
        return;
    }
    if (magic == VNA_HS3_MAGIC) {
        /* we responded: finish against the in-flight responder that verifies */
        for (i = 0; i < (int) VNAH_SESS_N; i++) {
            if (!g_hs_used[i] || g_hs[i].initiator || g_hs[i].state != VNA_HS_SENT_M2) continue;
            if (vna_hs_responder_finish(&g_hs[i], data, len, &g_sess[i]) == VNA_OK) {
                vna_copy(g_sess_addr[i], a, 6);
                /* same edge on the responder side: the peer is
                 * authenticated here, so a DECIDED-suspect refuses */
                if (chg_suspect(h, &g_sess[i].peer)) {
                    bond_obs(BOND_APP_SEC, BOND_ACT_REFUSE, now);
                    vna_zero(&g_sess[i], sizeof g_sess[i]);
                } else {
                    g_sess[i].established = true;
                    g_sess_est++;
                    bond_obs(BOND_APP_VNA, BOND_ACT_SESS, now);
                }
            }
            vna_hs_clear(&g_hs[i]);
            g_hs_used[i] = 0;
            return;
        }
        return;
    }
    /* not a handshake: a sealed record for an established session */
    {
        uint8_t type = 0;
        uint32_t pl = 0;
        i = -1;
        for (j = 0; j < (int) VNAH_SESS_N; j++) {
            uint64_t rseq = 0;
            if (!g_sess[j].established) continue;
            if (g_sess_addr[j][0] != a[0] || g_sess_addr[j][1] != a[1] ||
                g_sess_addr[j][2] != a[2] || g_sess_addr[j][3] != a[3])
                continue;
            if (len >= 9u) {
                rseq = (uint64_t) data[1] | ((uint64_t) data[2] << 8) |
                       ((uint64_t) data[3] << 16) | ((uint64_t) data[4] << 24) |
                       ((uint64_t) data[5] << 32) | ((uint64_t) data[6] << 40) |
                       ((uint64_t) data[7] << 48) | ((uint64_t) data[8] << 56);
            }
            if (vna_sess_open(&g_sess[j], data, len, g_sess_pt, sizeof g_sess_pt, &type, &pl) ==
                VNA_OK) {
                if (pl > 0) {
                    scp144_tunnel_expand(g_sess_pt, pl, g_sess[j].session_id, rseq);
                    g_casimir_cavity.parts_opened++;
                }
                i = j;
                break;
            }
        }
        if (i < 0) return;
        if (type == VNAH_REC_F_REQ) {
            int32_t rl = -1;
            uint64_t trust = vna_book_trust(&h->book, &g_sess[i].peer);
            for (j = 0; j < (int) VNAH_SF_N && rl <= 0; j++)
                rl = vna_file_serve(&g_sf[j], &h->agr, &h->us, &g_sess[i].peer, trust, g_sess_pt,
                                    pl, now, &g_chunk, g_sess_pt, sizeof g_sess_pt);
            for (j = 0; j < (int) VNAH_EXT_N && rl <= 0; j++)
                if (g_ext_used[j])
                    rl = vna_file_serve(&g_ext_sf[j], &h->agr, &h->us, &g_sess[i].peer, trust,
                                        g_sess_pt, pl, now, &g_chunk, g_sess_pt, sizeof g_sess_pt);
            if (rl > 0) {
                g_chunks_served++;
                bond_obs(BOND_APP_VNA, BOND_ACT_SERVE, now);
                sess_send(h, i, VNAH_REC_F_REP, g_sess_pt, (uint32_t) rl);
            }
            return;
        }
        if (type == VNAH_REC_F_REP) {
            if (!g_fetch_active) return;
            if (vna_fetch_accept(&g_fetch, g_sess_pt, pl, &g_chunk) == VNA_OK &&
                g_chunk.index < 8u &&
                (uint64_t) g_chunk.index * VNA_CHUNK_SIZE + g_chunk.data_len <=
                    sizeof g_fetch_data) {
                vna_copy(g_fetch_data + (uint64_t) g_chunk.index * VNA_CHUNK_SIZE, g_chunk.data,
                         g_chunk.data_len);
                g_chunks_in++;
                if (vna_fetch_complete(&g_fetch)) {
                    uint32_t e;
                    uint8_t fkind = 0;
                    uint16_t fent = 0;
                    g_fetch_active = 0;
                    g_fetch_done++;
                    bond_obs(BOND_APP_VNA, BOND_ACT_FETCH, now);
                    /* first-chunk classifier on the verified bytes */
                    (void) cisex_peek(&g_cisex, g_fetch_root.b, g_fetch_data,
                                      g_fetch_size > CISEX_HDR_PEEK ? CISEX_HDR_PEEK : g_fetch_size,
                                      g_cisex_region, &fkind, &fent);
                    (void) cisex_observe(&g_cisex, g_fetch_root.b, g_cisex_region,
                                         LPRES_STATE_TRUE);
                    /* become a seeder for what we just verified — the
                     * staging bucket evicts by demand velocity, so a
                     * rising micro-trend displaces a fading one */
                    for (e = 0; e < VNAH_EXT_N; e++)
                        if (!g_ext_used[e]) break;
                    if (e == VNAH_EXT_N) {
                        uint32_t lo = 0;
                        uint16_t lv = VNAH_VEL_MAX;
                        for (j = 0; j < (int) VNAH_EXT_N; j++) {
                            uint16_t v = 0;
                            for (i = 0; i < (int) g_cisex.count; i++)
                                if (g_cisex_ents[i].used &&
                                    vna_id_eq(&g_ext_sf[j].root,
                                              (const vna_id_t *) g_cisex_ents[i].digest))
                                    v = g_vel[i];
                            if (v < lv) {
                                lv = v;
                                lo = j;
                            }
                        }
                        e = lo;
                        g_ext_used[e] = 0;
                        g_pin_evicts++;
                        bond_obs(BOND_APP_IDX, BOND_ACT_PIN, now);
                    }
                    if (e < VNAH_EXT_N) {
                        vna_copy(g_ext_data[e], g_fetch_data, g_fetch_size);
                        if (vna_file_build(g_ext_data[e], g_fetch_size, VNA_CHUNK_SIZE,
                                           g_ext_leaves[e], 8, &g_ext_sf[e].root) == g_fetch.n &&
                            vna_id_eq(&g_ext_sf[e].root, &g_fetch_root)) {
                            g_ext_sf[e].size = g_fetch_size;
                            g_ext_sf[e].chunk_size = VNA_CHUNK_SIZE;
                            g_ext_sf[e].n = g_fetch.n;
                            g_ext_sf[e].leaves = (const uint8_t(*)[32]) g_ext_leaves[e];
                            g_ext_sf[e].data = g_ext_data[e];
                            g_ext_used[e] = 1;
                            g_pins++;
                        }
                    }
                } else
                    sess_fetch_step(h, i);
            }
            return;
        }
        if (type == VNAH_REC_CISEX_Q) {
            uint8_t query[258];
            uint32_t reply_len = 0;
            int qr;
            if (pl > sizeof query) return;
            vna_copy(query, g_sess_pt, pl);
            if (pl == 2u + CISEX_ENV_BYTES && query[0] == CISEX_OP_IDX) {
                h->idx_in++;
                (void) vna_agree_check(&h->agr, &h->us, &g_sess[i].peer,
                                       vna_book_trust(&h->book, &g_sess[i].peer), VNA_RES_RECORD,
                                       NULL, 1, 0, now, true);
            }
            qr = cisex_rt_handle(&g_cisex, query, pl, g_cisex_region, g_sess_pt, sizeof g_sess_pt,
                                 &reply_len);
            if (reply_len > 0) {
                cisex_vel_bump(g_sess_pt, reply_len); /* demand over sessions */
                sess_send(h, i, VNAH_REC_CISEX_R, g_sess_pt, reply_len);
            } else if (qr < 0)
                h->refused++;
            return;
        }
        if (type == VNAH_REC_V_REQ) {
            /* host side of a visit: [dna 153][grant 105][sig_v 4627]. The
             * visitor signs the grant it built; the host's countersignature
             * IS the admission — only then are both owners on the bytes. */
            chgk_dna_t vd;
            chgk_grant_t gt, gv;
            const uint8_t *pkv;
            uint8_t sigh[VNA_SIG_LEN], rnd2[32];
            uint8_t st = 0xfe;
            if (g_chg_alive && pl == CHGK_DNA_ENC + CHGK_GRANT_ENC + VNA_SIG_LEN &&
                chgk_dna_decode(g_sess_pt, CHGK_DNA_ENC, &vd) == CHGK_OK &&
                chgk_grant_decode(g_sess_pt + CHGK_DNA_ENC, CHGK_GRANT_ENC, &gt) == CHGK_OK &&
                byte_eq32(gt.visitor, vd.id) &&
                gt.depth <= CHGK_DEPTH_VERDICTS &&
                (pkv = vna_keycache_get(&h->node.kc, &g_sess[i].peer)) != NULL) {
                vna_drbg_gen(&h->node.rng, rnd2, 32);
                vna_sign(h->idn.sk, "zxv/chgk/grant/v1", g_sess_pt + CHGK_DNA_ENC, CHGK_GRANT_ENC,
                         rnd2, sigh);
                if (chgk_grant_verify(g_sess_pt + CHGK_DNA_ENC, CHGK_GRANT_ENC, pkv, h->idn.pk,
                                      VNA_PK_LEN, g_sess_pt + CHGK_DNA_ENC + CHGK_GRANT_ENC, sigh,
                                      VNA_SIG_LEN, chg_pk_verify, NULL, &gv) == CHGK_OK) {
                    vna_copy(gv.host, g_self_dna.id, 32);
                    if (chgk_visit_open(&g_visit[i], &g_xp, &g_gbook, &gv, &g_self_dna, &vd,
                                        now / 1000ull) == CHGK_OK) {
                        g_visit_dna[i] = vd;
                        g_chg_visits++;
                        bond_obs(BOND_APP_KIN, BOND_ACT_VISIT, now);
                        st = 0;
                    }
                }
            }
            g_sess_pt[0] = st;
            if (st == 0) {
                uint32_t dl = chgk_dna_encode(&g_self_dna, g_sess_pt + 1, CHGK_DNA_ENC);
                sess_send(h, i, VNAH_REC_V_ACK, g_sess_pt, 1 + dl);
            } else {
                sess_send(h, i, VNAH_REC_V_ACK, g_sess_pt, 1);
            }
            return;
        }
        if (type == VNAH_REC_V_ACK) {
            /* initiator side: the host's verdict on our visit request */
            if (!g_visit_wait[i]) return;
            g_visit_wait[i] = 0;
            if (pl >= 1 && g_sess_pt[0] == 0 && pl >= 1 + CHGK_DNA_ENC &&
                chgk_dna_decode(g_sess_pt + 1, CHGK_DNA_ENC, &g_visit_dna[i]) == CHGK_OK) {
                vna_copy(g_visit_out[i].host, g_visit_dna[i].id, 32);
                if (chgk_visit_open(&g_visit[i], &g_xp, &g_gbook, &g_visit_out[i], &g_self_dna,
                                    &g_visit_dna[i], now / 1000ull) == CHGK_OK) {
                    g_chg_visits++;
                    bond_obs(BOND_APP_KIN, BOND_ACT_VISIT, now);
                }
            }
            return;
        }
        if (type == VNAH_REC_V_CLAIM) {
            /* [kind][topic][verdict][claim 32][evidence 32] — the claim's own
             * scope/depth rules apply before our checker is even asked */
            chgk_claim_t cl;
            uint32_t xp = 0;
            uint8_t st = 0xfe;
            {
                int vi = i;
                if (!g_visit[vi].open) {
                    for (j = 0; j < (int) VNAH_SESS_N; j++)
                        if (g_visit[j].open && vna_id_eq(&g_sess[j].peer, &g_sess[i].peer)) {
                            vi = j;
                            break;
                        }
                }
                if (g_visit[vi].open && pl == 67u) {
                    vna_zero(&cl, sizeof cl);
                    cl.kind = g_sess_pt[0];
                    cl.topic = g_sess_pt[1];
                    cl.verdict = g_sess_pt[2];
                    vna_copy(cl.claim, g_sess_pt + 3, 32);
                    vna_copy(cl.evidence, g_sess_pt + 35, 32);
                    g_chg_claims_in++;
                    bond_obs(BOND_APP_KIN, BOND_ACT_CLAIM, now);
                    if (chgk_visit_receive(&g_visit[vi], &g_xp, &cl, chg_claim_checker(h, &cl),
                                           now / 1000ull, &xp) == CHGK_OK) {
                        st = 0;
                        g_chg_xp_gain += xp;
                    }
                }
            }
            g_sess_pt[0] = st;
            g_sess_pt[1] = (uint8_t) xp;
            g_sess_pt[2] = (uint8_t) (xp >> 8);
            g_sess_pt[3] = (uint8_t) (xp >> 16);
            g_sess_pt[4] = (uint8_t) (xp >> 24);
            sess_send(h, i, VNAH_REC_V_CACK, g_sess_pt, 5);
            return;
        }
        if (type == VNAH_REC_V_CACK || type == VNAH_REC_V_END) {
            if (type == VNAH_REC_V_END) {
                chgk_visit_close(&g_visit[i]);
                vna_zero(&g_visit_dna[i], sizeof g_visit_dna[i]);
            }
            return;
        }
    }
}

/* Offer one visit on an established session whose peer's chiglet genome we
 * hold: build the grant (both parties, a bounded window), sign it as the
 * visitor, and send it — the host's countersignature completes it. */
static void chg_visit_try(vna_host_t *h, int si, uint64_t now)
{
    uint32_t k;
    chgk_grant_t *g;
    uint8_t rnd2[160];
    uint32_t dl, gl;
    if (g_visit[si].open || g_visit_wait[si]) return;
    for (k = 0; k < VNAH_PD_N; k++)
        if (g_peer_dna[k].used && vna_id_eq(&g_peer_dna[k].publisher, &g_sess[si].peer)) break;
    if (k == VNAH_PD_N) {
        uint32_t pi, slot = VNAH_PD_N;
        uint8_t sp[32], ln[32];
        uint64_t pseed = VNAH_CHG_SEED_BASE;
        for (pi = 0; pi < h->nseed_addr; pi++) {
            if (h->seed_addr[pi][0] == g_sess_addr[si][0] &&
                h->seed_addr[pi][1] == g_sess_addr[si][1] &&
                h->seed_addr[pi][2] == g_sess_addr[si][2] &&
                h->seed_addr[pi][3] == g_sess_addr[si][3]) {
                pseed = (uint64_t) pi + VNAH_CHG_SEED_BASE;
                break;
            }
        }
        for (pi = 0; pi < VNAH_PD_N; pi++) {
            if (!g_peer_dna[pi].used) {
                slot = pi;
                break;
            }
        }
        if (slot < VNAH_PD_N) {
            sha256_tag("zxv/chg/species/node/v1", sp);
            sha256_tag("zxv/chg/lineage/fleet/0", ln);
            if (chgk_dna_birth(&g_kreg, g_sess[si].peer.b, sp, ln, NULL, pseed,
                               &g_peer_dna[slot].dna) == CHGK_OK) {
                g_peer_dna[slot].publisher = g_sess[si].peer;
                g_peer_dna[slot].used = 1;
                g_chg_dna_in++;
                k = slot;
            }
        }
    }
    if (k == VNAH_PD_N) return;
    g = &g_visit_out[si];
    vna_zero(g, sizeof *g);
    vna_drbg_gen(&h->node.rng, rnd2, sizeof rnd2);
    vna_copy(g->gid, rnd2, 16);
    vna_copy(g->visitor, g_self_dna.id, 32);
    vna_copy(g->host, g_peer_dna[k].dna.id, 32);
    g->topics = VNAH_CHG_TOPICS;
    g->depth = CHGK_DEPTH_VERDICTS;
    g->not_before = now / 1000ull;
    g->expires = g->not_before + VNAH_CHG_VISIT_S;
    dl = chgk_dna_encode(&g_self_dna, g_sess_pt, CHGK_DNA_ENC);
    gl = chgk_grant_encode(g, g_sess_pt + dl, CHGK_GRANT_ENC);
    if (dl != CHGK_DNA_ENC || gl != CHGK_GRANT_ENC) return;
    vna_sign(h->idn.sk, "zxv/chgk/grant/v1", g_sess_pt + dl, gl, rnd2 + 16, g_sess_pt + dl + gl);
    sess_send(h, si, VNAH_REC_V_REQ, g_sess_pt, dl + gl + VNA_SIG_LEN);
    g_visit_wait[si] = 1;
}

/* Demand velocity: every KQ/DQ reply envelope's digest bumps its entry.
 * Reply layout: [tag][n][env 168B...], digest at env+21. */
static void cisex_vel_bump(const uint8_t *reply, uint32_t len)
{
    uint32_t i, j;
    if (!reply || len < 2) return;
    if (reply[0] != CISEX_RSP_KQ && reply[0] != CISEX_RSP_DQ) return;
    for (i = 0; i < reply[1] && 2 + (i + 1) * CISEX_ENV_BYTES <= len; i++) {
        const uint8_t *dg = reply + 2 + i * CISEX_ENV_BYTES + 21;
        for (j = 0; j < g_cisex.count; j++) {
            const cisex_ent_t *e = &g_cisex_ents[j];
            uint32_t k;
            if (!e->used) continue;
            for (k = 0; k < 32 && e->digest[k] == dg[k]; k++) {
            }
            if (k == 32 && g_vel[j] < VNAH_VEL_MAX) g_vel[j]++;
        }
    }
}

/* One verdict claim over an open visit: our judge's current verdict on a
 * book member, the member id as the referent, our evidence bundle hashed.
 * Rotates members; verdicts travel only while the visit's grant allows. */
static void chg_claim_send(vna_host_t *h, int si, uint64_t now)
{
    static uint32_t rotate;
    uint32_t j, tries;
    chgk_claim_t cl;
    (void) now;
    if (!g_visit[si].open || !(g_visit[si].g.topics & (1u << VNAH_CHG_TOPIC_MEM))) return;
    for (tries = 0; tries < h->book.cap && tries < VNAH_BOOK_CAP; tries++) {
        j = (rotate + tries) % (h->book.cap < VNAH_BOOK_CAP ? h->book.cap : VNAH_BOOK_CAP);
        const vna_account_t *ac = &h->book.acct[j];
        /* NEITHER carries no evidence; T, F and BOTH all do */
        if (ac->used && ac->member && g_chg_verdict[j] != LPRES_STATE_NEITHER) break;
    }
    if (tries >= h->book.cap || tries >= VNAH_BOOK_CAP) return;
    rotate = (j + 1u) % (h->book.cap < VNAH_BOOK_CAP ? h->book.cap : VNAH_BOOK_CAP);
    vna_zero(&cl, sizeof cl);
    cl.kind = CHGK_K_VERDICT;
    cl.topic = VNAH_CHG_TOPIC_MEM;
    /* lpres numbering on the wire: NEITHER 0 TRUE 1 FALSE 2 BOTH 3 */
    cl.verdict = g_chg_verdict[j];
    vna_copy(cl.claim, h->book.acct[j].id.b, 32);
    {
        /* the evidence bundle: the four axes we judged, hashed, so the
         * receiving chiglet can tell its own evidence apart from ours */
        uint8_t eb[32 + 4];
        vna_copy(eb, h->book.acct[j].id.b, 32);
        eb[32] = 1; /* receipts */
        eb[33] = 2; /* usage */
        eb[34] = 3; /* smic admission */
        eb[35] = 4; /* recency */
        sha256_ctx_t sc;
        sha256_init(&sc);
        sha256_update(&sc, eb, sizeof eb);
        sha256_final(&sc, cl.evidence);
    }
    g_sess_pt[0] = cl.kind;
    g_sess_pt[1] = cl.topic;
    g_sess_pt[2] = cl.verdict;
    vna_copy(g_sess_pt + 3, cl.claim, 32);
    vna_copy(g_sess_pt + 35, cl.evidence, 32);
    sess_send(h, si, VNAH_REC_V_CLAIM, g_sess_pt, 67);
    g_chg_claims_out++;
}

/* Session upkeep on the host cadence: handshakes to book members, the active
 * fetch's next request, fetch expiry, and the chiglet's social rounds. */
static void vna_sess_poll(vna_host_t *h, uint64_t now)
{
    uint32_t i;
    for (i = 0; i < VNAH_SESS_N; i++) {
        if (g_hs_used[i] && now > g_hs_deadline[i]) {
            vna_hs_clear(&g_hs[i]);
            g_hs_used[i] = 0;
        }
    }
    for (i = 0; i < h->node.rt.cap; i++) {
        const vna_contact_t *c = &h->node.rt.pool[i];
        if (c->in_use) {
            (void) vna_book_learn(h, &c->id);
            sess_ensure(h, &c->id, now);
        }
    }
    for (i = 0; i < h->book.cap; i++) {
        const vna_account_t *ac = &h->book.acct[i];
        if (ac->used && ac->member) sess_ensure(h, &ac->id, now);
    }
    if (g_chg_alive) {
        int opened = 0;
        vna_chg_audit(h, now);
        for (i = 0; i < VNAH_SESS_N && !opened; i++)
            if (g_sess[i].established && !g_visit[i].open && !g_visit_wait[i]) {
                chg_visit_try(h, (int) i, now);
                opened = g_visit_wait[i];
            }
        for (i = 0; i < VNAH_SESS_N; i++)
            if (g_sess[i].established && g_visit[i].open) {
                if (now / 1000ull >= g_visit[i].g.expires) {
                    chgk_visit_close(&g_visit[i]);
                } else {
                    chg_claim_send(h, (int) i, now);
                }
            }
        if (g_cisex.count > 0) {
            static uint32_t sess_cisex_rot;
            for (i = 0; i < VNAH_SESS_N; i++) {
                if (!g_sess[i].established) continue;
                const cisex_ent_t *ce = cisex_at(&g_cisex, sess_cisex_rot % g_cisex.count);
                sess_cisex_rot++;
                if (ce && ce->used) {
                    uint8_t cenv[CISEX_ENV_BYTES];
                    if (cisex_pack_idx(ce, g_cisex_region, cenv) == IPFSN_OK) {
                        g_sess_pt[0] = CISEX_OP_IDX;
                        g_sess_pt[1] = g_cisex_region;
                        vna_copy(g_sess_pt + 2, cenv, CISEX_ENV_BYTES);
                        sess_send(h, (int) i, VNAH_REC_CISEX_Q, g_sess_pt, 2u + CISEX_ENV_BYTES);
                        h->idx_out++;
                    }
                }
            }
        }
    }
    if (!g_fetch_active) return;
    if (now > g_fetch_deadline) g_fetch_active = 0;
}

static int32_t door_cisex(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0;
    (void) ctx;
    if (in_len >= 3 && in[0] == 0x06u) {
        ipfsn_cid_t cid;
        uint32_t codec = in[1] == 0x70u ? IPFSN_MC_DAG_PB : IPFSN_MC_RAW;
        if (cap < 1) return -1;
        if (ipfsn_cid_sha256(codec, in + 2, in_len - 2, &cid) != IPFSN_OK) {
            out[0] = CISEX_ACK_BAD;
            return 1;
        }
        if (pin_ingest_block(&cid, in + 2, in_len - 2, g_cisex_region) != 0) {
            out[0] = CISEX_ACK_FULL;
            return 1;
        }
        out[0] = CISEX_ACK_OK;
        return 1;
    }
    if (cisex_rt_handle(&g_cisex, in, in_len, g_cisex_region, out, cap, &n) != IPFSN_OK) return -1;
    if (in_len == 2u + CISEX_ENV_BYTES && in[0] == CISEX_OP_IDX && n >= 1 && out[0] == CISEX_ACK_OK) {
        cisex_wire_t w;
        if (cisex_unpack_idx(in + 2, &w) == IPFSN_OK) {
            ipfsn_cid_t c;
            vna_zero(&c, sizeof c);
            c.version = (w.codec == IPFSN_MC_DAG_PB) ? 0u : 1u;
            c.codec = w.codec ? w.codec : IPFSN_MC_DAG_PB;
            c.mh_code = IPFSN_MH_SHA2_256;
            c.digest_len = 32u;
            vna_copy(c.digest, w.digest, 32u);
            pin_q_enqueue(&c);
        }
    }
    cisex_vel_bump(out, n);
    return (int32_t) n;
}

/* The hosted Seed Banks and the master catalog over them. The banks
 * themselves load from ZXVFS at boot (the bank files on the data
 * partition); until then the store is empty and every lookup misses. */
sb_bank_t G_sbanks[SB_CAT_MAX]; /* non-static: seedbank_blob.c registers into them */
uint32_t G_sbank_n;
sb_catalog_t G_catalog;
static uint32_t G_bank_hits, G_bank_misses;

/* POST /bank: [digest 32] -> [status][bank u16][len u32][offset u32],
 * then the seed's bytes when they fit the door's reply budget. status 0 =
 * hosted and served; 1 = not hosted here (bank 0xFFFF, len 0). */
static int32_t door_bank(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t cap)
{
    uint32_t len = 0, bank = SB_CAT_MAX, off = 0;
    const uint8_t *p;
    (void) ctx;
    if (in_len != SB_DIGEST_LEN || cap < 11) return -1;
    p = sb_catalog_find(&G_catalog, in, &len, &bank);
    if (p) {
        G_bank_hits++;
        off = (uint32_t) (p - (G_sbanks[bank].base + G_sbanks[bank].payload_off));
    } else {
        G_bank_misses++;
    }
    out[0] = p ? 0 : 1;
    out[1] = (uint8_t) (bank >> 8);
    out[2] = (uint8_t) bank;
    out[3] = (uint8_t) (len >> 24);
    out[4] = (uint8_t) (len >> 16);
    out[5] = (uint8_t) (len >> 8);
    out[6] = (uint8_t) len;
    out[7] = (uint8_t) (off >> 24);
    out[8] = (uint8_t) (off >> 16);
    out[9] = (uint8_t) (off >> 8);
    out[10] = (uint8_t) off;
    if (p && len <= cap - 11) {
        for (uint32_t i = 0; i < len; i++) out[11 + i] = p[i];
        return (int32_t) (11 + len);
    }
    return 11;
}

/* POST /telemetry: the node's counters as key=value lines — the
 * analytics plane's surface, human- and machine-readable alike. */
static int32_t door_telemetry(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out,
                              uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint32_t n = 0;
    char *p = (char *) out;
    int32_t c;
    (void) in;
    if (in_len != 0 || !cap) return -1;
    c = snprintf(p + n, cap - n,
                 "vna.up=%u\n"
                 "vna.seeds=%u\n"
                 "vna.nseed=%u\n"
                 "vna.rx=%u\n"
                 "vna.tx=%u\n"
                 "vna.contacts=%u\n"
                 "vna.sessions=%u\n"
                 "vna.rej=%u\n"
                 "vna.refused=%u\n"
                 "casimir.csr=%u\n"
                 "casimir.throat=%u\n"
                 "casimir.ac_cycles=%u\n"
                 "casimir.pos_waves=%u\n"
                 "casimir.neg_waves=%u\n"
                 "casimir.parts_sealed=%u\n"
                 "casimir.parts_opened=%u\n"
                 "cisex.entries=%u\n"
                 "cisex.indexed=%u\n"
                 "cisex.sync_in=%u\n"
                 "cisex.sync_out=%u\n"
                 "pins=%u\n"
                 "pin.queued=%u\n"
                 "ipfs.downloads=%u\n"
                 "ipfs.bytes=%u\n"
                 "smic.nodes=%u\n"
                 "smic.quar=%u\n"
                 "cell.octane=%u\n"
                 "chg.alive=%u\n"
                 "chg.decided=%u\n"
                 "chg.uncertain=%u\n"
                 "chg.visits=%u\n"
                 "chg.claims_in=%u\n"
                 "chg.claims_out=%u\n"
                 "chg.xp=%u\n"
                 "chg.bond=%u\n"
                 "rej.parse=%u\n"
                 "rej.dst=%u\n"
                 "rej.dup=%u\n"
                 "rej.stale=%u\n"
                 "rej.pow=%u\n"
                 "rej.binding=%u\n"
                 "rej.sig=%u\n"
                 "econ.cycles=%u\n"
                 "econ.sold=%u\n"
                 "econ.earned=%u\n"
                 "econ.imported=%u\n"
                 "bunker.heal=%u\n"
                 "bunker.bounces=%u\n"
                 "porter.admitted=%u\n"
                 "porter.rejected=%u\n"
                 "immig.visas=%u\n"
                 "evicts=%u\n"
                 "bank.seeds=%u\n"
                 "bank.bytes=%u\n"
                 "bank.hits=%u\n"
                 "bank.misses=%u\n",
                 h->up, h->seeds, h->nseed_addr,
                 h->recv, h->sent, h->node.rt.used, g_sess_est,
                 h->node.v.rej_parse + h->node.v.rej_dst + h->node.v.rej_dup + h->node.v.rej_stale +
                     h->node.v.rej_pow + h->node.v.rej_binding + h->node.v.rej_sig,
                 h->refused,
                 g_casimir_cavity.csr, (unsigned) g_casimir_cavity.throat_aperture,
                 (uint32_t) g_casimir_cavity.ac_cycle,
                 (uint32_t) g_casimir_cavity.pos_half_cycles,
                 (uint32_t) g_casimir_cavity.neg_half_cycles,
                 (uint32_t) g_casimir_cavity.parts_sealed,
                 (uint32_t) g_casimir_cavity.parts_opened,
                 g_cisex.count, g_cisex.n_indexed, h->idx_in, h->idx_out,
                 g_pins, g_pin_q_count, g_ipfs_downloads, (uint32_t) g_ipfs_bytes_served,
                 g_smic.n, h->smic_quar, (unsigned) g_self_hw.octane,
                 (unsigned) g_chg_alive, g_chg_decided, g_chg_uncertain,
                 g_chg_visits, g_chg_claims_in, g_chg_claims_out, g_chg_xp_gain, g_bond_obs,
                 h->node.v.rej_parse, h->node.v.rej_dst, h->node.v.rej_dup, h->node.v.rej_stale,
                 h->node.v.rej_pow, h->node.v.rej_binding, h->node.v.rej_sig,
                 h->earn_cycles, h->sold, (uint32_t) h->earned_posted, (uint32_t) h->imported_total,
                 g_ev_worst, g_bunker_bounces,
                 (uint32_t) g_porter.total_admitted, (uint32_t) g_porter.total_rejected,
                 g_immig.total_granted,
                 g_pin_evicts, G_sbank_n ? sb_count(&G_sbanks[0]) : 0u, sb_seedbank_blob_bytes(),
                 G_bank_hits, G_bank_misses);
    if (c < 0 || (uint32_t) c >= cap - n) return -1;
    n += (uint32_t) c;
    return (int32_t) n;
}

/* POST /pk: the node's ML-DSA-87 identity public key — the client
 * fetches it once and verifies every /sig against it. */
static int32_t door_pk(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out,
                       uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    if (in_len != 0 || cap < VNA_PK_LEN) return -1;
    for (uint32_t i = 0; i < VNA_PK_LEN; i++) out[i] = h->idn.pk[i];
    return (int32_t) VNA_PK_LEN;
}

/* POST /sig: [digest 32] -> the node's ML-DSA-87 signature over it (the
 * context "ZXV_DOOR" + the digest). The client hashes any door reply,
 * asks here, and verifies — spoofed telemetry, bank metadata or spaces
 * replies become impossible. */
static int32_t door_sig(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out,
                        uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint8_t rnd[32];
    if (in_len != 32 || cap < VNA_SIG_LEN) return -1;
    vna_drbg_gen(&h->node.rng, rnd, sizeof rnd);
    vna_sign(h->idn.sk, "ZXV_DOOR", in, in_len, rnd, out);
    return (int32_t) VNA_SIG_LEN;
}

static int32_t door_card(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint32_t id_len, pin_len;
    if (!h->card_check) return -1;
    /* the sovereign-key seat: [0][pubkey 32][nonce 8][signature 64]. The
     * nonce is the wall-clock second the client signed; a replay within
     * the window seats the same pubkey again, which is idempotent — the
     * seat is the identity, not a token. */
    if (in_len >= 1 + ED25519_PUBLIC_KEY_LEN + 8 + ED25519_SIGNATURE_LEN && in[0] == 0) {
        const uint8_t *pk = in + 1, *nonce = in + 1 + ED25519_PUBLIC_KEY_LEN;
        const uint8_t *sig = nonce + 8;
        uint64_t nsec = 0;
        uint32_t i;
        for (i = 0; i < 8; i++) nsec = nsec * 256 + nonce[i];
        if (g_sntp_done && vna_absdiff(nsec, vna_wall_sec(h)) > 120ull) {
            out[0] = 1;
            h->cards_bad++;
            return 1;
        }
        if (!ed25519_verify(nonce, 8, sig, pk)) {
            out[0] = 1;
            h->cards_bad++;
            return 1;
        }
        out[0] = 0;
        h->cards_ok++;
        bond_obs(BOND_APP_SEC, BOND_ACT_SESS, vna_now(h)); /* a player seated */
        return 1;
    }
    /* [id_len u8][id][pin_len u8][pin]; both bounded, id first */
    if (in_len < 2 || in[0] == 0 || in[0] > 63) return -1;
    id_len = in[0];
    if (in_len < 1 + id_len + 1) return -1;
    pin_len = in[1 + id_len];
    if (pin_len == 0 || pin_len > 63 || in_len != 1 + id_len + 1 + pin_len) return -1;
    if (cap < 1) return -1;
    if (h->card_check(h->card_ctx, in + 1, id_len, in + 2 + id_len, pin_len) == 0) {
        out[0] = 0;
        h->cards_ok++;
        bond_obs(BOND_APP_SEC, BOND_ACT_SESS, vna_now(h)); /* a player seated */
        return 1;
    }
    out[0] = 1;
    h->cards_bad++;
    return 1;
}

static int32_t door_spaces(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out,
                           uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint32_t n = 2, i, count = 0;
    (void) in;
    if (in_len != 0 || cap < 3) return -1;
    out[0] = 0;
    /* flags: bit0 wall clock landed; bit1 anything received on the VNA;
     * bit2 a message was rejected at a gate (the mesh's dead handshake
     * shows as rx=0, or rx>0 with rejects) */
    out[1] = (uint8_t) ((g_sntp_done ? 1u : 0u) |
                        (h->node.spool ? h->node.spool->rx_accepted > 0 ? 2u : 0u : 0u) |
                        (h->node.v.rej_parse + h->node.v.rej_dst + h->node.v.rej_dup +
                                     h->node.v.rej_stale + h->node.v.rej_pow +
                                     h->node.v.rej_binding + h->node.v.rej_sig >
                                 0
                             ? 4u
                             : 0u));
    for (i = 0; i < VNA_SPACE_MAX; i++) {
        const vna_space_t *s = &h->spaces.s[i];
        uint32_t nl;
        if (!s->used || !s->have_gen) continue;
        nl = s->gen.name_len < VNA_SPACE_NAME_MAX ? s->gen.name_len : VNA_SPACE_NAME_MAX - 1;
        if (n + 1 + nl + 4 > cap) break;
        out[n++] = (uint8_t) nl;
        for (uint32_t j = 0; j < nl; j++) out[n++] = s->gen.name[j];
        out[n++] = s->gen.kind;
        out[n++] = s->phase;
        out[n++] = (uint8_t) (s->nm >> 8);
        out[n++] = (uint8_t) s->nm;
        count++;
    }
    out[0] = (uint8_t) count;
    return (int32_t) n;
}

/* The door's POST /smic: an empty body asks for the ring summary; a
 * 168-byte manifest envelope is admitted and re-published to the fleet
 * under its own key, so a manifest POSTed to one node propagates. The
 * reply is the lpx action byte the admission produced. */
static int32_t door_smic(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t cap)
{
    vna_host_t *h = (vna_host_t *) ctx;
    smic_hw_t hw;
    uint16_t mtype;
    lpx_action_t a;
    uint64_t now;
    if (in_len == 0) {
        /* ring summary: one row per registered node, "region,role,admitted,
         * quarantined" — single digits stay inside a byte each */
        uint32_t n = 0, i;
        for (i = 0; i < g_smic.cap && n + 8 <= cap; i++) {
            const smic_node_t *e = &g_smic.ents[i];
            if (!e->used) continue;
            out[n++] = (uint8_t) ('0' + (e->region & 15));
            out[n++] = (uint8_t) ',';
            out[n++] = (uint8_t) ('0' + (e->role & 7));
            out[n++] = (uint8_t) ',';
            out[n++] = (uint8_t) ('0' + ((uint8_t) e->admitted & 3));
            out[n++] = (uint8_t) ',';
            out[n++] = (uint8_t) ('0' + (e->quarantined & 1));
            out[n++] = (uint8_t) '\n';
        }
        /* then the node's own attestation CID: pull the datagram from the
         * blockstore and verify the manifest signature offline */
        if (g_attest_ok && n + 8 + IPFSN_CID_STR_MAX <= cap) {
            char ct[IPFSN_CID_STR_MAX];
            uint32_t m;
            if (ipfsn_ubh_cid_to_text(&g_attest_cid, ct, sizeof ct) == 0) {
                const char *tag = "attest: ";
                for (m = 0; tag[m]; m++) out[n++] = (uint8_t) tag[m];
                for (m = 0; ct[m] && n < cap; m++) out[n++] = (uint8_t) ct[m];
                out[n++] = (uint8_t) '\n';
            }
        }
        return (int32_t) n;
    }
    /* Two ingress shapes: the bare 168-byte manifest envelope (the door
     * channel is authenticated), or the full attestation datagram — envelope
     * + ML-DSA-87 pk + sig — verified against its own embedded key before
     * the manifest is even unpacked for admission. */
    if (in_len == SMIC_ATTEST_BYTES) {
        if (smic_attest_verify(in, in_len, &mtype, &hw) != SMIC_OK) return -1;
    } else {
        if (in_len != SMIC_ENV_BYTES) return -1;
        if (smic_manifest_unpack(in, &mtype, &hw) != SMIC_OK) return -1;
    }
    now = vna_now(h);
    a = smic_admit(&g_smic, &hw, now);
    if (a == LPX_ACT_ESCALATE || a == LPX_ACT_ROLLBACK) h->smic_quar++;
    h->smic_in++;
    (void) cisex_observe(&g_cisex, hw.hw_digest, hw.region, LPRES_STATE_TRUE);
    {
        vna_id_t k;
        uint8_t env[SMIC_ENV_BYTES];
        if (smic_manifest_pack(mtype, &hw, env) == SMIC_OK) {
            vna_htag("zxv/smic/manifest/v1", hw.node_id, 32, k.b);
            vna_env_publish(h, &k, env, now, VNAH_ENV_TTL);
        }
    }
    if (cap < 1) return 0;
    out[0] = (uint8_t) a;
    return 1;
}

static uint32_t door_src_len(const vna_host_t *h)
{
    uint32_t n = 0;
    while (h->door_src && h->door_src[n] && n < 255) n++;
    return n;
}

static int is_ipfs_path(const char *path, uint32_t plen, ipfsn_cid_t *cid_out)
{
    uint32_t s = 6, e;
    if (plen <= 6 || path[0] != '/' || path[1] != 'i' || path[2] != 'p' ||
        path[3] != 'f' || path[4] != 's' || path[5] != '/')
        return 0;
    e = s;
    while (e < plen && path[e] != '?' && path[e] != '/' && path[e] != ' ') e++;
    if (e <= s) return 0;
    if (ipfsn_cid_parse(path + s, e - s, cid_out) != IPFSN_OK) return 0;
    return 1;
}

static int door_ipfs_load(const ipfsn_cid_t *cid)
{
    if (g_door_ipfs_valid && ipfsn_cid_equal(&g_door_ipfs_cid, cid)) return 1;
    if (ipfsn_bs_get(&g_aipi_bs, cid, g_door_ipfs_buf, sizeof g_door_ipfs_buf, &g_door_ipfs_len) !=
        IPFSN_OK)
        return 0;
    g_door_ipfs_cid = *cid;
    g_door_ipfs_valid = 1;
    return 1;
}

static int32_t door_size(void *ctx, const char *path, uint32_t plen)
{
    const zxv_door_file_t *f;
    ipfsn_cid_t cid;
    if (is_boot_src(path, plen)) {
        uint32_t n = door_src_len((const vna_host_t *) ctx);
        return n ? (int32_t) n : -1;
    }
    if (is_curriculum_cid(path, plen)) return (int32_t) (sizeof g_curriculum_cid - 1);
    if (is_ipfs_path(path, plen, &cid) && door_ipfs_load(&cid)) return (int32_t) g_door_ipfs_len;
    return door_path(path, plen, &f) ? (int32_t) f->len : -1;
}

static const char *door_type(void *ctx, const char *path, uint32_t plen)
{
    const zxv_door_file_t *f;
    ipfsn_cid_t cid;
    (void) ctx;
    if (is_boot_src(path, plen) || is_curriculum_cid(path, plen))
        return "text/plain; charset=utf-8";
    if (is_ipfs_path(path, plen, &cid)) return "application/vnd.ipld.raw";
    return door_path(path, plen, &f) ? f->type : 0;
}

static int32_t door_body(void *ctx, const char *path, uint32_t plen, uint64_t off, uint8_t *out,
                         uint32_t cap)
{
    const zxv_door_file_t *f;
    vna_host_t *h = (vna_host_t *) ctx;
    ipfsn_cid_t cid;
    uint32_t n;
    if (is_ipfs_path(path, plen, &cid) && door_ipfs_load(&cid)) {
        if (off == 0) {
            g_ipfs_downloads++;
            (void) cisex_want(&g_cisex, cid.digest, g_cisex_region);
        }
        if (off >= g_door_ipfs_len) return 0;
        n = g_door_ipfs_len - (uint32_t) off;
        if (n > cap) n = cap;
        vna_copy(out, g_door_ipfs_buf + off, n);
        g_ipfs_bytes_served += n;
        return (int32_t) n;
    }
    if (is_boot_src(path, plen)) {
        uint32_t len = door_src_len(h);
        if (off >= len) return 0;
        n = len - (uint32_t) off;
        if (n > cap) n = cap;
        vna_copy(out, (const uint8_t *) h->door_src + off, n);
        return (int32_t) n;
    }
    if (is_curriculum_cid(path, plen)) {
        uint32_t len = (uint32_t) (sizeof g_curriculum_cid - 1);
        if (off >= len) return 0;
        n = len - (uint32_t) off;
        if (n > cap) n = cap;
        vna_copy(out, (const uint8_t *) g_curriculum_cid + off, n);
        return (int32_t) n;
    }
    if (!door_path(path, plen, &f) || off >= f->len) return off >= f->len ? 0 : -1;
    n = f->len - (uint32_t) off;
    if (n > cap) n = cap;
    vna_copy(out, f->bytes + off, n);
    return (int32_t) n;
}

/* A delta the door accepted (a browser's POST): post it to the bus. */
static void door_gossip(void *ctx, const uint8_t *bytes, uint32_t n)
{
    vna_host_t *h = (vna_host_t *) ctx;
    (void) vna_space_delta(&h->spaces, h->bus_cid, bytes, n, h->door.now);
}

/* A bus event applied: a peer's delta reaches the door's page and its
 * streams. Our own echo is skipped — the door logged it when it posted. */
static void door_ev(void *ctx, const vna_space_t *s, const vna_space_logent_t *e)
{
    vna_host_t *h = (vna_host_t *) ctx;
    if (!s || !e || e->kind != VNA_SPACE_EV_DELTA) return;
    if (!vna_ct_eq(s->cid, h->bus_cid, VNA_CID_RAW_LEN)) return;
    if (vna_id_eq(&e->author, &h->idn.id)) return;
    (void) vna_door_delta_hear(&h->door, e->data, e->len, e->lamport);
}

static void put_hex_bytes(const uint8_t *b, uint32_t n)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        uart_putc(hex[b[i] >> 4]);
        uart_putc(hex[b[i] & 15]);
    }
}

static void put_u16(uint16_t v)
{
    uart_put_dec(v);
}

/* The stack's raw-UDP hook: a datagram to the Vinea port is the node's next
 * input. Runs inside sn_pump, the same context as vna_host_pump, so the node
 * is single-threaded like every host the protocol was written for. */
static void vna_rx(void *ctx, const uint8_t src_ip[NET_IP_LEN], uint16_t src_port,
                   const uint8_t *data, uint32_t len)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint8_t a[6];
    uint32_t i;
    for (i = 0; i < 4 && i < NET_IP_LEN; i++) a[i] = src_ip[i];
    a[4] = (uint8_t) (src_port >> 8);
    a[5] = (uint8_t) src_port;
    {
        uint32_t matches = 0, midx = 0;
        for (i = 0; i < h->nseed_addr; i++) {
            if (h->seed_addr[i][0] == a[0] && h->seed_addr[i][1] == a[1] &&
                h->seed_addr[i][2] == a[2] && h->seed_addr[i][3] == a[3]) {
                matches++;
                midx = i;
            }
        }
        if (matches == 1u) {
            a[4] = h->seed_addr[midx][4];
            a[5] = h->seed_addr[midx][5];
        }
    }
    h->recv++;
    if (len >= 4) {
        uint32_t magic = (uint32_t) data[0] | ((uint32_t) data[1] << 8) |
                         ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
        if (magic == VNA_HS1_MAGIC || magic == VNA_HS2_MAGIC || magic == VNA_HS3_MAGIC ||
            (magic != VNA_MSG_MAGIC && sess_slot_addr(a) >= 0)) {
            vna_sess_rx(h, a, data, len, vna_now(h));
            return;
        }
        if (magic != VNA_MSG_MAGIC &&
            !(data[0] == 'Z' && data[1] == 'X' && data[2] == 'V')) {
            h->refused++;
            g_bunker_bounces++;
            g_casimir_cavity.faults_bounced++;
            return;
        }
    } else {
        h->refused++;
        g_bunker_bounces++;
        g_casimir_cavity.faults_bounced++;
        return;
    }
    {
        word168_t pid;
        vna_zero(&pid, sizeof pid);
        for (i = 0; i < 6u; i++) pid.bytes[i] = a[i];
        if (!porter_house_admit(&g_porter, VNAH_PORT, &pid, 500u)) {
            h->refused++;
            g_bunker_bounces++;
            g_casimir_cavity.faults_bounced++;
            return;
        }
    }
    if (vna_node_handle(&h->node, data, len, a, 6, vna_now(h), &h->ob) != VNA_OK) {
        h->refused++;
        g_bunker_bounces++;
        g_casimir_cavity.faults_bounced++;
    }
}

/* Send one datagram to a six-byte Vinea address. 0 = on the wire; -1 = the
 * next hop is not in the ARP cache yet (the request went out; the caller may
 * keep the datagram in the re-send ring). */
static int vna_sendto(vna_host_t *h, const uint8_t addr[6], const uint8_t *data, uint32_t len)
{
    uint16_t port = (uint16_t) ((addr[4] << 8) | addr[5]);
    net_connect(&h->l->net, h->sock, addr, port);
    if (net_send(&h->l->net, h->sock, data, len) < 0) return -1;
    h->sent++;
    return 0;
}

/* The fleet's commons test: a contact is a member when its transport
 * address is one of the seed peers this node was started with. Every other
 * verified peer trades as a counterparty but draws nothing from the
 * commons the owner pays for. */
static int vna_is_seed(const vna_host_t *h, const uint8_t addr[6])
{
    uint32_t i, j;
    for (i = 0; i < h->nseed_addr; i++) {
        for (j = 0; j < 4; j++)
            if (h->seed_addr[i][j] != addr[j]) break;
        if (j == 4) return 1;
    }
    return 0;
}

static int vna_contact_addr(vna_host_t *h, const vna_id_t *id, uint8_t addr[6])
{
    const vna_contact_t *c = vna_rt_find(&h->node.rt, id);
    uint32_t i, j, matches = 0, match_idx = 0;
    if (!c || c->addr_len != 6) return 0;
    for (j = 0; j < 6; j++) addr[j] = c->addr[j];
    for (i = 0; i < h->nseed_addr; i++) {
        if (h->seed_addr[i][0] == addr[0] && h->seed_addr[i][1] == addr[1] &&
            h->seed_addr[i][2] == addr[2] && h->seed_addr[i][3] == addr[3]) {
            matches++;
            match_idx = i;
        }
    }
    if (matches == 1u) {
        addr[4] = h->seed_addr[match_idx][4];
        addr[5] = h->seed_addr[match_idx][5];
    }
    return 1;
}

/* Register `id` in the book from the key the node verified (keycache), if
 * the book does not know it yet. Members are the fleet: seed addresses
 * only — the commons is the owner's, outsiders still trade. */
static int vna_book_learn(vna_host_t *h, const vna_id_t *id)
{
    const vna_keycache_t *kc = &h->node.kc;
    const vna_keycache_ent_t *e = 0;
    uint8_t addr[6];
    uint32_t i;
    if (vna_book_find(&h->book, id)) return 1;
    for (i = 0; i < kc->cap && !e; i++)
        if (kc->e[i].used && vna_id_eq(&kc->e[i].id, id)) e = &kc->e[i];
    if (!e) return 0;
    return vna_book_add_peer(&h->book, id, e->pk, e->pow_nonce, e->pow_bits,
                             vna_contact_addr(h, id, addr) && vna_is_seed(h, addr)) == VNA_OK;
}

/* A receipt from a peer: the node verified the message (signature, PoW,
 * replay); the link checks it against the trade protocol. */
static vna_status_t vna_on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *r, uint32_t len,
                                uint64_t now)
{
    vna_host_t *h = (vna_host_t *) ctx;
    if (!vna_book_learn(h, from)) {
        h->refused_econ++;
        return VNA_ERR_DENIED;
    }
    return vna_link_on_rcpt(&h->link, from, r, len, now);
}

/* Receipts the trade loop queued go out through the node (signed, sequenced
 * messages); the caller's vna_flush puts them on the wire. */
static void vna_drain_link(vna_host_t *h, uint64_t now)
{
    vna_id_t dst;
    const uint8_t *b;
    uint32_t len;
    while (vna_link_next_out(&h->link, &dst, &b, &len))
        (void) vna_node_send_rcpt(&h->node, &dst, b, len, now, &h->ob);
}

/* Reap finished DHT lookups so the node's 8 lookup slots never fill up, then
 * announce our own primary epistemic shard + curriculum root and query the
 * next Fibonacci-adjacent complementary shard ((slot + {1,2,3,5}) & 7) across
 * the UBH-168 post-quantum mesh. */
static void vna_reap_lookups(vna_host_t *h, uint64_t now)
{
    for (int32_t i = 0; i < (int32_t) VNA_NODE_LOOKUPS; i++) {
        if (h->node.lks[i].used && vna_node_lookup_done(&h->node, i)) {
            const vna_node_lookup_t *lk = vna_node_lookup_get(&h->node, i);
            if (lk && lk->value_len) vna_env_ingest_rec(h, lk->value, lk->value_len, now);
            vna_node_lookup_release(&h->node, i);
        }
    }
}

static void vna_epistemic_sync(vna_host_t *h, uint64_t now)
{
    static const uint8_t fib_step[4] = {1u, 2u, 3u, 5u};
    vna_provider_t pv;
    vna_id_t cur_key, comp_key;
    uint8_t comp_slot;
    uint32_t i;

    vna_reap_lookups(h, now);
    vna_zero(&pv, sizeof pv);
    pv.chunk_size = VNA_CHUNK_SIZE;
    pv.n_chunks = 1u;
    pv.visibility = VNA_VIS_PUBLIC;
    for (i = 0; i < 4; i++) pv.addr[i] = h->l->eth->ip[i];
    pv.addr[4] = (uint8_t) (VNAH_PORT >> 8);
    pv.addr[5] = (uint8_t) VNAH_PORT;
    pv.addr_len = 6;

    /* the shard file this node announces: its real Merkle root is the DHT
     * key, so any node that carves the same CAR derives the same roots */
    pv.file_size = g_shard_len[h->shard_slot];
    if (vna_node_announce(&h->node, &g_sf_root[h->shard_slot], &pv, now, VNAH_SHARD_TTL, &h->ob) >=
        0)
        h->shard_announces++;
    if (g_attest_ok) {
        pv.file_size = SMIC_ATTEST_BYTES;
        if (vna_node_announce(&h->node, &g_sf_root[VNAH_SF_ATTEST], &pv, now, VNAH_SHARD_TTL,
                              &h->ob) >= 0)
            h->shard_announces++;
    }
    /* files this node fetched and now re-serves */
    for (i = 0; i < VNAH_EXT_N; i++) {
        if (!g_ext_used[i]) continue;
        pv.file_size = g_ext_sf[i].size;
        pv.n_chunks = g_ext_sf[i].n;
        if (vna_node_announce(&h->node, &g_ext_sf[i].root, &pv, now, VNAH_SHARD_TTL, &h->ob) >= 0)
            h->shard_announces++;
    }
    pv.n_chunks = 1u;
    /* the curriculum beacon stays: the CID names the whole archive the
     * shards are carved from */
    pv.file_size = CUR_CAR_LEN;
    pv.n_chunks = VNAH_SLICE_N;
    vna_htag("zxv/epistemic/curriculum/v1", (const uint8_t *) g_curriculum_cid,
             (uint32_t) (sizeof g_curriculum_cid - 2), cur_key.b);
    if (vna_node_announce(&h->node, &cur_key, &pv, now, VNAH_SHARD_TTL, &h->ob) >= 0)
        h->shard_announces++;

    /* the complementary shard's provider: its file root is computable, so
     * the lookup goes straight at it */
    comp_slot = (uint8_t) ((h->shard_slot + fib_step[h->shard_cursor & 3u]) & (VNAH_SHARD_N - 1u));
    h->shard_cursor++;
    comp_key = g_sf_root[comp_slot];
    if (vna_node_lookup(&h->node, &comp_key, VNA_LK_FIND_VALUE, now, &h->ob) >= 0)
        h->shard_queries++;
}

/* One economic cycle. The 8-node fleet trades INFORMATION (SWARM_CAP_INTELLECTUAL)
 * rather than money: wants top up what the node requests from complementary
 * shards (records and IPFS/UBH-168 file chunks); sells offer each peer the
 * verified epistemic service the usage table measured it drawing, on credit in
 * SWARM_CAP_INTELLECTUAL. The owner gate converts the cycle's budget into the
 * intellectual pool and settlement distributes it to members whose contribution
 * was countersigned. */
static void vna_settle(vna_host_t *h, uint64_t now)
{
    uint64_t got;
    uint32_t i, left = 0;

    /* Keep open wants for the information service this node consumes (DHT
     * epistemic records and UBH-168 file chunks from complementary shards). */
    for (i = 0; i < VNA_LINK_WANTS; i++)
        if (h->link.want[i].used && (h->link.want[i].resource == VNA_RES_RECORD ||
                                     h->link.want[i].resource == VNA_RES_FILE))
            left += (uint32_t) h->link.want[i].units_left;
    if (!left) {
        (void) vna_link_want(&h->link, VNA_RES_RECORD, SWARM_CAP_INTELLECTUAL, VNAH_WANT_UNITS,
                             VNAH_WANT_UNITS * VNAH_UNIT_PRICE);
        (void) vna_link_want(&h->link, VNA_RES_FILE, SWARM_CAP_INTELLECTUAL, VNAH_WANT_UNITS,
                             VNAH_WANT_UNITS * VNAH_UNIT_PRICE);
    }

    /* Sell what the agreement actually served this cycle, per peer, in
     * SWARM_CAP_INTELLECTUAL (verified information exchange). */
    for (i = 0; i < VNAH_USAGE_CAP; i++) {
        vna_usage_ent_t *e = &h->ue[i];
        uint64_t rec, ch;
        if (!e->used) {
            h->served_rec[i] = h->served_file[i] = 0;
            continue;
        }
        rec = e->records;
        ch = e->chunks;
        if ((rec > h->served_rec[i] || ch > h->served_file[i]) && vna_book_learn(h, &e->peer)) {
            if (rec > h->served_rec[i]) {
                uint64_t units = rec - h->served_rec[i];
                if (vna_link_sell(&h->link, &e->peer, VNA_TR_CREDIT, SWARM_CAP_INTELLECTUAL,
                                  VNA_RES_RECORD, units, units * VNAH_UNIT_PRICE,
                                  "serve(record, form: intellectual)", now) == VNA_OK)
                    h->sold++;
            }
            if (ch > h->served_file[i]) {
                uint64_t units = ch - h->served_file[i];
                if (vna_link_sell(&h->link, &e->peer, VNA_TR_CREDIT, SWARM_CAP_INTELLECTUAL,
                                  VNA_RES_FILE, units, units * VNAH_UNIT_PRICE,
                                  "serve(file, form: intellectual)", now) == VNA_OK)
                    h->sold++;
            }
        }
        h->served_rec[i] = rec;
        h->served_file[i] = ch;
    }

    /* The cycle's emission: the budget slot's allotment becomes the
     * intellectual pool the settle splits over verified contributors. */
    (void) swarm_budget_begin_cycle(&h->sb);
    (void) vna_gate_export(&h->gate, h->owner_secret, &h->sb, VNAH_MODEL_ID, &h->book,
                           SWARM_CAP_INTELLECTUAL, VNAH_EXPORT_MAX, &got);
    vna_usage_new_cycle(&h->us);
    (void) vna_usage_expire(&h->us, now);
    {
        uint64_t imported = 0;
        if (vna_link_cycle(&h->link, &h->gate, h->owner_secret, &h->sb, VNAH_BASE_RATE, now,
                           &imported) != VNA_OK)
            h->refused_econ++;
        h->imported_total += imported;
    }
    vna_epistemic_sync(h, now);
    vna_drain_link(h, now);
    h->earn_cycles++;
}

/* Every outbox entry to the wire; entries that cannot be sent keep their
 * bytes in the re-send ring (or are dropped and counted when it is full). */
static void vna_flush(vna_host_t *h)
{
    uint32_t i;
    for (i = 0; i < h->ob.n; i++) {
        vna_out_ent_t *e = &h->ob.e[i];
        if (e->addr_len != 6 || e->len > VNAH_PEND_MAX) {
            h->dropped_ob++;
            continue;
        }
        if (vna_sendto(h, e->addr, h->ob.buf + e->off, e->len) == 0) continue;
        if (h->npend < VNAH_PEND_CAP) {
            vnah_pend_t *p = &h->pend[h->npend++];
            uint32_t j;
            for (j = 0; j < 6; j++) p->addr[j] = e->addr[j];
            for (j = 0; j < e->len; j++) p->data[j] = h->ob.buf[e->off + j];
            p->len = (uint16_t) e->len;
        } else {
            h->dropped_tx++;
        }
    }
    vna_outbox_clear(&h->ob);
}

int vna_host_start(vna_host_t *h, sn_link_t *l, const uint8_t peers[][6], uint32_t npeer,
                   const char *door_src, uint32_t (*entropy)(void), const uint8_t persist_seed[32])
{
    uint8_t iseed[32], dseed[32], a[6];
    sha256_ctx_t c;
    vna_node_cfg_t cfg;
    vna_node_mem_t mem;
    uint64_t now;
    uint32_t i, j;

    if (!h || !l || !l->eth || !l->nic) return 0;
    h->l = l;
    h->sock = -1;
    h->npend = 0;
    h->last_tick = 0;
    h->entropy = entropy;
    h->seeds = h->sent = h->recv = h->refused = h->dropped_tx = h->dropped_ob = 0;
    h->seed_addr = peers;
    h->nseed_addr = npeer;
    h->door_src = door_src;
    for (i = 0; i < VNAH_USAGE_CAP; i++) h->served_rec[i] = h->served_file[i] = 0;
    h->earned_posted = h->imported_total = 0;
    h->last_cycle = 0;
    h->sold = h->earn_cycles = h->refused_econ = 0;
    h->shard_slot = 0;
    h->shard_cursor = 0;
    h->shard_announces = h->shard_queries = 0;
    h->up = 0;

    /* One NodeID per machine: the seed mixes the MAC (stable across
     * reboots) with a PERSISTED secret generated from hardware entropy on
     * first boot — a MAC is an identifier, not a secret; without the
     * persisted secret anyone who knows the MAC could reproduce the
     * signing key (the October 2026 audit, finding 1). The persisted
     * secret lives on the ZXV_DATA partition (identity.seed), generated
     * by kernel_main when the partition mounts. No entropy and no
     * persisted secret falls back to the MAC alone with a loud warning:
     * that context is the air-gapped bring-up only. */
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/vinea/node/v1", 17);
    sha256_update(&c, l->nic->mac, NET_MAC_LEN);
    if (persist_seed) sha256_update(&c, persist_seed, 32);
    sha256_final(&c, iseed);
    if (!entropy && !persist_seed)
        uart_puts("  [VNA] WARNING: no entropy and no persisted identity secret; "
                  "the signing key derives from the MAC alone (bring-up only)\n");
    if (vna_identity_create(&h->idn, iseed, 0, 1) != VNA_OK) {
        uart_puts("  [VNA] identity creation failed\n");
        return 0;
    }
    if (entropy) {
        for (i = 0; i < 8; i++) {
            uint32_t r = entropy();
            for (j = 0; j < 4; j++) dseed[i * 4 + j] = (uint8_t) (r >> (j * 8));
        }
    } else {
        /* no hardware source: the node still runs, its sequence numbers are
         * the wall clock's and its DRBG key is unique-to-the-machine */
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) "zxv/vinea/drbg/v1", 17);
        sha256_update(&c, iseed, sizeof iseed);
        sha256_final(&c, dseed);
    }

    vna_node_cfg_default(&cfg);
    cfg.ubh = true; /* enforce UBH-168 21-octet framing across all ZXV peers */
    cfg.ts_window_ms = 0x3FFFFFFFFFFFFFFFull; /* phase-tick adapter */
    h->shard_slot = (uint8_t) (h->idn.id.b[0] & (VNAH_SHARD_N - 1u));
    h->peer_index = 0xFF;
    if (peers) {
        for (i = 0; i < npeer; i++) {
            if (peers[i][0] == l->eth->ip[0] && peers[i][1] == l->eth->ip[1] &&
                peers[i][2] == l->eth->ip[2] && peers[i][3] == l->eth->ip[3]) {
                h->shard_slot = (uint8_t) (i & (VNAH_SHARD_N - 1u));
                h->peer_index = (uint8_t) i;
                break;
            }
        }
    }
    mem.pool = h->pool;
    mem.pool_cap = VNAH_POOL_CAP;
    mem.kc = h->kc;
    mem.kc_cap = VNAH_KC_CAP;
    mem.rp = h->rp;
    mem.rp_cap = VNAH_RP_CAP;
    mem.dd = h->dd;
    mem.dd_cap = VNAH_DD_CAP;
    mem.store = h->store;
    mem.store_cap = VNAH_STORE_CAP;
    if (vna_node_init(&h->node, &h->idn, &cfg, &mem, dseed) != VNA_OK) {
        uart_puts("  [VNA] node init failed\n");
        return 0;
    }
    now = vna_now(h);
    h->last_cycle = now; /* the first settle runs one full cycle in */
    /* the wall clock as a floor: no message seq a peer may still hold is
     * ever reused after a restart */
    vna_node_resume_seq(&h->node, now << 10);

    /* The economy. The gate and the trade loop's signature randomness are
     * keyed like the identity: deterministically from this machine's seed,
     * so nothing depends on a random source the CPU may not have. The
     * agreement is the seed role itself: serve the network (records, file
     * chunks, routing) to anyone, bounded by the per-peer quotas. */
    vna_book_init(&h->book, &h->idn, h->acct, VNAH_BOOK_CAP);
    chg_init(&g_chg, CHG_CAP_INFER | CHG_CAP_ADVISE);
    vna_chg_model();
    vna_chg_warm_init(h); /* the node's own chiglet: genome, hobbies, bond, xp */
    oseq_registry_init(&g_oseq);
    g_oseq_vna = oseq_register_node(&g_oseq, "vna", 1);
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/vinea/gate/v1", 17);
    sha256_update(&c, iseed, sizeof iseed);
    sha256_final(&c, h->owner_secret);
    vna_gate_init(&h->gate, h->owner_secret, VNAH_EXPORT_MAX, VNAH_IMPORT_CAP, 4);
    if (vna_gate_begin_cycle(&h->gate, h->owner_secret) != VNA_OK) {
        uart_puts("  [VNA] gate failed\n");
        return 0;
    }
    vna_usage_init(&h->us, h->ue, VNAH_USAGE_CAP, h->le, VNAH_LEASE_CAP);
    vna_agree_default(&h->agr, &h->idn.id);
    h->agr.degree = VNA_DEG_SERVE;
    h->agr.audience = VNA_AUD_EVERYONE;
    h->agr.quota_period_ms = VNAH_QUOTA_MS;
    h->agr.q_records = VNAH_Q_RECORDS;
    h->agr.q_chunks = VNAH_Q_CHUNKS;
    {
        uint8_t lseed[32];
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) "zxv/vinea/link/v1", 17);
        sha256_update(&c, iseed, sizeof iseed);
        sha256_final(&c, lseed);
        vna_link_init(&h->link, &h->book, &h->agr, &h->us, lseed, VNAH_RETRY_MS, VNAH_TRIES);
    }
    vna_node_set_agreement(&h->node, &h->agr, &h->us, 0, 0);
    vna_node_set_rcpt_handler(&h->node, vna_on_rcpt, h);
    (void) swarm_budget_init(&h->sb, 1, VNAH_BASE_RATE);
    (void) swarm_budget_register(&h->sb, VNAH_MODEL_ID, 0);
    (void) vna_link_want(&h->link, VNA_RES_RECORD, SWARM_CAP_INTELLECTUAL, VNAH_WANT_UNITS,
                         VNAH_WANT_UNITS * VNAH_UNIT_PRICE);
    (void) vna_link_want(&h->link, VNA_RES_FILE, SWARM_CAP_INTELLECTUAL, VNAH_WANT_UNITS,
                         VNAH_WANT_UNITS * VNAH_UNIT_PRICE);

    h->sock = net_socket(&l->net, SOCK_UDP);
    if (h->sock < 0 || net_bind(&l->net, h->sock, VNAH_PORT) != 0) {
        uart_puts("  [VNA] UDP socket/bind failed\n");
        return 0;
    }
    vna_outbox_init(&h->ob, h->obuf, VNAH_OBUF_CAP);
    net_udp_hook(&l->net, VNAH_PORT, vna_rx, h);
    /* the timesvc channel: a second UDP socket so SNTP replies never reach
     * the VNA hook */
    g_sntp_sock = net_socket(&l->net, SOCK_UDP);
    if (g_sntp_sock >= 0 && net_bind(&l->net, g_sntp_sock, VNAH_SNTP_LPORT) == 0)
        net_udp_hook(&l->net, VNAH_SNTP_LPORT, sntp_rx, h);

    /* The bus and the door. Every seed opens the same canonical space —
     * create_at with the bus's own constants lands all five on one CID —
     * and listens on plain http for the doorway, tree files and the L2
     * stream (vna_door.h: the browser bypass — no UDP, no WebSocket, no
     * TLS). Four listeners because the stack accepts one peer each. */
    {
        uint8_t sseed[32], bnonce[16], bn32[32];
        vna_id_t busid;
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) "zxv/vinea/spaces/v1", 19);
        sha256_update(&c, iseed, sizeof iseed);
        sha256_final(&c, sseed);
        vna_spaces_init(&h->spaces, &h->node, &h->ob, sseed);
        vna_spaces_on_event(&h->spaces, door_ev, h);
        vna_htag("zxv/l2/bus/v1", (const uint8_t *) "", 0, busid.b);
        vna_htag("zxv/l2/bus/nonce/v1", (const uint8_t *) "", 0, bn32);
        vna_copy(bnonce, bn32, sizeof bnonce);
        if (vna_space_create_at(&h->spaces, VNA_SPACE_ROOM, VNA_GOV_QUORUM, 51, &busid, "zxv-l2",
                                "zxv-l2", 0, bnonce, now, h->bus_cid) != VNA_OK)
            uart_puts("  [VNA] bus space failed\n");
        vna_door_init(&h->door, h, door_size, door_type, door_body, door_gossip);
        cisex_init(&g_cisex, g_cisex_ents, VNAH_CISEX_N);
        g_cisex_region = h->shard_slot;
        h->door.query = door_cisex;
        /* The ring programs: the machine-integrity ring admits this node's
         * own manifest first, and the exchange order book opens bound to
         * the trade book and the curriculum root. Both propagate as
         * VNA_REC_ENV records. */
        g_aipi_ms.buf = g_aipi_store;
        g_aipi_ms.cap = sizeof g_aipi_store;
        ipfsn_memstore_ops(&g_aipi_ms, &g_aipi_sops);
        (void) ipfsn_bs_init(&g_aipi_bs, &g_aipi_sops, g_aipi_tab, VNAH_AIPI_BS_N, NULL, 0);
        (void) ipfsn_node_init(&g_ipfs, &g_aipi_bs, g_aipi_pins, VNAH_AIPI_PINS, g_aipi_net,
                               sizeof g_aipi_net);
        smic_init(&g_smic, g_smic_ents, VNAH_SMIC_N);
        vna_shard_setup(); /* the CAR becomes eight Merkle-rooted shard files */
        g_car_indexed = cisex_car_harvest(g_cisex_region); /* curriculum -> blockstore + index */
        vna_smic_self(h);
        (void) smic_admit(&g_smic, &g_self_hw, now);
        {
            ipfsn_cid_t cc;
            if (ipfsn_cid_parse(g_curriculum_cid, (uint32_t) (sizeof g_curriculum_cid - 2), &cc) ==
                IPFSN_OK)
                vna_copy(g_cur_digest, cc.digest, 32);
            vna_exch_init(&g_exch, &h->book, g_cur_digest, h->shard_slot);
        }
        h->door.query2 = door_smic;
        h->door.qctx2 = h;
        h->door.query3 = door_card;
        h->door.qctx3 = h;
        h->door.query4 = door_spaces;
        h->door.qctx4 = h;
        h->door.query5 = door_bank;
        h->door.qctx5 = h;
        h->door.query6 = door_telemetry;
        h->door.qctx6 = h;
        h->door.query7 = door_pk;
        h->door.qctx7 = h;
        h->door.query8 = door_sig;
        h->door.qctx8 = h;
        G_sbank_n = 0;
        sb_catalog_init(&G_catalog, G_sbanks, G_sbank_n);
#if defined(ZXV_HAVE_SEEDBANK_BLOB)
        {
            int seeds = sb_seedbank_blob_register();
            if (seeds > 0)
                uart_puts("[BANK_OK] compiled-in seed bank: ");
            else
                uart_puts("[BANK_FAIL] seed bank blob refused\n");
            if (seeds > 0) {
                uart_put_dec((uint32_t) seeds);
                uart_puts(" seeds, ");
                uart_put_dec(sb_seedbank_blob_bytes());
                uart_puts(" bytes\n");
            }
        }
#endif
        /* self-supervision: one domain per running ring program */
        {
            char nid[9];
            uint32_t k;
            for (k = 0; k < 8; k++) {
                uint8_t n = h->idn.id.b[k] & 15;
                nid[k] = (char) (n < 10 ? '0' + n : 'a' + n - 10);
            }
            nid[8] = 0;
            ev_seq_init(&g_seq, nid);
            ev_audit_init(&g_audit);
            ev_healing_init(&g_heal);
            g_dom_vna = ev_seq_create_domain(&g_seq, "vnea", 64, 4096, EV_CONSISTENCY_CAUSAL);
            g_dom_door = ev_seq_create_domain(&g_seq, "door", 64, 2048, EV_CONSISTENCY_LOCAL);
            g_dom_aipi = ev_seq_create_domain(&g_seq, "aipi", 64, 2048, EV_CONSISTENCY_LOCAL);
            g_dom_smic = ev_seq_create_domain(&g_seq, "smic", 64, 2048, EV_CONSISTENCY_CAUSAL);
            g_dom_exch = ev_seq_create_domain(&g_seq, "cisex", 64, 2048, EV_CONSISTENCY_LOCAL);
            scp144_cavity_init(&g_casimir_cavity, 144u, h->idn.id.b, g_cur_digest);
            porter_house_init(&g_porter, 1u, "zxv-porter-bunker");
            (void) porter_house_seal_port(&g_porter, VNAH_PORT, PH_SEAL_OPEN, 0u);
            (void) porter_house_seal_port(&g_porter, VNAH_DOOR_PORT, PH_SEAL_OPEN, 0u);
            (void) porter_house_seal_port(&g_porter, VNAH_SNTP_LPORT, PH_SEAL_OPEN, 0u);
            immig_init(&g_immig, 1u, &g_porter);
            g_immig.verify_sig = immig_kernel_sig_ok;
            {
                static const char * const dnames[6] = {
                    "vnea-scp144", "cisex-index", "smic-ring",
                    "chiglet-ai", "aipi-broker", "ipfs-supernode"
                };
                uint32_t di;
                for (di = 0; di < 6u; di++) {
                    word168_t did;
                    uint8_t chash[IMMIG_CONTENT_HASH_LEN];
                    uint8_t sig[IMMIG_SIG_LEN];
                    vna_zero(&did, sizeof did);
                    vna_zero(chash, sizeof chash);
                    vna_zero(sig, sizeof sig);
                    did.bytes[0] = (uint8_t) (di + 1u);
                    (void) immig_apply_visa(&g_immig, dnames[di], IMMIG_VISA_RESIDENT, &did,
                                            h->idn.pk, chash, sig,
                                            (1u << 0) | (1u << 1), now);
                }
            }
        }
        if (aipi_init(&g_aipi, &g_ipfs, g_aipi_jobs, VNAH_AIPI_JOBS, g_aipi_memo, VNAH_AIPI_MEMO,
                      g_aipi_mod, g_aipi_con) == AIPI_OK) {
            aipi_vna_econ_t ec = {h, ah_reserve, ah_pay, ah_refund, ah_paid};
            /* the integer engine is the RUN/CHECKER executor: module = GGUF */
            g_aipi.exec.run = zt_run;
            g_aipi.exec.check = zt_check;
            g_aipi.exec.certify = zt_certify;
            if (aipi_vna_init(&g_av, &g_aipi, &h->node, &h->ob, &ec) == AIPI_OK) {
                aipi_vna_serve(&g_av, true, 1);
                g_aipi_on = true;
            }
            /* the node's own attestation: manifest + ML-DSA-87 pk + sig,
             * stored as a public block so any peer can pull the signed
             * datagram by CID and verify it offline */
            {
                uint32_t alen = 0;
                if (smic_attest(&g_self_hw, SMIC_T_MANIFEST, h->idn.pk, h->idn.sk, g_self_attest,
                                sizeof g_self_attest, &alen) == SMIC_OK &&
                    ipfsn_cid_sha256(IPFSN_MC_RAW, g_self_attest, alen, &g_attest_cid) ==
                        IPFSN_OK &&
                    pin_ingest_block(&g_attest_cid, g_self_attest, alen, g_cisex_region) == 0)
                    g_attest_ok = true;
            }
            if (g_attest_ok) vna_attest_share();
        }
        for (i = 0; i < VNA_DOOR_CONNS; i++) {
            h->door_slot[i] = -1;
            h->door_tx_len[i] = h->door_tx_off[i] = 0;
            h->door_sock[i] = net_socket(&l->net, SOCK_TCP);
            if (h->door_sock[i] >= 0 && (net_bind(&l->net, h->door_sock[i], VNAH_DOOR_PORT) != 0 ||
                                         net_listen(&l->net, h->door_sock[i], 1) != 0)) {
                net_close(&l->net, h->door_sock[i]);
                h->door_sock[i] = -1;
            }
        }
    }

    h->up = 1;
    uart_puts("  [VNA] node ");
    put_hex_bytes(h->idn.id.b, 8);
    uart_puts(" on ");
    for (i = 0; i < 4; i++) {
        uart_put_dec(l->eth->ip[i]);
        if (i < 3) uart_putc('.');
    }
    uart_putc(':');
    put_u16(VNAH_PORT);
    uart_puts(" shard ");
    uart_put_dec(h->shard_slot);
    uart_puts(" ubh168+mldsa87\n");

    if (peers) {
        for (i = 0; i < npeer; i++) {
            /* our own address is never a peer */
            for (j = 0; j < 4; j++) a[j] = peers[i][j];
            if (a[0] == l->eth->ip[0] && a[1] == l->eth->ip[1] && a[2] == l->eth->ip[2] &&
                a[3] == l->eth->ip[3])
                continue;
            for (j = 0; j < 6; j++) a[j] = peers[i][j];
            if (vna_node_bootstrap(&h->node, a, 6, now, &h->ob) == VNA_OK) h->seeds++;
        }
        vna_epistemic_sync(h, now);
        /* the ring programs announce themselves: our machine-integrity
         * manifest under its own key, our curriculum listing on the order
         * book — both VNA_REC_ENV records the fleet carries forward */
        {
            vna_id_t k;
            uint8_t env[SMIC_ENV_BYTES];
            if (smic_manifest_pack(SMIC_T_MANIFEST, &g_self_hw, env) == SMIC_OK) {
                vna_htag("zxv/smic/manifest/v1", g_self_hw.node_id, 32, k.b);
                vna_env_publish(h, &k, env, now, VNAH_ENV_TTL);
            }
            vna_exch_publish(h, now);
            vna_chg_dna_publish(h, now); /* our chiglet's genome, so visits can name it */
        }
        /* SNTP requests to the three time servers, each with its own nonce:
         * two agreeing replies commit the node to Unix ms via timesvc. The
         * exchange retries until it commits or the bound is spent — a
         * single dropped round must not leave the node on its boot clock,
         * whose timestamps no peer's freshness window would accept. */
        if (g_sntp_sock >= 0 && !g_sntp_done && g_sntp_tries < VNAH_SNTP_TRIES) {
            uint32_t si;
            if (g_sntp_tries == 0) timesvc_init(&g_ts, 0);
            g_sntp_tries++;
            for (si = 0; si < VNAH_SNTP_NSERV; si++) {
                uint8_t pkt[SNTP_PKT_LEN];
                vna_drbg_gen(&h->node.rng, (uint8_t *) &g_sntp_nonce[si], 8);
                g_sntp_t1_us[si] = (int64_t) now * 1000;
                if (sntp_build_request(pkt, g_sntp_nonce[si]) == SNTP_PKT_LEN &&
                    net_connect(&l->net, g_sntp_sock, g_sntp_serv[si], SNTP_PORT) == 0)
                    (void) net_send(&l->net, g_sntp_sock, pkt, SNTP_PKT_LEN);
            }
        }
        vna_flush(h);
    }
    return 1;
}

void vna_host_pump(vna_host_t *h, uint64_t now)
{
    uint32_t i;
    if (!h || !h->up) return;
    /* Phase-tick native: never jump internal pump time by g_wall_off_ms */
    sn_pump(h->l);
    (void) vna_http_mesh;
    (void) g_geo_routes;
    (void) vna_cell_pin_batch;
    if (now >= h->last_tick + VNAH_TICK_MS) {
        static uint32_t fib_tick, fib_next = 1u, fib_a = 1u, fib_b = 2u;
        h->last_tick = now;
        scp144_cavity_clock_tick(&g_casimir_cavity);
        fib_tick++;
        if (fib_tick >= fib_next) {
            uint32_t step = fib_b;
            fib_a = fib_b;
            fib_b = step >= 144u ? 144u : fib_a + step;
            if (fib_b > 144u) fib_b = 144u;
            fib_next = fib_tick + fib_a;
            if (h->seed_addr && h->nseed_addr > 0) {
                uint32_t pi = (fib_tick + (h->peer_index < 9u ? h->peer_index : 0u)) % h->nseed_addr;
                const uint8_t *sa = h->seed_addr[pi];
                if (!(sa[0] == h->l->eth->ip[0] && sa[1] == h->l->eth->ip[1] &&
                      sa[2] == h->l->eth->ip[2] && sa[3] == h->l->eth->ip[3])) {
                    (void) vna_node_bootstrap(&h->node, sa, 6, now, &h->ob);
                }
            }
            vna_sess_poll(h, now);
        }
        /* demand velocity: decay every entry, then let a hot root with
         * retained provider parameters fetch itself into staging */
        {
            uint32_t vi, hi = VNAH_CISEX_N;
            uint16_t hv = VNAH_VEL_FETCH;
            for (vi = 0; vi < g_cisex.count; vi++) {
                uint32_t p;
                if (g_vel[vi]) g_vel[vi] = (uint16_t) ((g_vel[vi] * VNAH_VEL_DECAY) >> 8);
                if (g_vel[vi] < hv || !g_cisex_ents[vi].used) continue;
                for (p = 0; p < VNAH_PROV_N; p++)
                    if (g_prov[p].used &&
                        vna_id_eq(&g_prov[p].root, (const vna_id_t *) g_cisex_ents[vi].digest))
                        break;
                if (p == VNAH_PROV_N) continue;
                if (vna_id_eq(&g_prov[p].root, &g_fetch_root) && g_fetch_active) continue;
                hi = vi;
                hv = g_vel[vi];
            }
            if (hi < VNAH_CISEX_N && !g_fetch_active) {
                uint32_t p;
                uint32_t j;
                int served = 0;
                for (p = 0; p < VNAH_PROV_N; p++)
                    if (g_prov[p].used &&
                        vna_id_eq(&g_prov[p].root, (const vna_id_t *) g_cisex_ents[hi].digest))
                        break;
                for (j = 0; j < VNAH_SF_N; j++)
                    if (vna_id_eq(&g_prov[p].root, &g_sf_root[j])) served = 1;
                for (j = 0; j < VNAH_EXT_N && !served; j++)
                    if (g_ext_used[j] && vna_id_eq(&g_prov[p].root, &g_ext_sf[j].root)) served = 1;
                if (!served && p < VNAH_PROV_N)
                    sess_fetch_begin(h, &g_prov[p].root, &g_prov[p].publisher, &g_prov[p].pv, now);
            }
        }
        vna_node_tick(&h->node, now, &h->ob);
        vna_link_tick(&h->link, now); /* L2: resend what is still waiting */
    }
    if (now >= h->last_cycle + VNAH_CYCLE_MS) {
        ev_heal_action_t wa;
        h->last_cycle = now;
        vna_settle(h, now);
        cisex_idx_sync(h, now); /* top-ranked entries out as CISX envelopes */
        /* the audit/heal cycle: replenish budgets, then check every domain's
         * invariants and let the healing engine act on the worst result */
        ev_seq_replenish(&g_seq);
        wa = ev_self_audit_heal_system(&g_seq, &g_audit, &g_heal);
        if ((uint32_t) wa > g_ev_worst) g_ev_worst = (uint32_t) wa;
    }
    vna_env_sweep(h, now); /* envelopes peers stored at this node */
    vna_chg_audit(h, now); /* the evidence judge reads every member */
    if (g_chg_alive) {
        /* the bond model's slow read: what this node's rhythm predicts
         * next, and a daily decay so old habits fade (B2) */
        static uint64_t decayed;
        chgb_suggestion_t sug;
        if (chgb_suggest(
                &g_bond,
                &(chgb_ctx_t){(uint16_t) ((now / 3600000ull) % 168u), BOND_APP_VNA, g_bond_prev},
                &sug) == CHGB_OK) {
            g_bond_sug = sug;
            g_bond_sug_ok = true;
        }
        if (now >= decayed + 24ull * 3600ull * 1000ull) {
            chgb_decay_all(&g_bond);
            decayed = now;
        }
    }
    vna_sess_poll(h, now); /* handshakes, the active fetch, expiry, visits */
    if (g_aipi_on) aipi_vna_tick(&g_av, now);
    vna_drain_link(h, now);

    /* The bus and the door. The space ticks (host lookups, presence,
     * member expiry) and the door ages its streams; then each listener's
     * accepted conn feeds what arrived, sends what the door produced —
     * one segment at a time, the stack's stop-and-wait — and re-listens
     * when it ends. */
    vna_spaces_tick(&h->spaces, now);
    vna_door_tick(&h->door, now);
    for (i = 0; i < VNA_DOOR_CONNS; i++) {
        int32_t ds = h->door_sock[i], dc = h->door_slot[i];
        uint8_t buf[VNAH_DOOR_TX];
        int32_t n;
        if (ds < 0) { /* the socket closed through: take a fresh listener */
            int32_t ns = net_socket(&h->l->net, SOCK_TCP);
            if (ns >= 0 && net_bind(&h->l->net, ns, VNAH_DOOR_PORT) == 0 &&
                net_listen(&h->l->net, ns, 1) == 0)
                h->door_sock[i] = ns;
            else if (ns >= 0)
                net_close(&h->l->net, ns);
            continue;
        }
        if (dc < 0) {
            socket_t *s = &h->l->net.sockets[ds];
            if (s->tcp_idx >= 0) {
                tcp_state_t st = h->l->net.tcp_conns[s->tcp_idx].state;
                if (st != TCPS_LISTEN && st != TCPS_SYN_RCVD && st != TCPS_ESTABLISHED) {
                    h->l->net.tcp_conns[s->tcp_idx].state = TCPS_CLOSED;
                    net_close(&h->l->net, ds);
                    h->door_sock[i] = -1;
                    continue;
                }
            }
            if (net_accept(&h->l->net, ds) < 0) continue;
            {
                word168_t pid;
                vna_zero(&pid, sizeof pid);
                pid.bytes[0] = s->remote_ip[0];
                pid.bytes[1] = s->remote_ip[1];
                pid.bytes[2] = s->remote_ip[2];
                pid.bytes[3] = s->remote_ip[3];
                if (!porter_house_admit(&g_porter, VNAH_DOOR_PORT, &pid, 500u)) {
                    g_bunker_bounces++;
                    if (s->tcp_idx >= 0) h->l->net.tcp_conns[s->tcp_idx].state = TCPS_CLOSED;
                    net_close(&h->l->net, ds);
                    h->door_sock[i] = -1;
                    continue;
                }
            }
            dc = vna_door_open(&h->door);
            if (dc < 0) {
                if (s->tcp_idx >= 0) h->l->net.tcp_conns[s->tcp_idx].state = TCPS_CLOSED;
                net_close(&h->l->net, ds);
                h->door_sock[i] = -1;
                continue;
            }
            h->door_slot[i] = dc;
        }
        while ((n = net_recv(&h->l->net, ds, buf, sizeof buf)) > 0)
            vna_door_input(&h->door, (uint32_t) dc, buf, (uint32_t) n);
        while (h->door_tx_off[i] >= h->door_tx_len[i]) {
            uint32_t m = vna_door_pull(&h->door, (uint32_t) dc, h->door_tx[i], VNAH_DOOR_TX);
            if (!m) break;
            h->door_tx_len[i] = (uint16_t) m;
            h->door_tx_off[i] = 0;
        }
        if (h->door_tx_off[i] < h->door_tx_len[i]) {
            int32_t w = net_send(&h->l->net, ds, h->door_tx[i] + h->door_tx_off[i],
                                 (uint32_t) (h->door_tx_len[i] - h->door_tx_off[i]));
            if (w > 0) h->door_tx_off[i] += (uint16_t) w;
        }
        if (h->door.conn[dc].state == VNA_DOOR_DONE || net_accept(&h->l->net, ds) < 0) {
            socket_t *s = &h->l->net.sockets[ds];
            if (h->door.conn[dc].head_len == 0) g_bunker_bounces++;
            net_close(&h->l->net, ds);
            if (s->tcp_idx >= 0) {
                h->l->net.tcp_conns[s->tcp_idx].state = TCPS_CLOSED;
                net_close(&h->l->net, ds);
            }
            vna_door_close(&h->door, (uint32_t) dc);
            h->door_slot[i] = -1;
            h->door_sock[i] = -1;
            h->door_tx_len[i] = h->door_tx_off[i] = 0;
        }
    }

    /* the re-send ring first (older datagrams), then whatever the node just
     * queued */
    i = 0;
    while (i < h->npend) {
        vnah_pend_t *p = &h->pend[i];
        if (vna_sendto(h, p->addr, p->data, p->len) != 0) {
            i++;
            continue;
        }
        h->npend--;
        if (i < h->npend) *p = h->pend[h->npend];
    }
    vna_flush(h);
}

uint64_t vna_host_take_earned(vna_host_t *h)
{
    uint64_t e;
    if (!h || !h->up) return 0;
    e = h->book.held_in[SWARM_CAP_INTELLECTUAL] + h->book.held_in[SWARM_CAP_FINANCIAL];
    if (e <= h->earned_posted) return 0;
    e -= h->earned_posted;
    h->earned_posted += e;
    return e;
}

void vna_host_report(const vna_host_t *h)
{
    vna_contact_t c[8];
    uint32_t n, ubh_n = 0, i;
    if (!h || !h->up) return;
    n = vna_node_contacts(&h->node, c, 8);
    for (i = 0; i < n; i++)
        if (c[i].flags & VNA_CF_UBH) ubh_n++;
    uart_puts("  [VNA] id ");
    put_hex_bytes(h->idn.id.b, 8);
    uart_puts(" shard ");
    uart_put_dec(h->shard_slot);
    uart_puts(" contacts ");
    uart_put_dec(n);
    uart_puts(" ubh ");
    uart_put_dec(ubh_n);
    uart_puts(" seeds ");
    uart_put_dec(h->seeds);
    uart_puts(" rx ");
    uart_put_dec(h->recv);
    uart_puts(" tx ");
    uart_put_dec(h->sent);
    uart_puts(" refused ");
    uart_put_dec(h->refused);
    uart_puts(" vrej ");
    uart_put_dec(h->node.v.rej_parse);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_dst);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_dup);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_stale);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_pow);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_binding);
    uart_putc('/');
    uart_put_dec(h->node.v.rej_sig);
    uart_puts(" dropped ");
    uart_put_dec(h->dropped_tx + h->dropped_ob + h->ob.dropped);
    uart_puts(" cycle ");
    uart_put_dec(h->book.cycle);
    uart_puts(" pool ");
    uart_put_dec(h->book.pool[SWARM_CAP_INTELLECTUAL]);
    uart_puts(" held_in ");
    uart_put_dec(h->book.held_in[SWARM_CAP_INTELLECTUAL]);
    uart_puts(" sold ");
    uart_put_dec(h->sold);
    uart_puts(" traded ");
    uart_put_dec(h->link.committed + h->link.confirmed);
    uart_puts(" sync ");
    uart_put_dec(h->shard_announces + h->shard_queries);
    uart_puts(" refused_econ ");
    uart_put_dec(h->refused_econ + h->link.refused);
    uart_puts(" cisex entries/indexed ");
    uart_put_dec(g_cisex.count);
    uart_putc('/');
    uart_put_dec(g_car_indexed);
    uart_puts(" sync in/out ");
    uart_put_dec(h->idx_in);
    uart_putc('/');
    uart_put_dec(h->idx_out);
    uart_puts(" pins/evicts ");
    uart_put_dec(g_pins);
    uart_putc('/');
    uart_put_dec(g_pin_evicts);
    /* the chiglet: judged members, its social layer, the bond's read */
    uart_puts(" chg d/u ");
    uart_put_dec(g_chg_decided);
    uart_putc('/');
    uart_put_dec(g_chg_uncertain);
    uart_puts(" visits ");
    uart_put_dec(g_chg_visits);
    uart_puts(" claims ");
    uart_put_dec(g_chg_claims_in);
    uart_putc('/');
    uart_put_dec(g_chg_claims_out);
    uart_puts(" xp ");
    uart_put_dec(g_chg_xp_gain);
    uart_puts(" dna ");
    uart_put_dec(g_chg_dna_in);
    uart_puts(" bond ");
    uart_put_dec(g_bond_obs);
    if (g_bond_sug_ok) {
        uart_puts(g_bond_sug.state == CHGB_SUGGEST
                      ? "->act"
                      : (g_bond_sug.state == CHGB_UNSURE ? "->?" : "->-"));
        uart_put_dec(g_bond_sug.action == CHGB_NONE ? 0 : g_bond_sug.action);
    }
    uart_puts("\n");
}
