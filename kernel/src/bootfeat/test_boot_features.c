/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_boot_features.c — exact-output tests for the platform and economy
 * roll-calls (boot_features.h). Both run against the REAL subsystem modules
 * (deploy, theme, icon, font, bridge, update, mage, reality, sigil, onepolicy,
 * zcapital, crown, ministry, ipfs); every evidence line is captured through the
 * puts callback and compared byte for byte, and the returned tallies are
 * checked. The build links with -Wl,--wrap=<fn> for the calls whose result
 * decides an [OK]: each wrapper passes through to the real function, records
 * the arguments boot_features passed, and can be told to fail, so both the
 * success and the failure branch of every self-check are exercised.
 * tvl_bringup is stubbed here: the TOL VOVINA modules refuse to build with
 * TEST_HOST (they need the Q32.32 surplus_real_t), and the roll-call only
 * hands them the puts callback and ignores the result. */
#include <stdio.h>
#include <string.h>
#include "boot_features.h"
#include "deploy.h"
#include "theme.h"
#include "icon.h"
#include "font.h"
#include "bridge.h"
#include "reality.h"
#include "sigil.h"
#include "onepolicy.h"
#include "zcapital.h"
#include "crown.h"
#include "ministry.h"
#include "ipfs.h"
#include "tvl_bringup.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

/* ---- captured output ---- */
#define MAXL 32
static char lines[MAXL][160];
static int nlines;
static void cap(const char *s)
{
    if (nlines < MAXL) snprintf(lines[nlines], sizeof lines[0], "%s", s);
    nlines++;
}

/* ---- fault knobs and recorded arguments ---- */
static struct {
    int deploy_mode; /* 1: override the profile */
    deploy_caps_t caps;
    int theme_mode; /* 1: return theme_val */
    uint32_t theme_val;
    int icon_zero, icon_builtin;      /* icon_zero: n_ops = icon_zero - 1 */
    int font_reg_mode, font_res_mode; /* 1: return the value below */
    int32_t font_reg_val, font_res_val;
    int bridge_bad; /* 1..3: misclassify the web2/web3/web4 probe */
    int sig_card, n_nodes, n_edges;
    uint8_t node[4][2], edge[4][2];
    uint64_t tick_ord;
    int tick_mode;
    uint32_t tick_val;
    int tvl_calls;
    tvl_puts_t tvl_puts;
    int op_calls, op_mode; /* 1: fair -> false, 2: usury -> true */
    op_term_t op_term[2];
    int zcap_mode;
    zcap_vec_t zcap_v;
    int zcap_from, zcap_to;
    surplus_real_t zcap_amt;
    int crown_mode;
    int32_t crown_val;
    uint8_t crown_subject[32];
    int crown_method;
    uint32_t crown_caps;
    int min_mode;
    surplus_real_t min_arg;
    int ipfs_mode; /* 1: corrupt byte 0, 2: corrupt byte 1, 3: corrupt byte 31 */
    uint8_t ipfs_data[8];
    uint32_t ipfs_len;
} F;

void __real_deploy_resolve(const deploy_caps_t *caps, deploy_profile_t *out);
void __wrap_deploy_resolve(const deploy_caps_t *caps, deploy_profile_t *out)
{
    F.caps = *caps;
    __real_deploy_resolve(caps, out);
    if (F.deploy_mode == 1) {
        out->klass = DEPLOY_HPC;
        out->max_cells = 1024;
        out->target_concurrency = 0;
    } else if (F.deploy_mode == 2) {
        out->klass = DEPLOY_CLUSTER;
        out->max_cells = 4294967295u;
        out->target_concurrency = 90807;
    }
}

uint32_t __real_theme_get(const theme_t *t, theme_color_t slot);
uint32_t __wrap_theme_get(const theme_t *t, theme_color_t slot)
{
    uint32_t v = __real_theme_get(t, slot);
    return F.theme_mode ? F.theme_val : v;
}

