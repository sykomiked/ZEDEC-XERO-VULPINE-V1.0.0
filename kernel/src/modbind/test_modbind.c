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
#ifdef GATE_TEST
    test_gate();
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
