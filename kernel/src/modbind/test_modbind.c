/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_modbind.c — behaviour and property tests for module binding (modbind.c)
 * and the declaration-graph boot gate (zxv_decl_gate.c).
 *
 * Until this file the only checks of either were the boot self-checks, which
 * print [OK] lines but run on one fixed graph. These tests compare modbind
 * against reference models written here from the rules in modbind.h (the
 * readiness fixpoint, withdraw/restore, the port check, composition) over
 * thousands of random graphs, pin the power-factor and real-power tables to
 * hand-computed values, and drive the boot gate through healthy and broken
 * graphs, reading what it prints. Expected values are worked from the header's
 * rules, not read back from the code under test. */
#include <stdio.h>
#include <string.h>
#include "modbind.h"
#include "zxv_decl.h"

static int failures = 0;
static unsigned checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------ helpers */

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(uint32_t n)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t) (rng_state % n);
}

static int stub_pack(const void *l, uint8_t *o, uint32_t m)
{
    (void) l;
    (void) o;
    (void) m;
    return 0;
}
static int stub_unpack(const uint8_t *i, uint32_t l, void *o)
{
    (void) i;
    (void) l;
    (void) o;
    return 0;
}

static void set_name(char *dst, uint32_t cap, const char *src)
{
    memset(dst, 0, cap);
    for (uint32_t i = 0; i < cap && src[i]; i++) dst[i] = src[i];
}

static mb_module_t blank(const char *name)
{
    mb_module_t m;
    memset(&m, 0, sizeof m);
    set_name(m.name, MB_NAME_LEN, name);
    m.ready = MB_HELD;
    return m;
}

static mb_cap_t cap(const char *name, uint16_t contract, uint8_t phase, uint8_t amp)
{
    mb_cap_t c;
    memset(&c, 0, sizeof c);
    set_name(c.name, MB_CAP_NAME_LEN, name);
    c.contract = contract;
    c.phase = phase;
    c.amplitude = amp;
    return c;
}

static void add_prov(mb_module_t *m, mb_cap_t c)
{
    m->provides[m->n_provides++] = c;
}
static void add_req(mb_module_t *m, mb_cap_t c)
{
    m->
        requires[
                m->n_requires++] = c;
}
static void add_emit(mb_module_t *m, mb_form_t f, uint16_t v)
{
    m->emits[m->n_emits].form = f;
    m->emits[m->n_emits].version = v;
    m->n_emits++;
    m->xform.pack = stub_pack;
}
static void add_ingest(mb_module_t *m, mb_form_t f, uint16_t v)
{
    m->ingests[m->n_ingests].form = f;
    m->ingests[m->n_ingests].version = v;
    m->n_ingests++;
    m->xform.unpack = stub_unpack;
}

static const mb_module_t *reg(const char *name)
{
    return modbind_find(name);
}

/* ------------------------------------------------- reference: coupling */

/* The power factor (1000 + cos(2*pi*k/13)) / 2 in permille, worked by hand
 * from cos to three places (1000, 885, 568, 121, -355, -749, -971). */
static const uint32_t PF[13] = {1000, 942, 784, 560, 322, 125, 14, 14, 125, 322, 560, 784, 942};

static int name_same(const char *a, const char *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
    }
    return 1;
}

/* Does a provided capability deliver real power to a requirement? The header:
 * the names agree, the contracts agree, the drive is not zero, and the carrier
 * is up. The phase never zeroes it (the floor is 14). */
static int ref_couples(const mb_cap_t *p, const mb_cap_t *r, int carrier)
{
    if (!carrier) return 0;
    if (!name_same(p->name, r->name, MB_CAP_NAME_LEN)) return 0;
    if (p->contract != r->contract) return 0;
    return p->amplitude >= 1 && p->amplitude <= 5;
}

#define NMAX 48u
static mb_module_t ref_mods[NMAX];
static uint32_t ref_n;
static mb_ready_t ref_ready[NMAX];

static void ref_resolve(int carrier)
{
    for (uint32_t i = 0; i < ref_n; i++)
        if (ref_ready[i] != MB_WITHDRAWN) ref_ready[i] = MB_HELD;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (uint32_t i = 0; i < ref_n; i++) {
            if (ref_ready[i] != MB_HELD) continue;
            int all = 1;
            for (uint32_t j = 0; j < ref_mods[i].n_requires && all; j++) {
                int got = 0;
                for (uint32_t k = 0; k < ref_n && !got; k++) {
                    if (ref_ready[k] != MB_READY) continue;
                    for (uint32_t l = 0; l < ref_mods[k].n_provides; l++)
                        if (ref_couples(&ref_mods[k].provides[l], &ref_mods[i].requires[j],
                                        carrier))
                            got = 1;
                }
                if (!got) all = 0;
            }
            if (all) {
                ref_ready[i] = MB_READY;
                changed = 1;
            }
        }
    }
}

static uint32_t ref_count_ready(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < ref_n; i++) n += ref_ready[i] == MB_READY;
    return n;
}

static int ref_provides_name(uint32_t i, const char *c)
{
    for (uint32_t j = 0; j < ref_mods[i].n_provides; j++)
        if (name_same(ref_mods[i].provides[j].name, c, MB_CAP_NAME_LEN)) return 1;
    return 0;
}

static uint32_t ref_withdraw(const char *c)
{
    uint32_t before = ref_count_ready();
    for (uint32_t i = 0; i < ref_n; i++)
        if (ref_provides_name(i, c)) ref_ready[i] = MB_WITHDRAWN;
    ref_resolve(1);
    uint32_t after = ref_count_ready();
    return before > after ? before - after : 0;
}

static uint32_t ref_restore(const char *c)
{
    uint32_t before = ref_count_ready();
    for (uint32_t i = 0; i < ref_n; i++)
        if (ref_ready[i] == MB_WITHDRAWN && ref_provides_name(i, c)) ref_ready[i] = MB_HELD;
    ref_resolve(1);
    uint32_t after = ref_count_ready();
    return after > before ? after - before : 0;
}

static int same_ready_as_registry(void)
{
    if (modbind_count() != ref_n) return 0;
    for (uint32_t i = 0; i < ref_n; i++)
        if (modbind_get(i)->ready != ref_ready[i]) return 0;
    return 1;
}

static const char *const POOL[] = {"mm_ready", "irq", "clock", "sha256", "rat",   "zphi",
                                   "tls",      "net", "vfs",   "ledger", "swarm", "gpu"};
#define POOL_N 12u

static mb_cap_t random_cap(int provided)
{
    mb_cap_t c = cap(POOL[rnd(POOL_N)], (uint16_t) (rnd(5) == 0 ? 2 : 1),
                     (uint8_t) (rnd(4) == 0 ? MB_PHASE_INHERIT : rnd(13)), MB_AMP_FULL);
    if (provided) {
        /* mostly full drive, sometimes each other level, the alias, zero and
         * an out-of-range value (6), which drives nothing */
        static const uint8_t amps[] = {1, 1, 1, 1, 2, 3, 4, 5, 0, 6};
        c.amplitude = amps[rnd(sizeof amps)];
    }
    return c;
}

/* Build a random graph, register it, and mirror it into the reference with
 * the phase inheritance applied exactly as the header describes. */
static void random_graph(uint32_t n)
{
    modbind_reset();
    ref_n = 0;
    for (uint32_t i = 0; i < n; i++) {
        char nm[MB_NAME_LEN];
        snprintf(nm, sizeof nm, "mod%u", i);
        mb_module_t m = blank(nm);
        m.phase = (uint8_t) rnd(40); /* above 12 on purpose: taken mod 13 */
        uint32_t np = rnd(3), nr = rnd(4);
        for (uint32_t j = 0; j < np; j++) add_prov(&m, random_cap(1));
        for (uint32_t j = 0; j < nr; j++) add_req(&m, random_cap(0));
        add_emit(&m, MB_FORM_BINARY, 1);
        CHECK(modbind_register(&m), "random module registers");
        for (uint32_t j = 0; j < m.n_provides; j++)
            if (m.provides[j].phase == MB_PHASE_INHERIT) m.provides[j].phase = m.phase % 13;
        for (uint32_t j = 0; j < m.n_requires; j++)
            if (m.requires[j].phase == MB_PHASE_INHERIT)
        m.
            requires[
                    j]
                .phase = m.phase % 13;
        m.registered = true;
        ref_mods[ref_n] = m;
        ref_ready[ref_n] = MB_HELD;
        ref_n++;
    }
}

/* ------------------------------------------------------------ tests */

static void test_power_tables(void)
{
    mb_carrier_up();
    for (uint32_t k = 0; k < 13; k++) {
        CHECK(mb_power_factor((uint8_t) k, 0) == PF[k], "power factor table");
        CHECK(mb_power_factor(0, (uint8_t) k) == PF[k], "power factor symmetric");
        CHECK(mb_power_factor((uint8_t) (k + 13), 0) == PF[k], "power factor wraps at 13");
        CHECK(mb_power_factor((uint8_t) (k + 100), 100) == PF[k], "only the difference counts");
    }
    CHECK(mb_power_factor(255, 0) == PF[255 % 13], "largest phase difference");
    CHECK(mb_power_factor(5, 200) == PF[195 % 13], "difference taken in both directions");

    /* real power = drive * pf / 1000, truncated; the minus glut carries a
     * negative charge. Drives in permille: TRUE 1000, PLUS 750, NEUTRAL 500
     * (and its alias GLUT 2), MINUS 250, FALSE 0, anything else 0. */
    static const int32_t drive[7] = {0, 1000, 500, 750, -250, 500, 0};
    for (uint8_t amp = 0; amp < 7; amp++) {
        for (uint8_t k = 0; k < 13; k++) {
            mb_cap_t p = cap("pwr", 3, k, amp);
            mb_cap_t r = cap("pwr", 3, 0, MB_AMP_FULL);
            int32_t mag = drive[amp] < 0 ? -drive[amp] : drive[amp];
            int32_t want = (int32_t) ((uint32_t) mag * PF[k] / 1000u);
            if (drive[amp] < 0) want = -want;
            CHECK(mb_real_power(&p, &r) == want, "real power = drive x pf");
        }
    }
    {
        mb_cap_t p = cap("pwr", 1, 1, MB_AMP_HIGH), r = cap("pwr", 1, 0, MB_AMP_FULL);
        CHECK(mb_real_power(&p, &r) == 706, "750 x 942 / 1000 truncates to 706");
        p.amplitude = MB_AMP_LOW;
        p.phase = 2;
        CHECK(mb_real_power(&p, &r) == -196, "-(250 x 784 / 1000) = -196");
        CHECK(mb_real_power(0, &r) == 0 && mb_real_power(&p, 0) == 0, "NULL delivers nothing");
        mb_cap_t other = cap("pwx", 1, 0, MB_AMP_FULL);
        CHECK(mb_real_power(&p, &other) == 0, "a different name delivers nothing");
        mb_cap_t v2 = cap("pwr", 2, 0, MB_AMP_FULL);
        CHECK(mb_real_power(&p, &v2) == 0, "a different contract delivers nothing");
        p.amplitude = MB_AMP_FULL;
        p.phase = 0;
        mb_carrier_down();
        CHECK(!mb_carrier(), "carrier reads down");
        CHECK(mb_real_power(&p, &r) == 0, "no carrier, no power");
        mb_carrier_up();
        CHECK(mb_carrier(), "carrier reads up");
        CHECK(mb_real_power(&p, &r) == 1000, "carrier back, full power");
    }
    {
        /* names are compared over all MB_CAP_NAME_LEN bytes, with no NUL needed */
        mb_cap_t a = cap("", 1, 0, MB_AMP_FULL), b = cap("", 1, 0, MB_AMP_FULL);
        memset(a.name, 'q', MB_CAP_NAME_LEN);
        memset(b.name, 'q', MB_CAP_NAME_LEN);
        CHECK(mb_real_power(&a, &b) == 1000, "full-length names that agree couple");
        b.name[MB_CAP_NAME_LEN - 1] = 'r';
        CHECK(mb_real_power(&a, &b) == 0, "the last byte of a name counts");
        b.name[MB_CAP_NAME_LEN - 1] = 'q';
        b.name[0] = 'r';
        CHECK(mb_real_power(&a, &b) == 0, "the first byte of a name counts");
    }
}

