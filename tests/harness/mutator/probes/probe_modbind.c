/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_modbind.c: differential probe for kernel/src/modbind/modbind.c and
 * zxv_decl_gate.c (classify.py). Random module graphs through the whole API,
 * and the boot gate over random declaration tables, printing everything the
 * gate prints. Like test_modbind.c, it supplies the declaration table itself
 * (ELF, x86_64 or aarch64). */
#include <string.h>
#include "probe_common.h"
#include "modbind.h"
#include "zxv_decl.h"

#define NDECL 8
static mb_module_t g_dm[NDECL];
static zxv_decl_t g_decl[NDECL];
const zxv_decl_t *const zxv_test_decls[NDECL] = {&g_decl[0], &g_decl[1], &g_decl[2], &g_decl[3],
                                                 &g_decl[4], &g_decl[5], &g_decl[6], &g_decl[7]};
_Static_assert(sizeof(void *) == 8, "the table end below is 8 x 8 bytes");
__asm__(".globl __zxv_decl_start\n\t.set __zxv_decl_start, zxv_test_decls\n\t"
        ".globl __zxv_decl_end\n\t.set __zxv_decl_end, zxv_test_decls + 64\n");

static int stub_pack(const void *l, uint8_t *o, uint32_t m) { (void)l; (void)o; (void)m; return 0; }
static int stub_unpack(const uint8_t *i, uint32_t l, void *o) { (void)i; (void)l; (void)o; return 0; }

static void set_name(char *dst, uint32_t cap, const char *src) {
    memset(dst, 0, cap);
    for (uint32_t i = 0; i < cap && src[i]; i++) dst[i] = src[i];
}

static const char *const POOL[] = {"mm_ready", "irq", "clock", "sha256", "rat", "zphi", "tls",
                                   "net", "vfs", "ledger", "swarm", "gpu",
                                   "a_capability_name_24chrs", "a_capability_name_24chrX"};
#define POOL_N 14u
static const char *const MODN[] = {"base", "mid", "top", "hw", "leaf", "quiet", "aux", "edge",
                                   "a_module_name_of_24_char", "a_module_name_of_24_chaX", "x", "mod11"};
#define MODN_N 12u

static mb_cap_t rcap(int provided) {
    mb_cap_t c;
    memset(&c, 0, sizeof c);
    set_name(c.name, MB_CAP_NAME_LEN, POOL[probe_below(probe_below(4) ? 8 : POOL_N)]);
    c.contract = (uint16_t)(probe_below(6) == 0 ? 2 + probe_below(400) : 1);
    c.phase = (uint8_t)(probe_below(3) == 0 ? MB_PHASE_INHERIT : probe_below(probe_below(8) ? 13 : 20));
    static const uint8_t amps[] = {1, 1, 1, 1, 2, 3, 4, 5, 0, 6};
    c.amplitude = provided ? amps[probe_below(10)] : (uint8_t)(probe_below(5) ? 1 : probe_below(7));
    c.alternating = (uint8_t)(probe_below(5) == 0);
    return c;
}

static mb_module_t rmod(const char *name, uint32_t richness) {
    mb_module_t m;
    memset(&m, 0, sizeof m);
    set_name(m.name, MB_NAME_LEN, name);
    m.phase = (uint8_t)probe_below(40);
    m.ready = (mb_ready_t)probe_below(3);
    uint32_t np = probe_below(richness + 1), nr = probe_below(richness + 2);
    if (np > MB_MAX_CAPS) np = MB_MAX_CAPS;
    if (nr > MB_MAX_CAPS) nr = MB_MAX_CAPS;
    for (uint32_t j = 0; j < np; j++) m.provides[m.n_provides++] = rcap(1);
    for (uint32_t j = 0; j < nr; j++) m.requires[m.n_requires++] = rcap(0);
    uint32_t ne = probe_below(3), ni = probe_below(3);
    for (uint32_t j = 0; j < ne; j++) {
        m.emits[m.n_emits].form = (mb_form_t)probe_below(MB_FORM__COUNT + 1);
        m.emits[m.n_emits].version = (uint16_t)(1 + probe_below(2));
        m.n_emits++;
    }
    for (uint32_t j = 0; j < ni; j++) {
        m.ingests[m.n_ingests].form = (mb_form_t)probe_below(MB_FORM__COUNT + 1);
        m.ingests[m.n_ingests].version = (uint16_t)(1 + probe_below(2));
        m.n_ingests++;
    }
    if (probe_below(6)) m.xform.pack = stub_pack;
    if (probe_below(6)) m.xform.unpack = stub_unpack;
    if (probe_below(25) == 0) m.n_provides = MB_MAX_CAPS + 1; /* malformed */
    if (probe_below(25) == 0) m.n_emits = MB_MAX_PORTS + 1;
    return m;
}