void __real_icon_for(const char *name, icon_t *out);
void __wrap_icon_for(const char *name, icon_t *out)
{
    __real_icon_for(name, out);
    if (F.icon_zero) out->n_ops = (uint8_t) (F.icon_zero - 1);
}
bool __real_icon_is_builtin(const char *name);
bool __wrap_icon_is_builtin(const char *name)
{
    return F.icon_builtin ? true : __real_icon_is_builtin(name);
}

int32_t __real_font_register(font_registry_t *r, const char *family, font_script_t sc,
                             font_style_t style, const char *asset);
int32_t __wrap_font_register(font_registry_t *r, const char *family, font_script_t sc,
                             font_style_t style, const char *asset)
{
    int32_t v = __real_font_register(r, family, sc, style, asset);
    return F.font_reg_mode ? F.font_reg_val : v;
}
int32_t __real_font_resolve(const font_registry_t *r, font_script_t sc, font_style_t pref);
int32_t __wrap_font_resolve(const font_registry_t *r, font_script_t sc, font_style_t pref)
{
    int32_t v = __real_font_resolve(r, sc, pref);
    return F.font_res_mode ? F.font_res_val : v;
}

bridge_realm_t __real_bridge_classify(const char *uri);
bridge_realm_t __wrap_bridge_classify(const char *uri)
{
    bridge_realm_t r = __real_bridge_classify(uri);
    if ((F.bridge_bad == 1 && r == REALM_WEB2) || (F.bridge_bad == 2 && r == REALM_WEB3) ||
        (F.bridge_bad == 3 && r == REALM_WEB4))
        return REALM_UNKNOWN;
    return r;
}

void __real_sig_init(sigil_t *s, uint32_t card_index);
void __wrap_sig_init(sigil_t *s, uint32_t card_index)
{
    F.sig_card = (int) card_index;
    __real_sig_init(s, card_index);
}
bool __real_sig_add_node(sigil_t *s, uint8_t col, uint8_t row);
bool __wrap_sig_add_node(sigil_t *s, uint8_t col, uint8_t row)
{
    if (F.n_nodes < 4) {
        F.node[F.n_nodes][0] = col;
        F.node[F.n_nodes][1] = row;
    }
    F.n_nodes++;
    return __real_sig_add_node(s, col, row);
}
bool __real_sig_add_edge(sigil_t *s, uint8_t a, uint8_t b);
bool __wrap_sig_add_edge(sigil_t *s, uint8_t a, uint8_t b)
{
    if (F.n_edges < 4) {
        F.edge[F.n_edges][0] = a;
        F.edge[F.n_edges][1] = b;
    }
    F.n_edges++;
    return __real_sig_add_edge(s, a, b);
}
uint32_t __real_reality_tick(reality_engine_t *e, uint64_t ordinal);
uint32_t __wrap_reality_tick(reality_engine_t *e, uint64_t ordinal)
{
    F.tick_ord = ordinal;
    uint32_t v = __real_reality_tick(e, ordinal);
    return F.tick_mode ? F.tick_val : v;
}

/* TEST_HOST stand-in (see the header comment) */
unsigned tvl_bringup(tvl_puts_t puts)
{
    F.tvl_calls++;
    F.tvl_puts = puts;
    puts("[TVL] stub\n");
    return 3;
}

bool __real_op_symbiotic_ok(const op_term_t *t);
bool __wrap_op_symbiotic_ok(const op_term_t *t)
{
    int i = F.op_calls++;
    if (i < 2) F.op_term[i] = *t;
    bool v = __real_op_symbiotic_ok(t);
    if (F.op_mode == 1 && i == 0) return false;
    if (F.op_mode == 2 && i == 1) return true;
    return v;
}

zcap_result_t __real_zcap_exchange(zcap_vec_t *v, zcap_form_t from, zcap_form_t to,
                                   surplus_real_t amount);