static void test_register(void)
{
    modbind_reset();
    CHECK(modbind_count() == 0, "reset empties the registry");
    CHECK(!modbind_register(0), "NULL refused");
    mb_module_t m = blank("");
    CHECK(!modbind_register(&m), "empty name refused");

    m = blank("ports");
    m.n_emits = MB_MAX_PORTS + 1;
    CHECK(!modbind_register(&m), "too many emits refused");
    m = blank("ports");
    m.n_ingests = MB_MAX_PORTS + 1;
    CHECK(!modbind_register(&m), "too many ingests refused");
    m = blank("caps");
    m.n_provides = MB_MAX_CAPS + 1;
    CHECK(!modbind_register(&m), "too many provides refused");
    m = blank("caps");
    m.n_requires = MB_MAX_CAPS + 1;
    CHECK(!modbind_register(&m), "too many requires refused");
    m = blank("caps");
    m.n_provides = MB_MAX_CAPS;
    m.n_requires = MB_MAX_CAPS;
    m.n_emits = 0;
    CHECK(modbind_register(&m), "exactly MB_MAX_CAPS of each accepted");

    m = blank("formnone");
    m.n_emits = 1;
    m.emits[0].form = MB_FORM_NONE;
    CHECK(!modbind_register(&m), "emitting MB_FORM_NONE refused");
    m.emits[0].form = MB_FORM__COUNT;
    CHECK(!modbind_register(&m), "emitting past the enum refused");
    m.emits[0].form = MB_FORM_PHASE;
    m.n_ingests = 1;
    m.ingests[0].form = MB_FORM_NONE;
    CHECK(!modbind_register(&m), "ingesting MB_FORM_NONE refused");
    m.ingests[0].form = MB_FORM__COUNT;
    CHECK(!modbind_register(&m), "ingesting past the enum refused");
    m.ingests[0].form = MB_FORM_PHASE;
    m.n_emits = MB_MAX_PORTS;
    m.n_ingests = MB_MAX_PORTS;
    for (uint32_t i = 0; i < MB_MAX_PORTS; i++) {
        m.emits[i].form = MB_FORM_PHASE;
        m.ingests[i].form = MB_FORM_PHASE;
    }
    CHECK(modbind_register(&m), "every port valid, exactly MB_MAX_PORTS accepted");
    CHECK(modbind_count() == 2, "two accepted so far");
    CHECK(!modbind_register(&m), "duplicate name refused");

    /* full-length names: no NUL needed, the last byte still distinguishes */
    mb_module_t a = blank("");
    memset(a.name, 'z', MB_NAME_LEN);
    CHECK(modbind_register(&a), "full-length name accepted");
    a.name[MB_NAME_LEN - 1] = 'y';
    CHECK(modbind_register(&a), "differs only in the last byte: a different module");
    CHECK(!modbind_register(&a), "and that one is now a duplicate");
    CHECK(modbind_count() == 4, "four registered");
    CHECK(modbind_find(0) == 0, "find(NULL) is NULL");
    CHECK(modbind_find("nobody") == 0, "find of an unknown name is NULL");
    CHECK(modbind_find("formnone") == modbind_get(1), "find returns the registered copy");
    CHECK(modbind_get(4) == 0 && modbind_get(1000) == 0, "get past the end is NULL");
    CHECK(modbind_get(0)->registered && modbind_get(3)->registered, "registered flag set");

    /* phase inheritance: resolved once, to the module's phase mod 13 */
    mb_module_t p = blank("phased");
    p.phase = 30;
    add_prov(&p, cap("x", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_prov(&p, cap("y", 1, 7, MB_AMP_FULL));
    add_req(&p, cap("z", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&p, cap("w", 1, 12, MB_AMP_FULL));
    CHECK(modbind_register(&p), "phased registers");
    const mb_module_t *rp = reg("phased");
    CHECK(rp && rp->provides[0].phase == 4, "inherited provide rides phase 30 mod 13 = 4");
    CHECK(rp && rp->provides[1].phase == 7, "explicit provide phase kept");
    CHECK(rp && rp->requires[0].phase == 4, "inherited require rides phase 4");
    CHECK(rp && rp->requires[1].phase == 12, "explicit require phase kept");
    CHECK(p.provides[0].phase == MB_PHASE_INHERIT, "the caller's declaration is not written");

    /* capacity: exactly MB_MAX_MODULES fit; the next is refused */
    modbind_reset();
    for (uint32_t i = 0; i < MB_MAX_MODULES; i++) {
        char nm[MB_NAME_LEN];
        snprintf(nm, sizeof nm, "cap%u", i);
        mb_module_t c = blank(nm);
        if (!modbind_register(&c)) {
            CHECK(0, "a module below the ceiling was refused");
            break;
        }
    }
    CHECK(modbind_count() == MB_MAX_MODULES, "registry holds MB_MAX_MODULES");
    mb_module_t over = blank("over");
    CHECK(!modbind_register(&over), "one past MB_MAX_MODULES refused");
    CHECK(modbind_count() == MB_MAX_MODULES, "refusal leaves the count");
    modbind_reset();
    CHECK(modbind_count() == 0 && modbind_find("cap0") == 0, "reset forgets every module");
    CHECK(modbind_register(&over), "after reset the table takes modules again");
}

/* The port check, against a reference worked from the header: a module with
 * no ports cannot be in a system; for each emit then each ingest in order: a
 * missing marshaller is NO_XFORM; no peer at all for the form and version is
 * ORPHAN (emit) or STARVED (ingest), unless a peer speaks the form at another
 * version (VERSION). TRIT is sound today, so BLOCKED never fires. */
static mb_err_t ref_check(uint32_t i)
{
    const mb_module_t *m = &ref_mods[i];
    if (m->n_emits == 0 && m->n_ingests == 0) return MB_ERR_NO_PORTS;
    for (int pass = 0; pass < 2; pass++) {
        uint8_t n = pass ? m->n_ingests : m->n_emits;
        const mb_port_t *ports = pass ? m->ingests : m->emits;
        for (uint8_t a = 0; a < n; a++) {
            if (pass ? !m->xform.unpack : !m->xform.pack) return MB_ERR_NO_XFORM;
            int match = 0, other = 0;
            for (uint32_t k = 0; k < ref_n; k++) {
                if (k == i) continue;
                uint8_t on = pass ? ref_mods[k].n_emits : ref_mods[k].n_ingests;
                const mb_port_t *op = pass ? ref_mods[k].emits : ref_mods[k].ingests;
                for (uint8_t b = 0; b < on; b++) {
                    if (op[b].form != ports[a].form) continue;
                    if (op[b].version == ports[a].version)
                        match = 1;
                    else
                        other = 1;
                }
            }
            if (!match) return other ? MB_ERR_VERSION : (pass ? MB_ERR_STARVED : MB_ERR_ORPHAN);
        }
    }
    return MB_OK;
}

static int ref_can_bind(uint32_t f, uint32_t t, mb_form_t form)
{
    if (form == MB_FORM_NONE || form >= MB_FORM__COUNT) return 0;
    if (!ref_mods[f].xform.pack || !ref_mods[t].xform.unpack) return 0;
    for (uint8_t a = 0; a < ref_mods[f].n_emits; a++)
        for (uint8_t b = 0; b < ref_mods[t].n_ingests; b++)
            if (ref_mods[f].emits[a].form == form && ref_mods[t].ingests[b].form == form &&
                ref_mods[t].ingests[b].version == ref_mods[f].emits[a].version)
                return 1;
    return 0;
}

static void test_ports_random(void)
{
    for (uint32_t round = 0; round < 600; round++) {
        modbind_reset();
        ref_n = 2 + rnd(7);
        for (uint32_t i = 0; i < ref_n; i++) {
            char nm[MB_NAME_LEN];
            snprintf(nm, sizeof nm, "p%u", i);
            mb_module_t m = blank(nm);
            uint32_t ne = rnd(3), ni = rnd(3);
            for (uint32_t j = 0; j < ne; j++)
                add_emit(&m, (mb_form_t) (1 + rnd(5)), (uint16_t) (1 + rnd(2)));
            for (uint32_t j = 0; j < ni; j++)
                add_ingest(&m, (mb_form_t) (1 + rnd(5)), (uint16_t) (1 + rnd(2)));
            if (rnd(6) == 0) m.xform.pack = 0;
            if (rnd(6) == 0) m.xform.unpack = 0;
            CHECK(modbind_register(&m), "port module registers");
            ref_mods[i] = m;
        }
        uint32_t bad = 0;
        const char *first = 0;
        mb_err_t first_err = MB_OK;
        for (uint32_t i = 0; i < ref_n; i++) {
            mb_err_t want = ref_check(i);
            CHECK(modbind_check(modbind_get(i)) == want, "modbind_check = reference");
            if (want != MB_OK) {
                if (!bad) {
                    first = ref_mods[i].name;
                    first_err = want;
                }
                bad++;
            }
            for (uint32_t t = 0; t < ref_n; t++)
                for (uint32_t f = 0; f <= MB_FORM__COUNT; f++)
                    CHECK(modbind_can_bind(modbind_get(i), modbind_get(t), (mb_form_t) f) ==
                              (ref_can_bind(i, t, (mb_form_t) f) != 0),
                          "modbind_can_bind = reference");
        }
        mb_err_t e = MB_ERR_CYCLE;
        const char *nm = "sentinel";
        CHECK(modbind_verify(&e, &nm) == bad, "verify counts the failing modules");
        CHECK(e == first_err, "verify reports the first failure's kind");
        CHECK(bad ? (nm && strcmp(nm, first) == 0) : nm == 0, "verify names the first failure");
        CHECK(modbind_verify(0, 0) == bad, "verify without out-parameters");
    }
    CHECK(modbind_check(0) == MB_ERR_NO_PORTS, "check(NULL) is NO_PORTS");
    CHECK(!modbind_can_bind(0, modbind_get(0), MB_FORM_BINARY), "can_bind(NULL, ...)");
    CHECK(!modbind_can_bind(modbind_get(0), 0, MB_FORM_BINARY), "can_bind(..., NULL)");
    CHECK(strcmp(mb_form_name(MB_FORM_POLY), "poly/F_q") == 0, "form name");
    CHECK(strcmp(mb_form_name((mb_form_t) 99), "?") == 0, "unknown form name");
    CHECK(strncmp(mb_err_name(MB_ERR_CYCLE), "CYCLE", 5) == 0, "error name");
    CHECK(strcmp(mb_err_name((mb_err_t) 99), "?") == 0, "unknown error name");
}

static void test_resolve_random(void)
{
    mb_carrier_up();
    for (uint32_t round = 0; round < 1500; round++) {
        random_graph(4 + rnd(NMAX - 4));
        uint32_t got = modbind_resolve();
        ref_resolve(1);
        CHECK(got == ref_count_ready(), "resolve count = reference fixpoint");
        CHECK(same_ready_as_registry(), "resolve states = reference fixpoint");
        CHECK(modbind_resolve() == got && same_ready_as_registry(), "resolve is idempotent");

        /* withdraw a capability, then restore it: the reverse edge, exact */
        mb_ready_t before[NMAX];
        memcpy(before, ref_ready, sizeof before);
        const char *c = POOL[rnd(POOL_N)];
        uint32_t w = modbind_withdraw(c);
        CHECK(w == ref_withdraw(c), "withdraw un-readies what the reference does");
        CHECK(same_ready_as_registry(), "withdraw states = reference");
        for (uint32_t i = 0; i < ref_n; i++) {
            if (ref_provides_name(i, c))
                CHECK(modbind_get(i)->ready == MB_WITHDRAWN, "every provider is in S-");
            else
                CHECK(modbind_get(i)->ready != MB_WITHDRAWN, "nothing else is in S-");
        }
        CHECK(modbind_resolve() == ref_count_ready() && same_ready_as_registry(),
              "a re-resolve keeps the withdrawal");
        uint32_t back = modbind_restore(c);
        CHECK(back == ref_restore(c), "restore regains what the reference does");
        CHECK(same_ready_as_registry(), "restore states = reference");
        CHECK(memcmp(before, ref_ready, sizeof before) == 0,
              "withdraw then restore is the identity");

        /* two withdrawals, restored in the opposite order */
        const char *c2 = POOL[rnd(POOL_N)];
        modbind_withdraw(c);
        ref_withdraw(c);
        CHECK(modbind_withdraw(c2) == ref_withdraw(c2), "second withdraw = reference");
        CHECK(modbind_restore(c) == ref_restore(c), "restore of the first = reference");
        CHECK(same_ready_as_registry(), "after the first restore = reference");
        CHECK(modbind_restore(c2) == ref_restore(c2), "restore of the second = reference");
        CHECK(same_ready_as_registry(), "after both restores = reference");

        /* the carrier: down, only modules that require nothing come up */
        mb_carrier_down();
        uint32_t dead = modbind_resolve();
        ref_resolve(0);
        uint32_t indep = 0;
        for (uint32_t i = 0; i < ref_n; i++)
            indep += ref_ready[i] != MB_WITHDRAWN && ref_mods[i].n_requires == 0;
        CHECK(dead == indep && same_ready_as_registry(), "carrier down opens every circuit");
        mb_carrier_up();
        CHECK(modbind_resolve() == got, "carrier up restores the graph");
        ref_resolve(1);
        CHECK(same_ready_as_registry(), "carrier up: same states as before");
    }
    CHECK(modbind_withdraw(0) == 0 && modbind_restore(0) == 0, "NULL capability is a no-op");
    random_graph(10);
    modbind_resolve();
    ref_resolve(1);
    CHECK(modbind_withdraw("nobody_provides") == 0, "withdrawing an unnamed cap changes nothing");
    CHECK(same_ready_as_registry(), "and no state moved");
    CHECK(modbind_restore("nobody_provides") == 0, "restoring it changes nothing either");
}

/* ------------------------------------------------- hold reasons + gate */

static void test_hold_reasons(void)
{
    mb_carrier_up();
    modbind_reset();
    mb_module_t base = blank("base"), lost = blank("lost"), down = blank("down");
    mb_module_t dead = blank("deadprov"), weak = blank("weakreq"), weak2 = blank("weakdown");
    mb_module_t ca = blank("cyc_a"), cb = blank("cyc_b"), cdown = blank("cyc_down");
    mb_module_t wprov = blank("wprov"), wreq = blank("wreq"), wdown = blank("wdown");
    mb_module_t mix = blank("mix"), mix2 = blank("mix2"), vreq = blank("vreq");
    add_prov(&base, cap("base", 1, 0, MB_AMP_FULL));
    add_req(&lost, cap("ghost", 1, 0, MB_AMP_FULL)); /* nothing names it */
    add_prov(&lost, cap("lostcap", 1, 0, MB_AMP_FULL));
    add_req(&down, cap("lostcap", 1, 0, MB_AMP_FULL));  /* provider is UNPROVIDED */
    add_prov(&dead, cap("deadcap", 1, 0, MB_AMP_NONE)); /* named, zero drive */
    add_req(&weak, cap("deadcap", 1, 0, MB_AMP_FULL));
    add_prov(&weak, cap("weakcap", 1, 0, MB_AMP_FULL));
    add_req(&weak2, cap("weakcap", 1, 0, MB_AMP_FULL)); /* inherits PHASE */
    add_prov(&ca, cap("a", 1, 0, MB_AMP_FULL));
    add_req(&ca, cap("b", 1, 0, MB_AMP_FULL));
    add_prov(&cb, cap("b", 1, 0, MB_AMP_FULL));
    add_req(&cb, cap("a", 1, 0, MB_AMP_FULL));
    add_req(&cdown, cap("a", 1, 0, MB_AMP_FULL));
    add_prov(&wprov, cap("wcap", 1, 0, MB_AMP_FULL));
    add_req(&wreq, cap("wcap", 1, 0, MB_AMP_FULL));
    add_prov(&wreq, cap("wreqcap", 1, 0, MB_AMP_FULL));
    add_req(&wdown, cap("wreqcap", 1, 0, MB_AMP_FULL));
    add_req(&mix, cap("a", 1, 0, MB_AMP_FULL));        /* cycle */
    add_req(&mix, cap("ghost2", 1, 0, MB_AMP_FULL));   /* and unprovided: that wins */
    add_req(&mix2, cap("wcap", 1, 0, MB_AMP_FULL));    /* withdrawn */
    add_req(&mix2, cap("deadcap", 1, 0, MB_AMP_FULL)); /* and out of phase: that wins */
    add_req(&vreq, cap("base", 2, 0, MB_AMP_FULL));    /* contract 2, provider at 1 */
    mb_module_t *all[] = {&base,  &lost,  &down, &dead,  &weak, &weak2, &ca,  &cb,
                          &cdown, &wprov, &wreq, &wdown, &mix,  &mix2,  &vreq};
    for (uint32_t i = 0; i < sizeof all / sizeof all[0]; i++)
        CHECK(modbind_register(all[i]), "hold graph registers");
    modbind_resolve();
    CHECK(modbind_withdraw("wcap") == 3, "withdraw wcap: wprov, wreq and wdown un-ready");
    CHECK(modbind_hold_reason(reg("base")) == MB_HOLD_NONE, "ready: not held");
    CHECK(modbind_hold_reason(reg("lost")) == MB_HOLD_UNPROVIDED, "nothing names ghost");
    CHECK(modbind_hold_reason(reg("down")) == MB_HOLD_UNPROVIDED, "inherits UNPROVIDED");
    CHECK(modbind_hold_reason(reg("deadprov")) == MB_HOLD_NONE, "zero-drive provider is ready");
    CHECK(modbind_hold_reason(reg("weakreq")) == MB_HOLD_PHASE, "zero drive: out of phase");
    CHECK(modbind_hold_reason(reg("weakdown")) == MB_HOLD_PHASE, "inherits PHASE");
    CHECK(modbind_hold_reason(reg("cyc_a")) == MB_HOLD_CYCLE, "2-cycle: a");
    CHECK(modbind_hold_reason(reg("cyc_b")) == MB_HOLD_CYCLE, "2-cycle: b");
    CHECK(modbind_hold_reason(reg("cyc_down")) == MB_HOLD_CYCLE, "downstream of a cycle");
    CHECK(modbind_hold_reason(reg("wprov")) == MB_HOLD_NONE, "a withdrawn module is not held");
    CHECK(modbind_hold_reason(reg("wreq")) == MB_HOLD_WITHDRAWN, "provider in S-");
    CHECK(modbind_hold_reason(reg("wdown")) == MB_HOLD_WITHDRAWN, "inherits WITHDRAWN");
    CHECK(modbind_hold_reason(reg("mix")) == MB_HOLD_UNPROVIDED, "UNPROVIDED outranks CYCLE");
    CHECK(modbind_hold_reason(reg("mix2")) == MB_HOLD_PHASE, "PHASE outranks WITHDRAWN");
    CHECK(modbind_hold_reason(reg("vreq")) == MB_HOLD_PHASE, "contract mismatch: open circuit");
    CHECK(modbind_hold_reason(0) == MB_HOLD_NONE, "hold_reason(NULL)");
    {
        mb_module_t stranger = blank("stranger");
        CHECK(modbind_hold_reason(&stranger) == MB_HOLD_NONE, "unregistered: not held");
        mb_module_t copy = *reg("cyc_a");
        CHECK(modbind_hold_reason(&copy) == MB_HOLD_CYCLE, "found by name, not only address");
    }
    CHECK(strncmp(mb_hold_name(MB_HOLD_PHASE), "OUT OF PHASE", 12) == 0, "hold name");
    CHECK(strcmp(mb_hold_name((mb_hold_t) 99), "?") == 0, "unknown hold name");

    /* the graph gate: UNPROVIDED per (module, requirement), CONTRACT per pair
     * of disagreeing providers, CYCLE per module held in a cycle, and the
     * first one noted is reported. Withdrawal and phase are not defects. */
    mb_err_t e;
    const char *who;
    uint32_t p = modbind_verify_graph(&e, &who);
    /* lost:ghost, mix:ghost2 = 2 UNPROVIDED; cyc_a, cyc_b, cyc_down = 3 CYCLE
     * (mix is held UNPROVIDED, not CYCLE) */
    CHECK(p == 5, "verify_graph counts 2 unprovided + 3 cycle");
    CHECK(e == MB_ERR_UNPROVIDED && who && strcmp(who, "lost") == 0,
          "unprovided is noted first, at the first module");
    CHECK(reg("wprov")->ready == MB_WITHDRAWN, "verify_graph keeps a withdrawal");

    modbind_reset();
    mb_module_t v1 = blank("prov_v1"), v2 = blank("prov_v2"), v2b = blank("prov_v2b");
    add_prov(&v1, cap("svc", 1, 0, MB_AMP_FULL));
    add_prov(&v2, cap("svc", 2, 0, MB_AMP_FULL));
    add_prov(&v2b, cap("svc", 2, 0, MB_AMP_FULL));
    modbind_register(&v1);
    modbind_register(&v2);
    modbind_register(&v2b);
    p = modbind_verify_graph(&e, &who);
    CHECK(p == 2, "three providers at v1, v2, v2: two disagreeing pairs");
    CHECK(e == MB_ERR_CONTRACT && who && strcmp(who, "prov_v2") == 0,
          "CONTRACT names the later provider of the first pair");
    CHECK(modbind_verify_graph(0, 0) == 2, "verify_graph without out-parameters");

    modbind_reset();
    mb_module_t s = blank("solo"), t = blank("solo_req");
    add_prov(&s, cap("k", 1, 0, MB_AMP_FULL));
    add_req(&t, cap("k", 1, 6, MB_AMP_FULL)); /* antiphase: weak, still coupled */
    modbind_register(&s);
    modbind_register(&t);
    e = MB_ERR_CYCLE;
    who = "sentinel";
    CHECK(modbind_verify_graph(&e, &who) == 0, "an antiphase edge is sound");
    CHECK(e == MB_OK && who == 0, "a sound graph clears the out-parameters");
    CHECK(reg("solo_req")->ready == MB_READY, "and the requirer is ready");
}

/* ------------------------------------------------------------ compose */

static int cap_in_list(const mb_cap_t *l, uint32_t n, const mb_cap_t *c)
{
    for (uint32_t i = 0; i < n; i++)
        if (name_same(l[i].name, c->name, MB_CAP_NAME_LEN)) return 1;
    return 0;
}

static void test_compose(void)
{
    mb_carrier_up();
    uint32_t refused = 0, built = 0;
    for (uint32_t round = 0; round < 1500; round++) {
        modbind_reset();
        ref_n = 3 + rnd(5);
        for (uint32_t i = 0; i < ref_n; i++) {
            char nm[MB_NAME_LEN];
            snprintf(nm, sizeof nm, "part%u", i);
            mb_module_t m = blank(nm);
            uint32_t np = rnd(3), nr = rnd(3), ne = rnd(3), ni = rnd(2);
            for (uint32_t j = 0; j < np; j++) {
                mb_cap_t c = cap(POOL[rnd(6)], (uint16_t) (rnd(8) == 0 ? 2 : 1), (uint8_t) rnd(13),
                                 MB_AMP_FULL);
                add_prov(&m, c);
            }
            for (uint32_t j = 0; j < nr; j++) add_req(&m, cap(POOL[rnd(6)], 1, 0, MB_AMP_FULL));
            for (uint32_t j = 0; j < ne; j++) add_emit(&m, (mb_form_t) (1 + rnd(3)), 1);
            for (uint32_t j = 0; j < ni; j++) add_ingest(&m, (mb_form_t) (1 + rnd(3)), 1);
            modbind_register(&m);
            ref_mods[i] = m;
        }
        uint32_t k = 1 + rnd(ref_n);
        const char *names[NMAX];
        uint32_t idx[NMAX];
        for (uint32_t i = 0; i < k; i++) {
            idx[i] = rnd(ref_n);
            names[i] = ref_mods[idx[i]].name;
        }
        /* reference: provides = union in first-seen order, refused on a
         * contract clash or overflow; requires = union of requires minus the
         * provides, refused on overflow; emits = those no part ingests at the
         * same form and version, up to MB_MAX_PORTS */
        mb_cap_t rp[16], rr[16];
        mb_port_t re[16];
        uint32_t nrp = 0, nrr = 0, nre = 0;
        int ok = 1;
        for (uint32_t i = 0; i < k && ok; i++) {
            const mb_module_t *m = &ref_mods[idx[i]];
            for (uint32_t j = 0; j < m->n_provides && ok; j++) {
                for (uint32_t q = 0; q < nrp; q++)
                    if (name_same(rp[q].name, m->provides[j].name, MB_CAP_NAME_LEN) &&
                        rp[q].contract != m->provides[j].contract)
                        ok = 0;
                if (!ok || cap_in_list(rp, nrp, &m->provides[j])) continue;
                if (nrp >= MB_MAX_CAPS)
                    ok = 0;
                else
                    rp[nrp++] = m->provides[j];
            }
        }
        for (uint32_t i = 0; i < k && ok; i++) {
            const mb_module_t *m = &ref_mods[idx[i]];
            for (uint32_t j = 0; j < m->n_requires && ok; j++) {
                if (cap_in_list(rp, nrp, &m->requires[j]) || cap_in_list(rr, nrr, &m->requires[j]))
                    continue;
                if (nrr >= MB_MAX_CAPS)
                    ok = 0;
                else
                rr[nrr++] = m->
                                requires[
                                    j];
            }
        }
        for (uint32_t i = 0; i < k && ok; i++) {
            const mb_module_t *m = &ref_mods[idx[i]];
            for (uint32_t j = 0; j < m->n_emits; j++) {
                int internal = 0;
                for (uint32_t q = 0; q < k; q++) {
                    const mb_module_t *o = &ref_mods[idx[q]];
                    for (uint32_t z = 0; z < o->n_ingests; z++)
                        if (o->ingests[z].form == m->emits[j].form &&
                            o->ingests[z].version == m->emits[j].version)
                            internal = 1;
                }
                if (!internal && nre < MB_MAX_PORTS) re[nre++] = m->emits[j];
            }
        }
        mb_module_t out;
        memset(&out, 0xA5, sizeof out);
        int got = modbind_compose("composite", names, k, &out);
        CHECK(got == ok, "compose refuses exactly when the reference does");
        if (!got || !ok) {
            refused++;
            continue;
        }
        built++;
        CHECK(strcmp(out.name, "composite") == 0, "composite carries its name");
        CHECK(out.n_provides == nrp && out.n_requires == nrr && out.n_emits == nre,
              "composite boundary sizes = reference");
        for (uint32_t i = 0; i < nrp && i < out.n_provides; i++)
            CHECK(memcmp(&out.provides[i], &rp[i], sizeof rp[i]) == 0, "provides = reference");
        for (uint32_t i = 0; i < nrr && i < out.n_requires; i++)
            CHECK(memcmp(&out.requires[i], &rr[i], sizeof rr[i]) == 0, "requires = reference");
        for (uint32_t i = 0; i < nre && i < out.n_emits; i++)
            CHECK(out.emits[i].form == re[i].form && out.emits[i].version == re[i].version,
                  "emits = reference");
        for (uint32_t i = 0; i < out.n_requires; i++)
            CHECK(!cap_in_list(out.provides, out.n_provides, &out.requires[i]),
                  "an internal edge never appears on the boundary");
        CHECK(out.n_ingests == 0 && out.ready == MB_HELD && !out.registered && out.phase == 0,
              "a composite starts held, unregistered, at phase 0");
        CHECK((out.n_emits > 0) == (out.xform.pack != 0), "a pack exactly when it emits");
        CHECK(out.xform.unpack == 0, "a composite has no unpack");

        /* closure: a composite is a module, and nesting gives the same
         * provides and requires as composing flat (as sets) */
        if (k >= 3) {
            const char *firsttwo[2] = {names[0], names[1]};
            mb_module_t ab;
            if (modbind_compose("ab", firsttwo, 2, &ab) && modbind_register(&ab)) {
                const char *nested[NMAX];
                nested[0] = "ab";
                for (uint32_t i = 2; i < k; i++) nested[i - 1] = names[i];
                mb_module_t abc;
                if (modbind_compose("abc", nested, k - 1, &abc)) {
                    int same = abc.n_provides == out.n_provides && abc.n_requires == out.n_requires;
                    for (uint32_t i = 0; same && i < abc.n_provides; i++)
                        same = cap_in_list(out.provides, out.n_provides, &abc.provides[i]);
                    for (uint32_t i = 0; same && i < abc.n_requires; i++)
                        same = cap_in_list(out.requires, out.n_requires, &abc.requires[i]);
                    CHECK(same, "compose(compose(a,b),c) = compose(a,b,c)");
                }
            }
        }
    }
    CHECK(refused > 50 && built > 500, "both outcomes exercised");
    const char *one[1] = {"part0"};
    mb_module_t out;
    CHECK(!modbind_compose(0, one, 1, &out), "compose: NULL name");
    CHECK(!modbind_compose("c", 0, 1, &out), "compose: NULL parts");
    CHECK(!modbind_compose("c", one, 1, 0), "compose: NULL out");
    CHECK(!modbind_compose("c", one, 0, &out), "compose: no parts");
    const char *missing[2] = {"part0", "nobody"};
    CHECK(!modbind_compose("c", missing, 2, &out), "compose: a part that is not registered");
}

static void test_selfcheck(void)
{
    mb_carrier_up();
    CHECK(modbind_selfcheck() == 0, "selfcheck passes with the carrier up");
    CHECK(modbind_count() == 0, "selfcheck leaves the registry empty");
    CHECK(mb_carrier(), "selfcheck restores a carrier that was up");
    mb_carrier_down();
    CHECK(modbind_selfcheck() == 0, "selfcheck passes with the carrier down");
    CHECK(!mb_carrier(), "selfcheck restores a carrier that was down");
    mb_carrier_up();
}

/* ------------------------------------------- edges found by mutation
 * Each block kills a surviving mutant that the differential probe saw change
 * behaviour (tests/MUTATION_REPORT.md). Expected values follow modbind.h. */

static int stub_pack2(const void *l, uint8_t *o, uint32_t m)
{
    (void) l;
    (void) o;
    (void) m;
    return 1;
}

static void test_register_edges(void)
{
    mb_carrier_up();
    modbind_reset();
    mb_module_t e = blank("");
    CHECK(!modbind_register(&e), "an empty name is refused");
    mb_module_t x = blank("x");
    CHECK(modbind_register(&x), "a one-letter name registers");

    /* names fill all MB_NAME_LEN bytes with no terminator: equal names are a
     * duplicate whatever the struct holds after the name */
    modbind_reset();
    mb_module_t a = blank(""), b;
    memset(a.name, 'n', MB_NAME_LEN);
    add_emit(&a, MB_FORM_TRISPACE, 1);
    b = a;
    b.emits[0].form = MB_FORM_POLY;
    CHECK(modbind_register(&a), "a full-length name registers");
    CHECK(!modbind_register(&b), "the same full-length name is a duplicate");

    /* every port is validated, not only the first */
    modbind_reset();
    mb_module_t p = blank("ports");
    add_emit(&p, MB_FORM_TRISPACE, 1);
    add_emit(&p, MB_FORM_NONE, 1);
    CHECK(!modbind_register(&p), "a second emit of form NONE is refused");
    p = blank("ports");
    add_ingest(&p, MB_FORM_TRISPACE, 1);
    add_ingest(&p, MB_FORM__COUNT, 1);
    CHECK(!modbind_register(&p), "a second ingest past the last form is refused");

    /* every INHERIT capability takes the module's phase, at the full count */
    mb_module_t ph = blank("phased");
    ph.phase = 5;
    for (int i = 0; i < (int) MB_MAX_CAPS; i++) {
        char nm[8] = {'p', (char) ('0' + i), 0};
        add_prov(&ph, cap(nm, 1, MB_PHASE_INHERIT, MB_AMP_FULL));
        nm[0] = 'r';
        add_req(&ph, cap(nm, 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    }
    CHECK(modbind_register(&ph), "four provides and four requires");
    const mb_module_t *rp = reg("phased");
    int all5 = 1;
    for (int i = 0; i < (int) MB_MAX_CAPS; i++)
    all5 = all5 && rp->provides[i].phase == 5 &&
           rp->
               requires[
                       i]
                   .phase == 5;
    CHECK(all5, "every inherited phase resolves to the module's");

    /* a full-length capability name couples to itself */
    modbind_reset();
    mb_module_t lp = blank("longprov"), lr = blank("longreq");
    mb_cap_t lc = cap("", 1, 0, MB_AMP_FULL);
    memset(lc.name, 'c', MB_CAP_NAME_LEN);
    add_prov(&lp, lc);
    add_req(&lr, lc);
    modbind_register(&lp);
    modbind_register(&lr);
    CHECK(modbind_resolve() == 2, "a full-length capability name couples");

    /* the port check reads ports[0..n) only, and at the full count */
    modbind_reset();
    mb_module_t em = blank("emitter"), wide = blank("wide");
    add_emit(&em, MB_FORM_PHASE, 1);
    for (int i = 0; i < (int) MB_MAX_PORTS; i++)
        add_ingest(&wide, MB_FORM_BINARY, (uint16_t) (i + 1));
    modbind_register(&em);
    modbind_register(&wide);
    CHECK(modbind_check(reg("emitter")) == MB_ERR_ORPHAN, "nobody ingests PHASE: orphan");
    modbind_reset();
    mb_module_t stale = blank("stale");
    add_ingest(&stale, MB_FORM_BINARY, 1);
    stale.ingests[1].form = MB_FORM_PHASE; /* past n_ingests: not declared */
    stale.ingests[1].version = 2;
    modbind_register(&em);
    modbind_register(&stale);
    CHECK(modbind_check(reg("emitter")) == MB_ERR_ORPHAN,
          "a port past the count is not a version mismatch");
}

static void test_hold_edges(void)
{
    mb_carrier_up();
    modbind_reset();
    /* uprov* are held UNPROVIDED, deadp provides a zero-drive capability,
     * phheld is held OUT OF PHASE, base is ready */
    mb_module_t base = blank("base"), deadp = blank("deadp");
    mb_module_t up1 = blank("uprov1"), up2 = blank("uprov2"), phheld = blank("phheld");
    mb_module_t ba = blank("b_a"), bc = blank("b_c"), bd = blank("b_d"), be = blank("b_e");
    mb_module_t named1 = blank("named1"), held2 = blank("held2"), r1 = blank("r1"),
                r2 = blank("r2");
    add_prov(&base, cap("base", 1, 0, MB_AMP_FULL));
    add_prov(&deadp, cap("dq", 1, 0, MB_AMP_NONE));
    add_req(&up1, cap("ghost", 1, 0, MB_AMP_FULL));
    add_prov(&up1, cap("x", 1, 0, MB_AMP_FULL));
    add_prov(&up1, cap("z", 1, 0, MB_AMP_FULL));
    add_req(&up2, cap("ghost", 1, 0, MB_AMP_FULL));
    add_req(&phheld, cap("dq", 1, 0, MB_AMP_FULL));
    add_prov(&phheld, cap("other", 1, 0, MB_AMP_FULL));
    add_prov(&phheld, cap("v", 1, 0, MB_AMP_FULL));
    add_prov(&phheld, cap("z", 1, 0, MB_AMP_FULL));
    /* b_a: an inherited UNPROVIDED, then an out-of-phase requirement */
    add_req(&ba, cap("x", 1, 0, MB_AMP_FULL));
    add_req(&ba, cap("dq", 1, 0, MB_AMP_FULL));
    /* b_c: z from uprov1 (UNPROVIDED) and from phheld (PHASE) */
    add_req(&bc, cap("z", 1, 0, MB_AMP_FULL));
    /* b_d: out of phase, then a requirement met by a ready provider */
    add_req(&bd, cap("dq", 1, 0, MB_AMP_FULL));
    add_req(&bd, cap("base", 1, 0, MB_AMP_FULL));
    /* b_e: v is phheld's second provide */
    add_req(&be, cap("v", 1, 0, MB_AMP_FULL));
    /* the capability sits at provides[1] */
    add_prov(&named1, cap("zz", 1, 0, MB_AMP_FULL));
    add_prov(&named1, cap("pc", 1, 0, MB_AMP_NONE));
    add_req(&r1, cap("pc", 1, 0, MB_AMP_FULL));
    add_req(&held2, cap("ghost", 1, 0, MB_AMP_FULL));
    add_prov(&held2, cap("zz2", 1, 0, MB_AMP_FULL));
    add_prov(&held2, cap("pc2", 1, 0, MB_AMP_FULL));
    add_req(&r2, cap("pc2", 1, 0, MB_AMP_FULL));
    mb_module_t *all[] = {&base, &deadp, &up1,    &up2,   &phheld, &ba, &bc,
                          &bd,   &be,    &named1, &held2, &r1,     &r2};
    for (uint32_t i = 0; i < sizeof all / sizeof all[0]; i++)
        CHECK(modbind_register(all[i]), "hold-edge graph registers");
    modbind_resolve();
    CHECK(modbind_hold_reason(reg("phheld")) == MB_HOLD_PHASE, "phheld: out of phase");
    CHECK(modbind_hold_reason(reg("b_a")) == MB_HOLD_UNPROVIDED,
          "an inherited UNPROVIDED outranks a later PHASE");
    CHECK(modbind_hold_reason(reg("b_c")) == MB_HOLD_UNPROVIDED,
          "the strongest reason among held providers, in any order");
    CHECK(modbind_hold_reason(reg("b_d")) == MB_HOLD_PHASE,
          "a requirement that is met explains nothing");
    CHECK(modbind_hold_reason(reg("b_e")) == MB_HOLD_PHASE, "every provide of a provider is read");
    CHECK(modbind_hold_reason(reg("r1")) == MB_HOLD_PHASE, "named at provides[1]: out of phase");
    CHECK(modbind_hold_reason(reg("r2")) == MB_HOLD_UNPROVIDED,
          "coupled at provides[1]: inherits the provider's reason");
    mb_err_t err;
    const char *who;
    /* ghost: uprov1, uprov2, held2 = 3 UNPROVIDED, nothing else */
    CHECK(modbind_verify_graph(&err, &who) == 3, "three unprovided, no cycle");

    /* a withdrawn provider listed after a held one does not hide the held
     * one's stronger reason */
    modbind_reset();
    mb_module_t a2 = blank("a2"), w = blank("w"), b2 = blank("b2");
    add_req(&a2, cap("ghost", 1, 0, MB_AMP_FULL));
    add_prov(&a2, cap("y", 1, 0, MB_AMP_FULL));
    add_prov(&w, cap("y", 1, 0, MB_AMP_FULL));
    add_prov(&w, cap("wcap", 1, 0, MB_AMP_FULL));
    add_req(&b2, cap("y", 1, 0, MB_AMP_FULL));
    modbind_register(&a2);
    modbind_register(&w);
    modbind_register(&b2);
    modbind_resolve();
    CHECK(reg("b2")->ready == MB_READY, "b2 ready through w");
    CHECK(modbind_withdraw("wcap") == 2, "withdrawing wcap un-readies w and b2");
    CHECK(modbind_hold_reason(reg("b2")) == MB_HOLD_UNPROVIDED,
          "UNPROVIDED from a2 outranks WITHDRAWN from w");

    /* a module held, then ready: its old reason does not stick */
    modbind_reset();
    mb_module_t m0 = blank("m0"), late = blank("late");
    add_req(&m0, cap("latecap", 1, 0, MB_AMP_FULL));
    add_prov(&late, cap("latecap", 1, 0, MB_AMP_FULL));
    modbind_register(&m0);
    modbind_resolve();
    CHECK(modbind_hold_reason(reg("m0")) == MB_HOLD_UNPROVIDED, "m0 waits for latecap");
    CHECK(modbind_verify_graph(&err, &who) == 1 && err == MB_ERR_UNPROVIDED && who &&
              strcmp(who, "m0") == 0,
          "the first module's requirement is checked");
    modbind_register(&late);
    modbind_resolve();
    CHECK(modbind_hold_reason(reg("m0")) == MB_HOLD_NONE, "ready now: not held");
    CHECK(modbind_verify_graph(&err, &who) == 0, "and the graph is sound");
}

static void test_verify_graph_edges(void)
{
    mb_err_t err;
    const char *who;
    mb_carrier_up();
    /* a provider left behind by modbind_reset is not a provider */
    modbind_reset();
    mb_module_t filler = blank("filler"), gone = blank("gone");
    add_prov(&gone, cap("gonecap", 1, 0, MB_AMP_FULL));
    add_prov(&gone, cap("svc", 2, 0, MB_AMP_FULL));
    modbind_register(&filler);
    modbind_register(&gone);
    modbind_reset();
    mb_module_t needs = blank("needs");
    add_req(&needs, cap("gonecap", 1, 0, MB_AMP_FULL));
    add_prov(&needs, cap("svc", 1, 0, MB_AMP_FULL));
    modbind_register(&needs);
    CHECK(modbind_verify_graph(&err, &who) == 1 && err == MB_ERR_UNPROVIDED,
          "after reset the old provider is gone, and no stale contract clashes");
    CHECK(modbind_hold_reason(reg("needs")) == MB_HOLD_UNPROVIDED,
          "after reset nothing names the old capability");

    /* modules left behind by modbind_reset are not held in a cycle */
    modbind_reset();
    mb_module_t ya = blank("cyc_ya"), yb = blank("cyc_yb"), one = blank("one");
    add_prov(&ya, cap("ya", 1, 0, MB_AMP_FULL));
    add_req(&ya, cap("yb", 1, 0, MB_AMP_FULL));
    add_prov(&yb, cap("yb", 1, 0, MB_AMP_FULL));
    add_req(&yb, cap("ya", 1, 0, MB_AMP_FULL));
    modbind_register(&ya);
    modbind_register(&yb);
    CHECK(modbind_verify_graph(&err, &who) == 2 && err == MB_ERR_CYCLE, "a 2-cycle");
    modbind_reset();
    modbind_register(&one);
    CHECK(modbind_verify_graph(&err, &who) == 0, "after reset the old cycle is gone");

    /* a requirement met at provides[1] */
    modbind_reset();
    mb_module_t pv = blank("pv"), rq = blank("rq");
    add_prov(&pv, cap("first", 1, 0, MB_AMP_FULL));
    add_prov(&pv, cap("second", 1, 0, MB_AMP_FULL));
    add_req(&rq, cap("second", 1, 0, MB_AMP_FULL));
    modbind_register(&pv);
    modbind_register(&rq);
    CHECK(modbind_verify_graph(&err, &who) == 0, "provides[1] provides");

    /* a contract clash between later modules, at provides[1] on both sides */
    modbind_reset();
    mb_module_t c0 = blank("c0"), c1 = blank("c1"), c2 = blank("c2");
    add_prov(&c0, cap("lone", 1, 0, MB_AMP_FULL));
    add_prov(&c1, cap("pad1", 1, 0, MB_AMP_FULL));
    add_prov(&c1, cap("svc", 1, 0, MB_AMP_FULL));
    add_prov(&c2, cap("pad2", 1, 0, MB_AMP_FULL));
    add_prov(&c2, cap("svc", 2, 0, MB_AMP_FULL));
    modbind_register(&c0);
    modbind_register(&c1);
    modbind_register(&c2);
    CHECK(modbind_verify_graph(&err, &who) == 1 && err == MB_ERR_CONTRACT && who &&
              strcmp(who, "c2") == 0,
          "a clash between modules 1 and 2 at provides[1]");
}

static void test_compose_edges(void)
{
    mb_carrier_up();
    modbind_reset();
    mb_module_t a = blank("ca"), b = blank("cb"), c = blank("cc");
    add_emit(&a, MB_FORM_PHASE, 1); /* ingested by cb at ingests[1]: internal */
    add_ingest(&b, MB_FORM_BINARY, 1);
    add_ingest(&b, MB_FORM_PHASE, 1);
    add_emit(&b, MB_FORM_POLY, 1);
    b.xform.pack = stub_pack;
    add_emit(&c, MB_FORM_TRISPACE, 1);
    c.xform.pack = stub_pack2;
    modbind_register(&a);
    modbind_register(&b);
    modbind_register(&c);
    const char *const parts[] = {"ca", "cb", "cc"};
    mb_module_t out;
    CHECK(modbind_compose("comp", parts, 3, &out), "compose three parts");
    CHECK(out.n_emits == 2 && out.emits[0].form == MB_FORM_POLY &&
              out.emits[1].form == MB_FORM_TRISPACE,
          "PHASE is internal (ingested at ingests[1]); POLY and TRISPACE surface");
    CHECK(out.xform.pack == stub_pack, "the composite packs with its first surfacing part");
}

/* Fill the stack below the caller with a byte, so a local the code under test
 * forgets to initialise reads that byte instead of whatever zero happens to be
 * there. */
static void __attribute__((noinline)) dirty_stack(unsigned char v)
{
    volatile unsigned char buf[32768];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = v;
}

static const mb_form_t FOUR_FORMS[4] = {MB_FORM_BINARY, MB_FORM_TRISPACE, MB_FORM_POLY,
                                        MB_FORM_PHASE};

/* Every array at its bound: MB_MAX_MODULES modules, each with MB_MAX_CAPS
 * provides and requires and MB_MAX_PORTS emits and ingests, through every
 * entry point; then the same entry points on a small graph registered over
 * the leftovers of the big one. Under tiers --mode san a read one past any
 * count is a failure. */
static void test_full_registry(void)
{
    static mb_module_t big[MB_MAX_MODULES];
    char nm[MB_NAME_LEN];
    mb_carrier_up();
    modbind_reset();
    for (uint32_t i = 0; i < MB_MAX_MODULES; i++) {
        snprintf(nm, sizeof nm, "m%u", i);
        big[i] = blank(nm);
        for (uint32_t k = 0; k < MB_MAX_CAPS; k++) {
            snprintf(nm, sizeof nm, "c%u_%u", i, k);
            add_prov(&big[i], cap(nm, 1, 0, MB_AMP_FULL));
            if (i) {
                snprintf(nm, sizeof nm, "c%u_%u", i - 1, k);
                add_req(&big[i], cap(nm, 1, 0, MB_AMP_FULL));
            }
        }
        for (uint32_t k = 0; k < MB_MAX_PORTS; k++) {
            add_emit(&big[i], FOUR_FORMS[k], 1);
            add_ingest(&big[i], FOUR_FORMS[k], 1);
        }
        CHECK(modbind_register(&big[i]), "the full registry registers");
    }
    mb_module_t extra = blank("extra");
    CHECK(!modbind_register(&extra), "one past MB_MAX_MODULES is refused");
    CHECK(modbind_resolve() == MB_MAX_MODULES, "a chain of 192 resolves");
    CHECK(modbind_hold_reason(reg("m191")) == MB_HOLD_NONE, "the end of the chain is ready");
    CHECK(modbind_hold_reason(&extra) == MB_HOLD_NONE, "an unregistered module is not held");
    CHECK(modbind_find("nobody") == 0, "not found among 192");
    CHECK(modbind_verify_graph(0, 0) == 0, "the full chain is sound");
    CHECK(modbind_check(reg("m5")) == MB_OK, "every port has a partner");
    CHECK(modbind_can_bind(reg("m0"), reg("m1"), MB_FORM_PHASE), "m0 -> m1 over PHASE");
    CHECK(modbind_withdraw("c0_0") == MB_MAX_MODULES, "withdrawing the root drops all 192");
    CHECK(modbind_hold_reason(reg("m191")) == MB_HOLD_WITHDRAWN, "the end waits on S-");
    CHECK(modbind_restore("c0_0") == MB_MAX_MODULES, "restoring it brings all 192 back");
    CHECK(modbind_withdraw("a_capability_name_longer_than_24") == 0,
          "a long name withdraws nothing");
    const char *const one[] = {"m1"};
    mb_module_t out;
    CHECK(modbind_compose("composite_name_longer_than_24", one, 1, &out) && out.n_provides == 4 &&
              out.n_requires == 4 && out.n_emits == 0,
          "a one-part composite: its emits are its own ingests");
    CHECK(memcmp(out.name, "composite_name_longer_th", MB_NAME_LEN) == 0,
          "a long composite name is clipped to MB_NAME_LEN");

    /* a small graph over the leftovers */
    modbind_reset();
    mb_module_t solo = blank("solo"), e4 = blank("e4"), hungry = blank("hungry");
    add_req(&solo, cap("c1_0", 1, 0, MB_AMP_FULL)); /* left behind by m1 */
    for (uint32_t k = 0; k < MB_MAX_PORTS; k++) {
        add_emit(&e4, FOUR_FORMS[k], 1);
        add_ingest(&hungry, FOUR_FORMS[k], 9);
    }
    modbind_register(&solo);
    modbind_register(&e4);
    modbind_register(&hungry);
    CHECK(modbind_resolve() == 2 && reg("solo")->ready == MB_HELD,
          "a provider left behind by reset provides nothing");
    CHECK(modbind_hold_reason(reg("solo")) == MB_HOLD_UNPROVIDED, "and names nothing");
    CHECK(modbind_verify_graph(0, 0) == 1, "one unprovided requirement");
    CHECK(modbind_withdraw("c1_0") == 0 && modbind_restore("c1_0") == 0,
          "nothing left behind is withdrawn or restored");
    CHECK(modbind_find("m3") == 0, "nor found");
    CHECK(modbind_check(reg("hungry")) == MB_ERR_VERSION,
          "every form is there at version 1, not 9");
    CHECK(!modbind_can_bind(reg("e4"), reg("hungry"), MB_FORM_BINARY), "v1 does not bind to v9");
    const char *const two[] = {"e4", "hungry"};
    CHECK(modbind_compose("pair", two, 2, &out) && out.n_emits == 4,
          "no emit of e4 is ingested inside the pair");

    /* a declaration that counts one port too many is refused, even when the
     * byte after the last port reads as a valid form */
    mb_module_t over = blank("over");
    for (uint32_t k = 0; k < MB_MAX_PORTS; k++) add_emit(&over, FOUR_FORMS[k], 1);
    over.n_emits = MB_MAX_PORTS + 1;
    CHECK(!modbind_register(&over), "n_emits past MB_MAX_PORTS is refused");
    over = blank("over");
    for (uint32_t k = 0; k < MB_MAX_PORTS; k++) add_ingest(&over, FOUR_FORMS[k], 1);
    over.n_ingests = MB_MAX_PORTS + 1;
    CHECK(!modbind_register(&over), "n_ingests past MB_MAX_PORTS is refused");

    /* compose writes every byte of the composite's name */
    dirty_stack(0x5A);
    mb_module_t dirty;
    memset(&dirty, 0x5A, sizeof dirty);
    CHECK(modbind_compose("", one + 0, 0, &dirty) == false, "no parts: refused");
    const char *const e4only[] = {"e4"};
    CHECK(modbind_compose("", e4only, 1, &dirty), "an unnamed composite");
    int zero = 1;
    for (uint32_t i = 0; i < MB_NAME_LEN; i++) zero = zero && dirty.name[i] == 0;
    CHECK(zero, "an empty composite name is all zero");
}

/* Slots past every count hold real-looking capabilities: the registry slot
 * just past g_n (left by an earlier, larger registration) and the array slots
 * just past n_provides / n_requires (a declaration filled in past its count).
 * Every walk stops at the count, so none of them is seen. */
static void test_leftover_slots(void)
{
    char nm[MB_NAME_LEN];
    mb_carrier_up();
    modbind_reset();
    for (uint32_t i = 0; i < 8; i++) {
        snprintf(nm, sizeof nm, "s%u", i);
        mb_module_t m = blank(nm);
        if (i == 7) {
            add_prov(&m, cap("lc", 1, 0, MB_AMP_FULL));
            add_prov(&m, cap("lp", 1, 0, MB_AMP_FULL));
            add_prov(&m, cap("lq", 1, 0, MB_AMP_FULL));
            add_prov(&m, cap("lz", 1, 0, MB_AMP_FULL));
        }
        CHECK(modbind_register(&m), "the first registration");
    }
    CHECK(modbind_resolve() == 8 && reg("s7")->ready == MB_READY, "s7 is ready and provides lc");

    modbind_reset(); /* slot 7 still holds s7, ready, providing all four */
    mb_module_t x[7];
    for (uint32_t i = 0; i < 7; i++) {
        snprintf(nm, sizeof nm, "x%u", i);
        x[i] = blank(nm);
    }
    add_req(&x[0], cap("lp", 1, 0, MB_AMP_FULL));
    x[0].
        requires[
                1] = cap("lz", 1, 0, MB_AMP_FULL);   /* past n_requires = 1 */
    x[0].provides[0] = cap("lp", 2, 0, MB_AMP_FULL); /* past n_provides = 0: would clash */
    add_prov(&x[1], cap("lp", 1, 0, MB_AMP_NONE));
    x[1].provides[1] = cap("lq", 1, 0, MB_AMP_FULL); /* past n_provides = 1 */
    add_req(&x[2], cap("lc", 1, 0, MB_AMP_FULL));
    x[2].provides[0] = cap("lp", 1, 0, MB_AMP_FULL); /* past n_provides = 0: would couple lp */
    add_req(&x[3], cap("lq", 1, 0, MB_AMP_FULL));
    x[3].provides[0] = cap("lp", 2, 0, MB_AMP_FULL); /* past n_provides = 0: would clash */
    x[4].provides[0] = cap("lw", 1, 0, MB_AMP_FULL); /* ready, and past n_provides = 0 */
    add_req(&x[5], cap("lw", 1, 0, MB_AMP_FULL));
    add_prov(&x[6], cap("lw", 1, 0, MB_AMP_FULL));
    add_req(&x[6], cap("none_such", 1, 0, MB_AMP_FULL));
    for (uint32_t i = 0; i < 7; i++)
        CHECK(modbind_register(&x[i]), "registered over the leftovers");
    CHECK(modbind_count() == 7, "seven, so slot 7 is the leftover");
    CHECK(modbind_resolve() == 2 && reg("x1")->ready == MB_READY && reg("x4")->ready == MB_READY,
          "only x1 and x4 come up");
    CHECK(modbind_hold_reason(reg("x0")) == MB_HOLD_PHASE,
          "x0: lp is named by x1 and couples nowhere");
    CHECK(modbind_hold_reason(reg("x2")) == MB_HOLD_UNPROVIDED, "x2: lc only in the slot past g_n");
    CHECK(modbind_hold_reason(reg("x3")) == MB_HOLD_UNPROVIDED,
          "x3: lq only in a slot past n_provides");
    CHECK(modbind_hold_reason(reg("x6")) == MB_HOLD_UNPROVIDED, "x6: none_such");
    CHECK(modbind_hold_reason(reg("x5")) == MB_HOLD_UNPROVIDED,
          "x5 inherits x6's reason: x4's lw is past its count");
    mb_err_t err = MB_OK;
    const char *who = 0;
    CHECK(modbind_verify_graph(&err, &who) == 3 && err == MB_ERR_UNPROVIDED && who &&
              strcmp(who, "x2") == 0,
          "three unprovided, the first at x2, and no contract clash");
    CHECK(modbind_withdraw("lc") == 0 && modbind_withdraw("lq") == 0 &&
              modbind_withdraw("lz") == 0 && modbind_withdraw("lw") == 0,
          "withdrawing a leftover takes nothing ready down");
    CHECK(reg("x1")->ready == MB_READY && reg("x4")->ready == MB_READY, "x1 and x4 untouched");
    CHECK(modbind_find("s7") == 0, "the slot past g_n is not found");
    CHECK(modbind_withdraw("lp") == 1 && reg("x1")->ready == MB_WITHDRAWN,
          "withdrawing lp takes x1 down");
    CHECK(modbind_hold_reason(reg("x0")) == MB_HOLD_PHASE, "x0 still waits on phase, not S-");
    CHECK(modbind_restore("lq") == 0 && reg("x1")->ready == MB_WITHDRAWN,
          "lq is past x1's count: restoring it leaves x1 down");
    CHECK(modbind_restore("lp") == 1 && reg("x1")->ready == MB_READY,
          "restoring lp brings x1 back");

    /* withdrawing the empty name matches nothing, whatever is on the stack */
    modbind_reset();
    mb_module_t zp = blank("zp");
    add_prov(&zp, cap("Z", 1, 0, MB_AMP_FULL));
    CHECK(modbind_register(&zp) && modbind_resolve() == 1, "zp provides Z");
    dirty_stack(0x5A);
    CHECK(modbind_withdraw("") == 0 && reg("zp")->ready == MB_READY, "\"\" is not \"Z\"");

    /* a name is compared up to its NUL; what follows it is not the name */
    modbind_reset();
    mb_module_t pv = blank("pv"), rq = blank("rq");
    mb_cap_t padded = cap("pad", 1, 0, MB_AMP_FULL);
    memset(padded.name + 4, 'Z', MB_CAP_NAME_LEN - 4);
    add_prov(&pv, padded);
    add_req(&rq, cap("pad", 1, 0, MB_AMP_FULL));
    CHECK(modbind_register(&pv) && modbind_register(&rq), "registered");
    CHECK(modbind_resolve() == 2, "\"pad\" then garbage provides \"pad\"");
    CHECK(modbind_verify_graph(0, 0) == 0, "and the graph is sound");
}

/* ---------------------------------------------------- the boot gate */

#if defined(__ELF__) && defined(__x86_64__) || defined(__ELF__) && defined(__aarch64__)
#    define GATE_TEST 1
#    define NDECL     6
static mb_module_t g_dm[NDECL];
static zxv_decl_t g_decl[NDECL];
const zxv_decl_t *const zxv_test_decls[NDECL] = {&g_decl[0], &g_decl[1], &g_decl[2],
                                                 &g_decl[3], &g_decl[4], &g_decl[5]};
_Static_assert(sizeof(void *) == 8, "the table end below is 6 x 8 bytes");
__asm__(".globl __zxv_decl_start\n\t.set __zxv_decl_start, zxv_test_decls\n\t"
        ".globl __zxv_decl_end\n\t.set __zxv_decl_end, zxv_test_decls + 48\n");

static char out_buf[65536];
static size_t out_len;
static void cap_puts(const char *s)
{
    size_t n = strlen(s);
    if (out_len + n < sizeof out_buf) {
        memcpy(out_buf + out_len, s, n);
        out_len += n;
        out_buf[out_len] = 0;
    }
}
static int said(const char *s)
{
    return strstr(out_buf, s) != 0;
}

static int calls[NDECL];
static int rc_of[NDECL];
static int bu0(void)
{
    calls[0]++;
    return rc_of[0];
}
static int bu1(void)
{
    calls[1]++;
    return rc_of[1];
}
static int bu2(void)
{
    calls[2]++;
    return rc_of[2];
}
static int bu3(void)
{
    calls[3]++;
    return rc_of[3];
}
static int bu4(void)
{
    calls[4]++;
    return rc_of[4];
}
static int bu5(void)
{
    calls[5]++;
    return rc_of[5];
}
static zxv_bringup_fn const BU[NDECL] = {bu0, bu1, bu2, bu3, bu4, bu5};

/* base <- mid <- top; base <- hw (held on absent hardware); mid <- leaf;
 * base <- quiet (declared only). Bring-ups: base, mid, leaf run and succeed,
 * hw holds, top and quiet declare only. */
static void healthy(void)
{
    static const char *const nm[NDECL] = {"base", "mid", "top", "hw", "leaf", "quiet"};
    for (int i = 0; i < NDECL; i++) {
        g_dm[i] = blank(nm[i]);
        g_decl[i].mod = &g_dm[i];
        g_decl[i].bringup = BU[i];
        calls[i] = 0;
        rc_of[i] = 0;
    }
    add_prov(&g_dm[0], cap("base_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&g_dm[1], cap("base_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_prov(&g_dm[1], cap("mid_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&g_dm[2], cap("mid_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&g_dm[3], cap("base_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&g_dm[4], cap("mid_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_req(&g_dm[5], cap("base_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    g_decl[2].bringup = ZXV_NO_BRINGUP;
    g_decl[5].bringup = ZXV_NO_BRINGUP;
    rc_of[3] = MB_BRINGUP_HELD;
    out_len = 0;
    out_buf[0] = 0;
    modbind_reset();
    mb_carrier_up();
}

static void test_gate(void)
{
    CHECK(zxv_decl_count() == NDECL, "the decl table is the linked section");
    CHECK(zxv_decl_boot_gate(0) == 1, "no console: refuse");

    healthy();
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "healthy graph passes the gate");
    CHECK(said("declarations collected=6 registered=6 refused=0"), "gate counts the decls");
    CHECK(said("[OK] GATE modbind_verify_graph"), "gate prints the graph OK");
    CHECK(said("S+ ready=6  S0 held=0  of 6 declared"), "every module ready");
    CHECK(said("bring-up: ran=3 failed=0 held=1 declared-only=2"), "bring-up outcome counts");
    CHECK(said("S0 HELD (bring-up): 'hw'"), "the held bring-up is named");
    CHECK(said("[OK] every bring-up that RAN"), "bring-up OK line");
    CHECK(said("line down -> S+ ready=1 (the 1 modules that require nothing)"),
          "carrier down leaves only base");
    CHECK(said("[OK] carrier is load-bearing"), "carrier OK line");
    CHECK(said("withdraw test: capability 'base_cap'"), "withdraws the most-required cap");
    CHECK(said("S+ ready 6 -> 0, un-readied=6"), "withdrawing base_cap drops all six");
    CHECK(said("S+ ready 0 -> 6, regained=6"), "restore brings all six back");
    CHECK(said("[OK] S-: withdrawal"), "withdraw OK line");
    CHECK(!said("[FAIL]"), "no FAIL line on a healthy graph");
    CHECK(said("coupling: 5 declared edge(s) over 6 module(s); carrier up"), "edges counted");
    CHECK(calls[0] == 1 && calls[1] == 1 && calls[3] == 1 && calls[4] == 1,
          "each bring-up ran once");
    CHECK(calls[2] == 0 && calls[5] == 0, "declared-only modules ran nothing");
    CHECK(zxv_decl_held_name(0) && strcmp(zxv_decl_held_name(0), "hw") == 0, "held name 0");
    CHECK(zxv_decl_held_name(1) == 0 && zxv_decl_failed_name(0) == 0, "no more names");
    for (int i = 0; i < NDECL; i++)
        CHECK(modbind_find(g_dm[i].name)->ready == MB_READY, "gate leaves the graph ready");

    healthy();
    rc_of[1] = -1;
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "a failing bring-up fails the gate");
    CHECK(said("BRING-UP FAILED: 'mid'"), "the failing bring-up is named");
    CHECK(said("ran=2 failed=1 held=1 declared-only=2"), "failure counted");
    CHECK(!said("[OK] every bring-up"), "no bring-up OK line");
    CHECK(zxv_decl_failed_name(0) && strcmp(zxv_decl_failed_name(0), "mid") == 0, "failed name 0");

    healthy();
    add_req(&g_dm[4], cap("ghost", 1, 0, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "an unprovided requirement fails the gate");
    CHECK(said("[FAIL] GATE modbind_verify_graph"), "graph FAIL line");
    CHECK(said("UNPROVIDED: module 'leaf' requires 'ghost'"), "the gap is named");
    CHECK(said("[FAIL] bring-up REFUSED"), "bring-up refused");
    CHECK(calls[0] + calls[1] + calls[3] + calls[4] == 0, "no bring-up ran on a bad graph");
    CHECK(!said("[OK] GATE"), "no graph OK line");

    healthy();
    add_req(&g_dm[0], cap("top_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    add_prov(&g_dm[2], cap("top_cap", 1, MB_PHASE_INHERIT, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 6, "base->mid->top->base: every module in the cycle");
    CHECK(said("CYCLE: module 'base' never resolves"), "the cycle is named");
    CHECK(calls[0] == 0, "nothing in a cycle is brought up");

    healthy();
    add_prov(&g_dm[5], cap("base_cap", 2, MB_PHASE_INHERIT, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "two providers at two contracts fail the gate");
    CHECK(said("CONTRACT: 'base_cap' provided by 'base' at v1 and by 'quiet' at v2"),
          "the contract clash is named");

    healthy();
    add_prov(&g_dm[5], cap("base_cap", 305, MB_PHASE_INHERIT, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "a three-digit contract clash");
    CHECK(said("CONTRACT: 'base_cap' provided by 'base' at v1 and by 'quiet' at v305\n"),
          "multi-digit numbers print in order");

    /* the power readout, worked by hand from the COS13 table: provider and
     * requirer phases give the distance k, P = 1000 * PF(k) / 1000 at full drive.
     * base_cap@0 -> mid@3 (k=3, 560), hw@12 (k=12, 942), quiet@0 (k=0, 1000);
     * mid_cap@6 -> top@0 (k=6, 14), leaf@6 (k=0, 1000). */
    healthy();
    {
        static const uint8_t mph[NDECL] = {0, 3, 0, 12, 6, 0};
        for (int i = 0; i < NDECL; i++) g_dm[i].phase = mph[i];
        g_dm[0].provides[0].phase = 0;
        g_dm[1]
            .
            requires[
                    0]
            .phase = 3;
        g_dm[1].provides[0].phase = 6;
        g_dm[2]
            .
            requires[
                    0]
            .phase = 0;
        g_dm[3]
            .
            requires[
                    0]
            .phase = 12;
        g_dm[4]
            .
            requires[
                    0]
            .phase = 6;
        g_dm[5]
            .
            requires[
                    0]
            .phase = 0;
    }
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "a phased graph still passes (no edge is zero)");
    CHECK(said("coupling: 5 declared edge(s) over 6 module(s); carrier up\n"), "edges and modules");
    CHECK(
        said(
            "module phases (DERIVED from the DRC layer, none typed): L0=3 L3=1 L6=1 L12=1  -- 4 of "
            "13 phases populated\n"),
        "module phase histogram");
    CHECK(said("         dphase  interf permille   edges\n"
               "              0            1000      2\n"
               "              3            560      1\n"
               "              6            14      1\n"
               "              12            942      1\n"),
          "edges per phase distance, with the factor");
    CHECK(said("weakest: 'top' requires 'mid_cap' <- 'mid'  L6->L0  P=14/1000\n"),
          "the weakest edge is named with its phases and power");
    CHECK(!said("more edge(s) at the same floor"), "one weakest edge: nothing unnamed");
    CHECK(said("           cut at P>14 would drop 1 of 5 edge(s)\n"
               "           cut at P>560 would drop 2 of 5 edge(s)\n"
               "           cut at P>942 would drop 3 of 5 edge(s)\n"
               "           cut at P>1000 would drop 5 of 5 edge(s)\n"),
          "the threshold sweep, cumulative and sorted");
    CHECK(!said("[FAIL]"), "no FAIL line");

    healthy();
    set_name(g_dm[5].name, MB_NAME_LEN, "base");
    CHECK(zxv_decl_boot_gate(cap_puts) >= 1, "a duplicate declaration fails the gate");
    CHECK(said("registered=5 refused=1"), "one refused");
    CHECK(said("[FAIL] modbind REFUSED"), "refusal FAIL line");

    healthy();
    g_dm[0].provides[0].amplitude = MB_AMP_NONE;
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "a zero-drive provider is S0, not a defect");
    CHECK(said("S+ ready=1  S0 held=5"), "only base is ready");
    CHECK(said("S0 HELD: 'mid' -- OUT OF PHASE"), "the open circuit is named");
    CHECK(said("weakest: "), "the weakest edge is named");
    CHECK(said("an edge reached EXACTLY ZERO real power"), "zero power is flagged");
}

static int count_said(const char *s)
{
    int n = 0;
    for (const char *p = out_buf; (p = strstr(p, s)) != 0; p += strlen(s)) n++;
    return n;
}

/* healthy()'s six declarations with every capability cleared */
static void bare(void)
{
    healthy();
    for (int i = 0; i < NDECL; i++) {
        char nm[MB_NAME_LEN + 1];
        memcpy(nm, g_dm[i].name, MB_NAME_LEN);
        nm[MB_NAME_LEN] = 0;
        g_dm[i] = blank(nm);
    }
}

/* The gate's printed report, line for line, over graphs built so that each
 * line has exactly one right answer (worked by hand from modbind.h). */
static void test_gate_readout(void)
{
    /* a broken graph: mid requires ghost (UNPROVIDED), top <-> hw (CYCLE),
     * leaf requires a zero-drive capability (OUT OF PHASE), base and quiet
     * provide cap_b at two contracts (CONTRACT) */
    bare();
    add_prov(&g_dm[0], cap("cap_a", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[0], cap("cap_b", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[1], cap("ghost", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[1], cap("cap_a", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[2], cap("cap_t", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[2], cap("cap_c", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[2], cap("cap_a", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[3], cap("cap_c", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[3], cap("cap_t", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[4], cap("dead", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[4], cap("cap_b", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[5], cap("dead", 1, 0, MB_AMP_NONE));
    add_prov(&g_dm[5], cap("cap_b", 2, 0, MB_AMP_FULL));
    /* 1 unprovided + 1 contract + 2 cycle */
    CHECK(zxv_decl_boot_gate(cap_puts) == 4, "broken graph: four problems");
    CHECK(said("  coupling: 7 declared edge(s) over 6 module(s); carrier up\n"),
          "7 edges: mid-cap_a, top-cap_c, top-cap_a, hw-cap_t, leaf-dead, leaf-cap_b x2");
    CHECK(said("module phases (DERIVED from the DRC layer, none typed): L0=6  -- 1 of 13 phases "
               "populated\n"),
          "all six modules at L0");
    CHECK(said("         dphase  interf permille   edges\n"
               "              0            1000      7\n         weakest"),
          "every edge in phase");
    CHECK(said("         weakest: 'leaf' requires 'dead' <- 'quiet'  L0->L0  P=0/1000\n"
               "         weakest: 'leaf' requires 'cap_b' <- 'quiet'  L0->L0  P=0/1000\n"
               "         threshold sweep"),
          "the two zero-power edges, named, in order");
    CHECK(count_said("weakest: '") == 2, "and no other edge is named weakest");
    CHECK(said("would cost:\n           cut at P>0 would drop 2 of 7 edge(s)\n"
               "           cut at P>1000 would drop 7 of 7 edge(s)\n"
               "  [FAIL] an edge reached EXACTLY ZERO"),
          "sweep over the two powers present");
    CHECK(said("  [FAIL] GATE modbind_verify_graph: the graph is NOT sound\n"
               "         problems=4  first=UNPROVIDED"),
          "four problems, the first unprovided");
    CHECK(said("         UNPROVIDED: module 'mid' requires 'ghost' -- nothing provides it\n"),
          "the gap is named");
    CHECK(count_said("UNPROVIDED: module '") == 1, "provided requirements are not reported");
    CHECK(said("         CYCLE: module 'top' never resolves; its requirements close on themselves "
               "(cap_c, cap_a)\n"),
          "top's cycle, every requirement listed");
    CHECK(said("         CYCLE: module 'hw' never resolves; its requirements close on themselves "
               "(cap_t)\n"),
          "hw's cycle");
    CHECK(count_said("CYCLE: module '") == 2, "only the two in the cycle");
    CHECK(said("         S0 OUT OF PHASE: a provider exists, delivers no real power: module 'leaf' "
               "is present and not coupled\n"),
          "the out-of-phase module is named");
    CHECK(count_said("is present and not coupled") == 1, "an UNPROVIDED module is not S0");
    CHECK(said("         CONTRACT: 'cap_b' provided by 'base' at v1 and by 'quiet' at v2\n"),
          "the clash at provides[1]");
    CHECK(count_said("CONTRACT: '") == 1, "different capabilities never clash");

    /* a sound phased graph: base requires q_cap from quiet at low drive and
     * antiphase (P = -250 * 14 / 1000 = -3); mid_cap at phase 2, top at 3
     * (k = 1, 942), leaf at 2 (k = 0); powers appear in the order 3, 1000, 942 */
    bare();
    add_req(&g_dm[0], cap("q_cap", 1, 6, MB_AMP_FULL));
    add_prov(&g_dm[0], cap("base_cap", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[1], cap("base_cap", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[1], cap("mid_cap", 1, 2, MB_AMP_FULL));
    add_req(&g_dm[2], cap("mid_cap", 1, 3, MB_AMP_FULL));
    add_req(&g_dm[3], cap("base_cap", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[4], cap("mid_cap", 1, 2, MB_AMP_FULL));
    add_prov(&g_dm[5], cap("q_cap", 1, 0, MB_AMP_LOW));
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "phased graph passes");
    CHECK(said("         dphase  interf permille   edges\n"
               "              0            1000      3\n"
               "              1            942      1\n"
               "              6            14      1\n"),
          "distances 0, 1 and 6");
    CHECK(said("         weakest: 'base' requires 'q_cap' <- 'quiet'  L0->L6  P=-3/1000\n"),
          "a negative power is printed with its sign");
    CHECK(count_said("weakest: '") == 1, "one weakest edge");
    CHECK(said("would cost:\n           cut at P>3 would drop 1 of 5 edge(s)\n"
               "           cut at P>942 would drop 2 of 5 edge(s)\n"
               "           cut at P>1000 would drop 5 of 5 edge(s)\n  [OK]"),
          "the sweep, sorted, nothing before or after it");

    /* ten edges at the same power: eight named, two counted */
    bare();
    add_prov(&g_dm[0], cap("e0", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[0], cap("e1", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[1], cap("e0", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[1], cap("e1", 1, 0, MB_AMP_FULL));
    for (int i = 2; i < NDECL; i++) add_req(&g_dm[i], cap("e0", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[5], cap("e1", 1, 0, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "alternative providers pass");
    CHECK(said("  coupling: 10 declared edge(s)"), "ten edges");
    CHECK(count_said("weakest: '") == 8, "eight named");
    CHECK(said("         (+2 more edge(s) at the same floor, not named)\n"), "two counted");

    /* module 0 held, and ready modules not listed as held */
    bare();
    add_req(&g_dm[0], cap("deadc", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[0], cap("base_cap", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[5], cap("deadc", 1, 0, MB_AMP_NONE));
    add_req(&g_dm[1], cap("base_cap", 1, 0, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "an out-of-phase module 0 is S0, not a defect");
    CHECK(said("S+ ready=4  S0 held=2  of 6 declared\n"
               "         S0 HELD: 'base' -- OUT OF PHASE: a provider exists, delivers no real "
               "power\n"
               "         S0 HELD: 'mid' -- OUT OF PHASE"),
          "base and mid held, named in order");
    CHECK(count_said("S0 HELD: '") == 2 && !said("-- not held"), "ready modules are not listed");

    /* two failures named; two held bring-ups named; nothing stale afterwards */
    healthy();
    rc_of[3] = -1;
    rc_of[4] = -1;
    CHECK(zxv_decl_boot_gate(cap_puts) == 2, "two failed bring-ups");
    CHECK(said("         BRING-UP FAILED: 'hw' -- its own check returned non-zero\n"
               "         BRING-UP FAILED: 'leaf' -- its own check returned non-zero\n"),
          "both failures named");
    CHECK(!said("further failures"), "all failures fit");
    healthy();
    rc_of[4] = MB_BRINGUP_HELD;
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "two held bring-ups pass");
    CHECK(said("S0 HELD (bring-up): 'hw'") && said("S0 HELD (bring-up): 'leaf'") &&
              count_said("S0 HELD (bring-up)") == 2,
          "both held bring-ups named, nothing more");
    healthy();
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "healthy again");
    CHECK(count_said("S0 HELD (bring-up)") == 1, "one held bring-up");
    CHECK(zxv_decl_held_name(0) && strcmp(zxv_decl_held_name(0), "hw") == 0 &&
              zxv_decl_held_name(1) == 0 && zxv_decl_failed_name(0) == 0,
          "names from an earlier run do not survive the next");
    CHECK(said("         S- withdraw test: capability 'base_cap' chosen from the live graph -- 3 "
               "module(s) require it\n"),
          "base_cap: mid, hw and quiet require it");

    /* the gate counts what it refuses. A refused declaration is counted
     * twice: once as refused, once as a bring-up that failed because it is not
     * in the registry (AUDIT_FINDINGS section 3). */
    healthy();
    g_dm[5].n_emits = MB_MAX_PORTS + 1;
    CHECK(zxv_decl_boot_gate(cap_puts) == 2, "one refused declaration: refused + not registered");
    CHECK(said("registered=5 refused=1\n") && said("BRING-UP FAILED: 'quiet'"),
          "both counts are printed");
    /* two refused: both named, in order, and neither one's bring-up runs.
     * An earlier run left 'leaf' in the second failed-name slot. */
    healthy();
    g_dm[3].n_emits = MB_MAX_PORTS + 1;
    g_dm[5].n_emits = MB_MAX_PORTS + 1;
    CHECK(zxv_decl_boot_gate(cap_puts) == 4,
          "two refused declarations: 2 refused + 2 not registered");
    CHECK(said("         bring-up: ran=3 failed=2 held=0 declared-only=1\n"),
          "the bring-up counts");
    CHECK(said("         BRING-UP FAILED: 'hw' -- its own check returned non-zero\n"
               "         BRING-UP FAILED: 'quiet' -- its own check returned non-zero\n") &&
              count_said("BRING-UP FAILED: '") == 2 && !said("further failures"),
          "both refused declarations named, nothing stale");
    CHECK(calls[3] == 0, "a refused declaration's bring-up does not run");

    /* entered with the carrier down: the negative tests see it */
    healthy();
    mb_carrier_down();
    CHECK(zxv_decl_boot_gate(cap_puts) == 2, "carrier down at entry: two negative-test failures");
    CHECK(said("  [FAIL] carrier up did not restore the graph it took down\n"),
          "the carrier test fails");
    CHECK(said("  [FAIL] restore did not return the graph to its entry state\n"),
          "the withdraw test fails");
    CHECK(said("         bring-up: ran=1 failed=0 held=5 declared-only=0\n"),
          "only base is ready; the other five are held");
    CHECK(calls[0] == 1 && calls[1] == 0 && calls[3] == 0 && calls[4] == 0,
          "a held module's bring-up does not run");
    CHECK(mb_carrier(), "the gate leaves the carrier up");
}

/* More of the gate's report, each graph built for one formatter branch. */
static void test_gate_readout_edges(void)
{
    /* every module at its capability bound, passing and then failing */
    bare();
    for (uint32_t k = 0; k < MB_MAX_CAPS; k++) {
        char p[4] = {'p', (char) ('0' + k), 0}, q[4] = {'q', (char) ('0' + k), 0};
        add_prov(&g_dm[0], cap(p, 1, 0, MB_AMP_FULL));
        add_prov(&g_dm[1], cap(q, 1, 0, MB_AMP_FULL));
        add_prov(&g_dm[2], cap(q, 1, 0, MB_AMP_FULL)); /* alternative, same contract */
    }
    /* each requirer lists p0..p3 rotated by its index, so p0 is not always first */
    for (int i = 1; i < NDECL; i++)
        for (uint32_t k = 0; k < MB_MAX_CAPS; k++) {
            char p[4] = {'p', (char) ('0' + (k + (uint32_t) i) % MB_MAX_CAPS), 0};
            add_req(&g_dm[i], cap(p, 1, 0, MB_AMP_FULL));
        }
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "four capabilities each: passes");
    CHECK(said("  coupling: 20 declared edge(s)"), "five requirers x four capabilities");
    CHECK(said("chosen from the live graph -- 5 module(s) require it\n"), "five requirers of p0");
    set_name(g_dm[5].requires[3].name, MB_CAP_NAME_LEN, "ghost");
    modbind_reset(); /* the gate registers into the live registry */
    out_len = 0;
    out_buf[0] = 0;
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "one unprovided requirement");
    CHECK(said("         UNPROVIDED: module 'quiet' requires 'ghost' -- nothing provides it\n") &&
              count_said("UNPROVIDED: module '") == 1,
          "named, once");
    CHECK(count_said("CONTRACT: '") == 0, "alternative providers at one contract do not clash");

    /* bytes after a capability name's NUL are not part of the name */
    bare();
    mb_cap_t padded = cap("pad", 1, 0, MB_AMP_FULL);
    memset(padded.name + 4, 'Z', MB_CAP_NAME_LEN - 4);
    add_prov(&g_dm[3], padded);
    add_req(&g_dm[0], cap("pad", 1, 0, MB_AMP_FULL)); /* module 0 is a requirer too */
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "a padded name provides its prefix");
    CHECK(said("         S- withdraw test: capability 'pad' chosen from the live graph -- 1 "
               "module(s) require it\n"),
          "the requirer of the padded name is counted");

    /* names that differ only in their first letter are different */
    bare();
    add_prov(&g_dm[0], cap("acap", 1, 0, MB_AMP_FULL));
    add_req(&g_dm[2], cap("bcap", 1, 0, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "bcap is unprovided");
    CHECK(said("  coupling: 0 declared edge(s) over 6 module(s); carrier up\n"), "no edge");
    CHECK(said("populated\n         no requirer names a declared capability: there is no edge to "
               "measure, and that is the measurement\n  [FAIL] GATE"),
          "and nothing else is measured");
    CHECK(said("UNPROVIDED: module 'top' requires 'bcap'"), "the gap is named");

    /* full-length names, module and capability: one edge, open by contract */
    bare();
    mb_cap_t full = cap("", 1, 0, MB_AMP_FULL);
    memset(full.name, 'k', MB_CAP_NAME_LEN);
    add_prov(&g_dm[0], full);
    full.contract = 2;
    add_req(&g_dm[1], full);
    memset(g_dm[1].name, 'm', MB_NAME_LEN);
    add_emit(&g_dm[1], MB_FORM_PHASE, 1); /* the byte after the name is a form */
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "an open circuit is S0, not a defect");
    CHECK(said("  coupling: 1 declared edge(s) over 6 module(s)"), "one edge by name");
    CHECK(
        said("         weakest: 'mmmmmmmmmmmmmmmmmmmmmmmm' requires 'kkkkkkkkkkkkkkkkkkkkkkkk' <- "
             "'base'  L0->L0  P=0/1000\n"),
        "full-length names print whole and stop at MB_NAME_LEN");

    /* a clash between neighbours */
    bare();
    add_prov(&g_dm[0], cap("x", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[1], cap("x", 2, 0, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "adjacent providers disagree");
    CHECK(said("         CONTRACT: 'x' provided by 'base' at v1 and by 'mid' at v2\n"),
          "the clash between modules 0 and 1 is named");

    /* fourteen distinct powers: FULL and HIGH drive at distances 0..6 */
    bare();
    add_prov(&g_dm[0], cap("a0", 1, 0, MB_AMP_FULL));
    add_prov(&g_dm[0], cap("a1", 1, 0, MB_AMP_HIGH));
    static const struct {
        int mod;
        const char *c;
        uint8_t ph;
    } E[14] = {{1, "a0", 0}, {1, "a0", 1}, {1, "a0", 2}, {1, "a0", 3}, {2, "a0", 4},
               {2, "a0", 5}, {2, "a0", 6}, {2, "a1", 0}, {3, "a1", 1}, {3, "a1", 2},
               {3, "a1", 3}, {3, "a1", 4}, {4, "a1", 5}, {4, "a1", 6}};
    for (int i = 0; i < 14; i++) add_req(&g_dm[E[i].mod], cap(E[i].c, 1, E[i].ph, MB_AMP_FULL));
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "fourteen powers pass");
    CHECK(said("  coupling: 14 declared edge(s)"), "fourteen edges");
    CHECK(count_said("           cut at P>") == 14, "a cut for every distinct power");
    CHECK(said("           cut at P>10 would drop 1 of 14 edge(s)\n"),
          "HIGH drive at antiphase: 750 * 14 / 1000 = 10");
    CHECK(said("           cut at P>1000 would drop 14 of 14 edge(s)\n"),
          "the last cut drops every edge");

    /* a declaration with no module, and register_all without an out-count */
    healthy();
    g_decl[5].mod = 0;
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "a declaration with no module is one problem");
    CHECK(said("collected=6 registered=5 refused=1\n"), "it is refused");
    healthy();
    CHECK(zxv_decl_register_all(0) == NDECL, "register_all without an out-count");

    /* the readout starts from zero, whatever the stack held */
    healthy();
    {
        static const uint8_t mph[NDECL] = {0, 3, 0, 12, 6, 0};
        for (int i = 0; i < NDECL; i++) g_dm[i].phase = mph[i];
    }
    dirty_stack(0x5A);
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "healthy over a dirty stack");
    CHECK(
        said(
            "module phases (DERIVED from the DRC layer, none typed): L0=3 L3=1 L6=1 L12=1  -- 4 of "
            "13 phases populated\n"),
        "phase histogram from zero");
    CHECK(said("         dphase  interf permille   edges\n"
               "              0            1000      1\n"
               "              3            560      3\n"
               "              12            942      1\n         weakest"),
          "inherited phases: base_cap@0 to mid@3, hw@12, quiet@0; mid_cap@3 to top@0, leaf@6");
}
#endif

int main(void)
{
    printf("test_modbind: module binding and the declaration gate\n");
    test_power_tables();
    test_register();
    test_ports_random();
    test_resolve_random();
    test_hold_reasons();
    test_compose();
    test_selfcheck();
    test_register_edges();
    test_hold_edges();
    test_verify_graph_edges();
    test_compose_edges();
    test_full_registry();
    test_leftover_slots();
#ifdef GATE_TEST
    test_gate();
    test_gate_readout();
    test_gate_readout_edges();
#else
    printf("  [SKIP] boot gate (needs an ELF x86_64 or aarch64 host for the decl table)\n");
#endif
    printf("%u checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_modbind\n");
        return 1;
    }
    printf("[PASS] test_modbind\n");
    return 0;
}