static void pcap(const mb_cap_t *c) {
    printf(" [%.*s c%u p%u a%u x%u]", (int)MB_CAP_NAME_LEN, c->name, c->contract, c->phase, c->amplitude, c->alternating);
}
static void pmod(const mb_module_t *m) {
    printf("  %.*s ph%u rd%d reg%d e%u i%u p%u r%u pk%d un%d", (int)MB_NAME_LEN, m->name, m->phase, (int)m->ready,
           (int)m->registered, m->n_emits, m->n_ingests, m->n_provides, m->n_requires, m->xform.pack != 0,
           m->xform.unpack != 0);
    for (uint32_t j = 0; j < m->n_emits && j < MB_MAX_PORTS; j++) printf(" E%d.%u", (int)m->emits[j].form, m->emits[j].version);
    for (uint32_t j = 0; j < m->n_ingests && j < MB_MAX_PORTS; j++) printf(" I%d.%u", (int)m->ingests[j].form, m->ingests[j].version);
    for (uint32_t j = 0; j < m->n_provides && j < MB_MAX_CAPS; j++) pcap(&m->provides[j]);
    printf(" |");
    for (uint32_t j = 0; j < m->n_requires && j < MB_MAX_CAPS; j++) pcap(&m->requires[j]);
    printf("\n");
}
static void dump_reg(const char *tag) {
    uint32_t n = modbind_count();
    printf("%s count %u carrier %d\n", tag, n, (int)mb_carrier());
    for (uint32_t i = 0; i <= n; i++) {
        const mb_module_t *m = modbind_get(i);
        if (!m) { printf("  get %u null\n", i); continue; }
        pmod(m);
        printf("   check %d hold %d %s find %d\n", (int)modbind_check(m), (int)modbind_hold_reason(m),
               mb_hold_name(modbind_hold_reason(m)), modbind_find(m->name) == m);
    }
}

static char out_buf[1 << 17];
static size_t out_len;
static void cap_puts(const char *s) {
    size_t n = strlen(s);
    if (out_len + n < sizeof out_buf) {
        memcpy(out_buf + out_len, s, n);
        out_len += n;
        out_buf[out_len] = 0;
    }
}
static int rc_of[NDECL], calls[NDECL];
#define BU_FN(k) static int bu##k(void) { calls[k]++; return rc_of[k]; }
BU_FN(0) BU_FN(1) BU_FN(2) BU_FN(3) BU_FN(4) BU_FN(5) BU_FN(6) BU_FN(7)
static zxv_bringup_fn const BU[NDECL] = {bu0, bu1, bu2, bu3, bu4, bu5, bu6, bu7};

static void tables(void) {
    for (uint32_t a = 0; a < 16; a++) {
        printf("pf %u:", a);
        for (uint32_t b = 0; b < 16; b++) printf(" %u", mb_power_factor((uint8_t)a, (uint8_t)b));
        printf("\n");
    }
    printf("pf ext %u %u %u\n", mb_power_factor(255, 0), mb_power_factor(0, 255), mb_power_factor(200, 7));
    mb_cap_t p, r;
    for (int carrier = 0; carrier < 2; carrier++) {
        if (carrier) mb_carrier_up(); else mb_carrier_down();
        for (uint32_t k = 0; k < 400; k++) {
            probe_seed(1000 + k);
            p = rcap(1);
            r = rcap(0);
            if (k % 3 == 0) memcpy(r.name, p.name, sizeof r.name);
            if (k % 7 == 0) r.contract = p.contract;
            printf("rp %d", mb_real_power(&p, &r));
            pcap(&p); pcap(&r); printf("\n");
        }
    }
    printf("rp null %d %d\n", mb_real_power(NULL, &r), mb_real_power(&p, NULL));
    for (int f = -1; f <= MB_FORM__COUNT + 1; f++) printf("form %d %s\n", f, mb_form_name((mb_form_t)f));
    for (int e = -1; e <= 11; e++) printf("err %d %s\n", e, mb_err_name((mb_err_t)e));
    for (int h = -1; h <= 6; h++) printf("hold %d %s\n", h, mb_hold_name((mb_hold_t)h));
    printf("hold null %d check null %d can null %d\n", (int)modbind_hold_reason(NULL), (int)modbind_check(NULL),
           (int)modbind_can_bind(NULL, NULL, MB_FORM_BINARY));
}