zcap_result_t __wrap_zcap_exchange(zcap_vec_t *v, zcap_form_t from, zcap_form_t to,
                                   surplus_real_t amount)
{
    F.zcap_v = *v;
    F.zcap_from = (int) from;
    F.zcap_to = (int) to;
    F.zcap_amt = amount;
    zcap_result_t r = __real_zcap_exchange(v, from, to, amount);
    return F.zcap_mode ? ZCAP_OK : r;
}

int32_t __real_crown_register(crown_t *cr, const uint8_t subject[32], crown_id_method_t method,
                              uint32_t capability_bits, crown_isc_t *out);
int32_t __wrap_crown_register(crown_t *cr, const uint8_t subject[32], crown_id_method_t method,
                              uint32_t capability_bits, crown_isc_t *out)
{
    memcpy(F.crown_subject, subject, 32);
    F.crown_method = (int) method;
    F.crown_caps = capability_bits;
    int32_t r = __real_crown_register(cr, subject, method, capability_bits, out);
    return F.crown_mode ? F.crown_val : r;
}

surplus_real_t __real_ministry_tribute(surplus_real_t amount);
surplus_real_t __wrap_ministry_tribute(surplus_real_t amount)
{
    F.min_arg = amount;
    surplus_real_t r = __real_ministry_tribute(amount);
    return F.min_mode ? r + SR_FROM_INT(1) : r;
}

int32_t __real_ipfs_cid_from_bytes(const uint8_t *data, uint32_t len,
                                   uint8_t out_cid[IPFS_CID_LEN]);
int32_t __wrap_ipfs_cid_from_bytes(const uint8_t *data, uint32_t len, uint8_t out_cid[IPFS_CID_LEN])
{
    F.ipfs_len = len;
    memcpy(F.ipfs_data, data, len < 8 ? len : 8);
    int32_t r = __real_ipfs_cid_from_bytes(data, len, out_cid);
    if (F.ipfs_mode == 1) out_cid[0] ^= 1;
    if (F.ipfs_mode == 2) out_cid[1] ^= 1;
    if (F.ipfs_mode == 3) out_cid[31] ^= 1;
    return r;
}

/* leave non-zero bytes in the stack the next call will reuse, so a local the
 * roll-call forgets to initialise does not read as zero by luck */
static __attribute__((noinline)) void dirty_stack(void)
{
    volatile uint8_t junk[8192];
    for (unsigned i = 0; i < sizeof junk; i++) junk[i] = 0xEE;
}

static void reset(void)
{
    memset(&F, 0, sizeof F);
    memset(lines, 0, sizeof lines);
    nlines = 0;
}

/* ---- expected platform lines ---- */
static const char *P_HEAD = "[FEAT] Platform layer (phase-tick ordered)...\n";
static const char *P_THEME = "  [OK] theme: ACCENT=0xFF6600 (customisable)\n";
static const char *P_ICON = "  [OK] icon: 'terminal' ops=4 (procedural)\n";
static const char *P_FONT = "  [OK] font: registered face #0, Latin resolves\n";
static const char *P_BRIDGE = "  [OK] bridge: web2/web3/web4 classify OK\n";
static const char *P_UPD = "  [OK] update: decentralised, opt-in (0 pending by default)\n";
static const char *P_MAGE = "  [OK] mage: hat framework up (self-test on own assets allowed)\n";
static const char *P_REAL = "  [OK] reality: sigil circuit ticked (reactions=2)\n";
static const char *P_TVL = "[TVL] stub\n";

static void expect_platform(const char *deploy, const char *theme, const char *icon,
                            const char *font, const char *bridge, const char *real,
                            const char *summary, const char *what)
{
    const char *want[] = {P_HEAD, deploy, theme, icon,    font, bridge,
                          P_UPD,  P_MAGE, real,  summary, P_TVL};
    int n = (int) (sizeof want / sizeof want[0]);
    int ok = nlines == n;
    for (int i = 0; ok && i < n; i++) {
        if (strcmp(lines[i], want[i]) != 0) {
            printf("  line %d: got  [%s]\n          want [%s]\n", i, lines[i], want[i]);
            ok = 0;
        }
    }
    if (nlines != n) printf("  %d lines, want %d\n", nlines, n);
    CHECK(ok, what);
}

static void test_platform_defaults(void)
{
    CHECK(boot_features_init(NULL, 4, 4096) == 0, "NULL puts -> 0");
    CHECK(boot_economy_init(NULL) == 0, "economy NULL puts -> 0");

    reset();
    CHECK(boot_features_init(cap, 0, 0) == 8, "8/8 on defaults");
    expect_platform(
        "  [OK] deploy: class=embedded cells=2 concurrency=1\n", P_THEME, P_ICON, P_FONT, P_BRIDGE,
        P_REAL, "[FEAT] platform layer up: 8/8 subsystems self-checked\n", "default platform text");
    CHECK(F.caps.cpu_cores == 1 && F.caps.mem_mb == 256, "0/0 -> 1 core, 256 MB");
    CHECK(F.caps.node_count == 1 && F.caps.power == POWER_MAINS && F.caps.has_network,
          "single mains networked node");
    CHECK(F.tvl_calls == 1 && F.tvl_puts == cap, "tvl gets the same puts once");
    CHECK(F.sig_card == 1, "sigil card 1");
    CHECK(F.n_nodes == 2 && F.node[0][0] == 0 && F.node[0][1] == 0 && F.node[1][0] == 1 &&
              F.node[1][1] == 0,
          "nodes (0,0) and (1,0)");
    CHECK(F.n_edges == 1 && F.edge[0][0] == 0 && F.edge[0][1] == 1, "edge 0->1");
    CHECK(F.tick_ord == 1, "tick at ordinal 1");

    reset();
    CHECK(boot_features_init(cap, 64, 65536) == 8, "8/8 on a server");
    CHECK(F.caps.cpu_cores == 64 && F.caps.mem_mb == 65536, "probed caps pass through");
    CHECK(strcmp(lines[1], "  [OK] deploy: class=server cells=64 concurrency=64\n") == 0,
          "server deploy line");

    reset();
    boot_features_init(cap, 3, 0);
    CHECK(F.caps.cpu_cores == 3 && F.caps.mem_mb == 256, "only mem defaults");
    reset();
    boot_features_init(cap, 0, 777);
    CHECK(F.caps.cpu_cores == 1 && F.caps.mem_mb == 777, "only cores default");

    /* the deploy line is whatever deploy_resolve decided, digit for digit */
    reset();
    F.deploy_mode = 1;
    CHECK(boot_features_init(cap, 1, 1) == 8, "deploy always counts");
    CHECK(strcmp(lines[1], "  [OK] deploy: class=supercomputer cells=1024 concurrency=0\n") == 0,
          "1024 and 0 in decimal");
    reset();
    F.deploy_mode = 2;
    boot_features_init(cap, 1, 1);
    CHECK(strcmp(lines[1], "  [OK] deploy: class=cluster cells=4294967295 concurrency=90807\n") ==
              0,
          "UINT_MAX and an inner zero in decimal");
}