static void api_walk(uint64_t seed) {
    probe_seed(seed);
    modbind_reset();
    if (seed % 2) mb_carrier_up(); else mb_carrier_down();
    uint32_t n = seed % 9 == 0 ? MB_MAX_MODULES + 3 : 2 + probe_below(16);
    for (uint32_t i = 0; i < n; i++) {
        char nm[40];
        if (n > 20) snprintf(nm, sizeof nm, "m%u", i);
        else snprintf(nm, sizeof nm, "%s", MODN[probe_below(MODN_N)]);
        if (probe_below(30) == 0) nm[0] = 0;
        mb_module_t m = rmod(nm, n > 20 ? 1 : 3);
        bool ok = modbind_register(&m);
        if (n <= 20 || !ok) printf("reg [%s] %d\n", nm, (int)ok);
        if (probe_below(4) == 0) printf("  check-unreg %d\n", (int)modbind_check(&m));
    }
    printf("register null %d\n", (int)modbind_register(NULL));
    mb_err_t e1 = MB_OK, e2 = MB_OK;
    const char *w1 = 0, *w2 = 0;
    uint32_t v1 = modbind_verify(&e1, &w1), v2 = modbind_verify_graph(&e2, &w2);
    printf("verify %u %d %s graph %u %d %s\n", v1, (int)e1, w1 ? w1 : "-", v2, (int)e2, w2 ? w2 : "-");
    printf("verify nulls %u %u\n", modbind_verify(NULL, NULL), modbind_verify_graph(NULL, NULL));
    printf("resolve %u\n", modbind_resolve());
    if (n <= 40) dump_reg("resolved");
    uint32_t cnt = modbind_count();
    for (uint32_t k = 0; k < 40 && cnt; k++) {
        const mb_module_t *a = modbind_get(probe_below(cnt)), *b = modbind_get(probe_below(cnt));
        printf(" bind %d", (int)modbind_can_bind(a, b, (mb_form_t)probe_below(MB_FORM__COUNT + 1)));
    }
    printf("\n");
    for (uint32_t k = 0; k < 6; k++) {
        const char *c = POOL[probe_below(POOL_N)];
        uint32_t w = modbind_withdraw(c);
        printf("withdraw %s %u", c, w);
        if (n <= 40) dump_reg(" w");
        if (probe_below(2)) mb_carrier_up(); else if (probe_below(3) == 0) mb_carrier_down();
        printf(" resolve %u", modbind_resolve());
        uint32_t r = modbind_restore(probe_below(5) ? c : POOL[probe_below(POOL_N)]);
        printf(" restore %u\n", r);
        if (n <= 40) dump_reg(" r");
    }
    printf("withdraw null %u restore null %u\n", modbind_withdraw(NULL), modbind_restore(NULL));
    for (uint32_t k = 0; k < 8 && cnt; k++) {
        const char *parts[6];
        uint32_t np = probe_below(6);
        for (uint32_t j = 0; j < np; j++) {
            const mb_module_t *pm = modbind_get(probe_below(cnt + 1));
            parts[j] = pm ? pm->name : (probe_below(2) ? "nobody" : NULL);
        }
        mb_module_t out;
        memset(&out, 0x5A, sizeof out);
        bool ok = modbind_compose(probe_below(10) ? "composite" : "", np ? parts : NULL, np, &out);
        printf("compose %u -> %d", np, (int)ok);
        if (ok) pmod(&out); else printf("\n");
        if (ok && probe_below(2)) printf("  reg composite %d check %d\n", (int)modbind_register(&out), (int)modbind_check(&out));
    }
    printf("compose null %d\n", (int)modbind_compose("c", NULL, 0, NULL));
}