static void test_platform_faults(void)
{
    const char *dep = "  [OK] deploy: class=embedded cells=2 concurrency=1\n";
    const char *sum7 = "[FEAT] platform layer up: 7/8 subsystems self-checked\n";
    const char *sum8 = "[FEAT] platform layer up: 8/8 subsystems self-checked\n";

    reset();
    F.theme_mode = 1;
    F.theme_val = 0;
    CHECK(boot_features_init(cap, 0, 0) == 7, "accent 0 is not a colour");
    expect_platform(dep, "  [OK] theme: ACCENT=0x000000 (customisable)\n", P_ICON, P_FONT, P_BRIDGE,
                    P_REAL, sum7, "theme 0 text");
    reset();
    F.theme_mode = 1;
    F.theme_val = 0xAB1C2D3Eu;
    CHECK(boot_features_init(cap, 0, 0) == 8, "any nonzero accent counts");
    CHECK(strcmp(lines[2], "  [OK] theme: ACCENT=0x1C2D3E (customisable)\n") == 0,
          "low 24 bits, upper-case hex");
    reset();
    F.theme_mode = 1;
    F.theme_val = 0x1000000u;
    CHECK(boot_features_init(cap, 0, 0) == 8, "accent with only bit 24 still counts");
    CHECK(strcmp(lines[2], "  [OK] theme: ACCENT=0x000000 (customisable)\n") == 0,
          "bit 24 is not printed");
    reset();
    F.theme_mode = 1;
    F.theme_val = 0x987654u;
    boot_features_init(cap, 0, 0);
    CHECK(strcmp(lines[2], "  [OK] theme: ACCENT=0x987654 (customisable)\n") == 0,
          "every nibble in order");

    reset();
    F.icon_zero = 1;
    CHECK(boot_features_init(cap, 0, 0) == 7, "empty icon does not count");
    expect_platform(dep, P_THEME, "  [OK] icon: 'terminal' ops=0 (procedural)\n", P_FONT, P_BRIDGE,
                    P_REAL, sum7, "empty icon text");
    reset();
    F.icon_zero = 2; /* exactly one op */
    CHECK(boot_features_init(cap, 0, 0) == 8, "a one-op icon counts");
    CHECK(strcmp(lines[3], "  [OK] icon: 'terminal' ops=1 (procedural)\n") == 0, "ops=1");
    reset();
    F.icon_builtin = 1;
    CHECK(boot_features_init(cap, 0, 0) == 8, "builtin icon counts");
    CHECK(strcmp(lines[3], "  [OK] icon: 'terminal' ops=4 (hand-drawn)\n") == 0, "builtin label");

    reset();
    F.font_reg_mode = 1;
    F.font_reg_val = -1;
    F.font_res_mode = 1;
    F.font_res_val = -1;
    CHECK(boot_features_init(cap, 0, 0) == 7, "failed register does not count");
    expect_platform(dep, P_THEME, P_ICON, P_FONT, P_BRIDGE, P_REAL, sum7,
                    "failed register prints face #0");
    reset();
    F.font_reg_mode = 1;
    F.font_reg_val = 7;
    F.font_res_mode = 1;
    F.font_res_val = 7;
    CHECK(boot_features_init(cap, 0, 0) == 8, "face 7 resolving to 7 counts");
    CHECK(strcmp(lines[4], "  [OK] font: registered face #7, Latin resolves\n") == 0, "#7");
    reset();
    F.font_reg_mode = 1;
    F.font_reg_val = 7;
    F.font_res_mode = 1;
    F.font_res_val = 6;
    CHECK(boot_features_init(cap, 0, 0) == 7, "resolving to another face does not count");
    reset();
    F.font_res_mode = 1;
    F.font_res_val = 1;
    CHECK(boot_features_init(cap, 0, 0) == 7, "face 0 resolving to 1 does not count");

    for (int b = 1; b <= 3; b++) {
        reset();
        F.bridge_bad = b;
        CHECK(boot_features_init(cap, 0, 0) == 7, "one misclassified realm fails the bridge");
        expect_platform(dep, P_THEME, P_ICON, P_FONT,
                        "  [OK] bridge: web2/web3/web4 classify FAIL\n", P_REAL, sum7,
                        "bridge FAIL text");
    }

    reset();
    F.tick_mode = 1;
    F.tick_val = 0;
    CHECK(boot_features_init(cap, 0, 0) == 8, "reality always counts");
    expect_platform(dep, P_THEME, P_ICON, P_FONT, P_BRIDGE,
                    "  [OK] reality: sigil circuit ticked (reactions=0)\n", sum8,
                    "reactions=0 text");
    reset();
    F.tick_mode = 1;
    F.tick_val = 305;
    boot_features_init(cap, 0, 0);
    CHECK(strcmp(lines[8], "  [OK] reality: sigil circuit ticked (reactions=305)\n") == 0,
          "reactions=305");

    /* everything that can fail, failing: 4/8 */
    reset();
    F.theme_mode = 1;
    F.icon_zero = 1;
    F.font_res_mode = 1;
    F.font_res_val = 9;
    F.bridge_bad = 2;
    CHECK(boot_features_init(cap, 0, 0) == 4, "4/8 when four self-checks fail");
    CHECK(strcmp(lines[9], "[FEAT] platform layer up: 4/8 subsystems self-checked\n") == 0,
          "4/8 summary");
    CHECK(F.tvl_calls == 1, "tvl still runs");
}

static const char *E_HEAD = "[FEAT] Economy foundation (One Policy + pillars)...\n";
static const char *E_OP = "  [OK] onepolicy: Symbiotic Maxim active (fair term admitted, usury "
                          "voided)\n";
static const char *E_ZC = "  [OK] zcapital: nine capital forms; Crown capital inalienable (no "
                          "price on a language)\n";
static const char *E_CR = "  [OK] crown: the Sicilian Crown issued a sovereign credential "
                          "(issues no money)\n";
static const char *E_MI = "  [OK] ministry: the Illumaheart treasury measures value (tribute "
                          "11%, issues no papers)\n";
static const char *E_IP = "  [OK] ipfs: content-addressed spine up (your hash is your key)\n";

static void expect_economy(const char *op, const char *zc, const char *cr, const char *mi,
                           const char *ip, const char *summary, const char *what)
{
    const char *want[] = {E_HEAD, op, zc, cr, mi, ip, summary};
    int n = (int) (sizeof want / sizeof want[0]);
    int ok = nlines == n;
    for (int i = 0; ok && i < n; i++) {
        if (strcmp(lines[i], want[i]) != 0) {
            printf("  line %d: got  [%s]\n          want [%s]\n", i, lines[i], want[i]);
            ok = 0;
        }
    }
    if (nlines != n) printf("  %d lines, want %d\n", nlines, n);
    CHECK(ok, what);
}