static void gate_case(uint64_t seed) {
    probe_seed(seed);
    static const char *const nm[NDECL] = {"base", "mid", "top", "hw", "leaf", "quiet", "aux", "edge"};
    for (int i = 0; i < NDECL; i++) {
        memset(&g_dm[i], 0, sizeof g_dm[i]);
        set_name(g_dm[i].name, MB_NAME_LEN, nm[i]);
        g_decl[i].mod = &g_dm[i];
        g_decl[i].bringup = probe_below(5) ? BU[i] : ZXV_NO_BRINGUP;
        rc_of[i] = probe_below(6) == 0 ? (probe_below(2) ? -1 : MB_BRINGUP_HELD) : 0;
        calls[i] = 0;
    }
    /* a backbone, so most graphs are sound and the gate goes all the way */
    for (int i = 0; i < NDECL; i++) {
        mb_cap_t c;
        memset(&c, 0, sizeof c);
        snprintf(c.name, sizeof c.name, "cap%d", i);
        c.contract = 1;
        c.phase = probe_below(3) ? MB_PHASE_INHERIT : (uint8_t)probe_below(13);
        c.amplitude = probe_below(8) ? 1 : (uint8_t)probe_below(6);
        g_dm[i].provides[g_dm[i].n_provides++] = c;
        g_dm[i].phase = (uint8_t)probe_below(13);
        if (i) {
            mb_cap_t r = c;
            snprintf(r.name, sizeof r.name, "cap%u", probe_below((uint32_t)i));
            r.phase = probe_below(3) ? MB_PHASE_INHERIT : (uint8_t)probe_below(13);
            g_dm[i].requires[g_dm[i].n_requires++] = r;
        }
    }
    uint32_t breaks = seed % 4 == 0 ? probe_below(3) : 0;
    for (uint32_t b = 0; b < breaks; b++) {
        mb_module_t *m = &g_dm[probe_below(NDECL)];
        mb_cap_t r;
        memset(&r, 0, sizeof r);
        uint32_t kind = probe_below(4);
        if (kind == 0) snprintf(r.name, sizeof r.name, "ghost%u", b);            /* unprovided */
        else if (kind == 1) snprintf(r.name, sizeof r.name, "cap%d", NDECL - 1); /* maybe a cycle */
        else snprintf(r.name, sizeof r.name, "cap%u", probe_below(NDECL));
        r.contract = 1;
        r.amplitude = 1;
        r.phase = MB_PHASE_INHERIT;
        if (kind == 2 && m->n_provides < MB_MAX_CAPS) { /* a second provider, maybe another contract */
            r.contract = (uint16_t)(1 + probe_below(3) * 151);
            m->provides[m->n_provides++] = r;
        } else if (m->n_requires < MB_MAX_CAPS) {
            m->requires[m->n_requires++] = r;
        }
    }
    if (seed % 11 == 0) g_dm[3].name[0] = 0; /* refused */
    if (seed % 13 == 0) memcpy(g_dm[4].name, g_dm[2].name, MB_NAME_LEN); /* duplicate */
    out_len = 0;
    out_buf[0] = 0;
    modbind_reset();
    if (seed % 5 == 0) mb_carrier_down(); else mb_carrier_up();
    uint32_t r = zxv_decl_boot_gate(cap_puts);
    printf("gate %llu -> %u calls", (unsigned long long)seed, r);
    for (int i = 0; i < NDECL; i++) printf(" %d", calls[i]);
    printf("\n%s", out_buf);
    for (uint32_t i = 0; i < 3; i++) printf(" failed %s held %s\n", zxv_decl_failed_name(i) ? zxv_decl_failed_name(i) : "-",
                                            zxv_decl_held_name(i) ? zxv_decl_held_name(i) : "-");
    dump_reg("after gate");
}

int main(void) {
    printf("decls %u gate0 %u\n", zxv_decl_count(), zxv_decl_boot_gate(0));
    tables();
    for (uint64_t s = 1; s <= 40; s++) api_walk(s);
    for (uint64_t s = 1; s <= 90; s++) gate_case(s);
    return 0;
}