static void test_economy(void)
{
    reset();
    dirty_stack();
    CHECK(boot_economy_init(cap) == 5, "5/5");
    expect_economy(E_OP, E_ZC, E_CR, E_MI, E_IP, "[FEAT] economy foundation up: 5/5 self-checked\n",
                   "economy text");
    CHECK(F.tvl_calls == 0, "economy does not run tvl");

    /* the arguments the roll-call hands each subsystem */
    CHECK(F.op_calls == 2, "two One Policy checks");
    CHECK(F.op_term[0].give_a == 1.0 && F.op_term[0].give_b == 1.0, "fair gives 1:1");
    CHECK(F.op_term[0].reciprocal && F.op_term[0].interest == 0.0, "fair: reciprocal, no interest");
    CHECK(F.op_term[0].harm_a == 0.0 && F.op_term[0].harm_b == 0.0 && !F.op_term[0].denies_aid,
          "fair: zero-initialised rest");
    CHECK(F.op_term[1].interest == 1.0 && F.op_term[1].give_a == 1.0 &&
              F.op_term[1].give_b == 1.0 && F.op_term[1].reciprocal,
          "usury is the fair term plus interest 1");
    CHECK(F.zcap_from == ZCAP_FINANCIAL && F.zcap_to == ZCAP_CULTURAL, "financial -> cultural");
    CHECK(F.zcap_amt == 1.0, "amount 1");
    CHECK(F.zcap_v.bal[ZCAP_FINANCIAL] == 100.0, "balance 100 financial");
    {
        int rest_zero = 1;
        for (int i = 1; i < ZCAP_FORM_COUNT; i++) rest_zero = rest_zero && F.zcap_v.bal[i] == 0.0;
        CHECK(rest_zero, "other forms empty");
    }
    {
        int seq = 1;
        for (int i = 0; i < 32; i++) seq = seq && F.crown_subject[i] == (uint8_t) i;
        CHECK(seq, "subject is 0..31");
    }
    CHECK(F.crown_method == CROWN_ID_DID, "DID method");
    CHECK(F.crown_caps == (CROWN_CAP_GRIDCHAIN | CROWN_CAP_VINO_SETTLE), "caps gridchain|settle");
    CHECK(F.min_arg == 100.0, "tribute of 100");
    CHECK(F.ipfs_len == 3 && memcmp(F.ipfs_data, "abc", 3) == 0, "cid of \"abc\"");

    const char *s4 = "[FEAT] economy foundation up: 4/5 self-checked\n";
    reset();
    F.op_mode = 1;
    CHECK(boot_economy_init(cap) == 4, "fair rejected -> 4");
    expect_economy("  [??] onepolicy\n", E_ZC, E_CR, E_MI, E_IP, s4, "onepolicy ?? text");
    reset();
    F.op_mode = 2;
    CHECK(boot_economy_init(cap) == 4, "usury admitted -> 4");
    expect_economy("  [??] onepolicy\n", E_ZC, E_CR, E_MI, E_IP, s4, "usury ?? text");

    reset();
    F.zcap_mode = 1;
    CHECK(boot_economy_init(cap) == 4, "crown form bought -> 4");
    expect_economy(E_OP, "  [??] zcapital\n", E_CR, E_MI, E_IP, s4, "zcapital ?? text");

    reset();
    F.crown_mode = 1;
    F.crown_val = -1;
    CHECK(boot_economy_init(cap) == 4, "crown refused -> 4");
    expect_economy(E_OP, E_ZC, "  [??] crown\n", E_MI, E_IP, s4, "crown ?? text");
    reset();
    F.crown_mode = 1;
    F.crown_val = 0;
    CHECK(boot_economy_init(cap) == 5, "crown index 0 is success");
    reset();
    F.crown_mode = 1;
    F.crown_val = 3;
    CHECK(boot_economy_init(cap) == 5, "crown index 3 is success");

    reset();
    F.min_mode = 1;
    CHECK(boot_economy_init(cap) == 4, "tribute 12 -> 4");
    expect_economy(E_OP, E_ZC, E_CR, "  [??] ministry\n", E_IP, s4, "ministry ?? text");

    reset();
    F.ipfs_mode = 1;
    CHECK(boot_economy_init(cap) == 4, "cid[0] wrong -> 4");
    expect_economy(E_OP, E_ZC, E_CR, E_MI, "  [??] ipfs\n", s4, "ipfs byte 0 ?? text");
    reset();
    F.ipfs_mode = 2;
    CHECK(boot_economy_init(cap) == 4, "cid[1] wrong -> 4");
    expect_economy(E_OP, E_ZC, E_CR, E_MI, "  [??] ipfs\n", s4, "ipfs byte 1 ?? text");
    reset();
    F.ipfs_mode = 3;
    CHECK(boot_economy_init(cap) == 4, "cid[31] wrong -> 4");
    expect_economy(E_OP, E_ZC, E_CR, E_MI, "  [??] ipfs\n", s4, "ipfs last byte ?? text");

    reset();
    F.op_mode = 1;
    F.zcap_mode = 1;
    F.crown_mode = 1;
    F.crown_val = -5;
    F.min_mode = 1;
    F.ipfs_mode = 1;
    CHECK(boot_economy_init(cap) == 0, "all failing -> 0");
    expect_economy("  [??] onepolicy\n", "  [??] zcapital\n", "  [??] crown\n", "  [??] ministry\n",
                   "  [??] ipfs\n", "[FEAT] economy foundation up: 0/5 self-checked\n", "0/5 text");
}

int main(void)
{
    test_platform_defaults();
    test_platform_faults();
    test_economy();
    printf("test_boot_features: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
