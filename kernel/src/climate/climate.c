/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate.c — thermal modes and the climate shell controller. See climate.h
 * for the sources, units and the order of a control step. */
#include "climate.h"

/* ---- profiles ------------------------------------------------------------
 * Figures are the specs'. A value the specs leave out or mark [CONFIRM] is
 * marked [default] (taken here) or [CONFIRM] (spec figure, unconfirmed). */

#define PREF_ALL ((1u << CL_PREF_QUIET) | (1u << CL_PREF_BALANCED) | (1u << CL_PREF_PERFORMANCE))
#define PREF_BAL (1u << CL_PREF_BALANCED)

/* Briefcase scram triggers. PLATFORM_COMMON s6.3: any die > 88 C, exhaust
 * minus intake > 45 K, ambient > 48 C. OEM_CONSUMER s4 sets the Pioneer's
 * die trigger at 95 C and makes a micro-jet fault a scram cause; the
 * product document wins for the Pioneer. */
#define BC_DT_MC      CL_C(45)
#define BC_AMBIENT_MC CL_C(48)
/* Hot-ambient compute cap (PLATFORM_COMMON s6.3: "measured during
 * MIL-STD-810H Method 501.7 testing", not yet measured): 50 % of the rail
 * peak, applied in Scram [default]. */
#define BC_HOT_PERMILLE 500u
/* Silent ceiling for the institutional tiers: PLATFORM_COMMON s6.3 says
 * "Vanguard idle", measured on the prototype; 40 W [default]. */
#define INST_SILENT_MW CL_W(40)

/* Server shell (SERVER_COMMON s1, s2.2): free cooling below about +15 C;
 * supply air 18 to 27 C; dew point up to +15 C; RH at most 60 %; outside
 * -40 to +50 C. */
#define SV_FREE_MAX_MC CL_C(15)
#define SV_SUPPLY_MAX  CL_C(27)
#define SV_DP_MAX      CL_C(15)
#define SV_RH_MAX      CL_RH(60)
#define SV_OUT_MAX     CL_C(50)
#define SV_OUT_MIN     CL_C(-40)

/* Handheld (HARDENING s5): charging 0 to +45 C cell temperature; throttle
 * below about -20 C, by an amount the spec does not give: 50 % [default]. */
#define HH_CHG_MIN CL_C(0)
#define HH_CHG_MAX CL_C(45)
#define HH_COLD    CL_C(-20)
#define HH_COLD_PM 500u

#define BRIEFCASE(slug_, name_, sus, pk, cpu, neu, oth, prefs_, q, b, p, silent, die, jet)         \
    {                                                                                              \
        .slug = slug_, .name = name_, .cls = CL_CLASS_BRIEFCASE, .sustained_mw = CL_W(sus),        \
        .peak_mw = CL_W(pk), .rail_peak_mw = {CL_W(cpu), CL_W(neu), CL_W(90), 0},                  \
        .other_mw = CL_W(oth), .prefs = prefs_, .pref_cap_mw = {CL_W(q), CL_W(b), CL_W(p)},        \
        .silent_ceiling_mw = silent, .scram_die_mc = CL_C(die), .scram_dt_mc = BC_DT_MC,           \
        .scram_ambient_mc = BC_AMBIENT_MC, .jet_fault_scrams = jet,                                \
        .hot_permille = BC_HOT_PERMILLE                                                            \
    }

#define SERVER(slug_, name_, sus, pk, shells_, heater, comp, hdd)                                  \
    {                                                                                              \
        .slug = slug_, .name = name_, .cls = CL_CLASS_SERVER, .sustained_mw = CL_W(sus),           \
        .peak_mw = CL_W(pk), .rail_peak_mw = {0, 0, 0, CL_W(pk)}, .shells = shells_,               \
        .heater_mw = CL_W(heater), .compressors = comp, .hdd_fitted = hdd,                         \
        .free_cool_max_mc = SV_FREE_MAX_MC, .supply_max_mc = SV_SUPPLY_MAX,                        \
        .dp_max_mc = SV_DP_MAX, .rh_max_x100 = SV_RH_MAX, .outside_max_mc = SV_OUT_MAX,            \
        .outside_min_mc = SV_OUT_MIN                                                               \
    }

#define HANDHELD(slug_, name_, dev, proc, opmax)                                                   \
    {                                                                                              \
        .slug = slug_, .name = name_, .cls = CL_CLASS_HANDHELD, .sustained_mw = dev,               \
        .peak_mw = dev, .rail_peak_mw = {proc, 0, 0, 0}, .op_min_mc = CL_C(-30),                   \
        .op_max_mc = CL_C(opmax), .charge_min_mc = HH_CHG_MIN, .charge_max_mc = HH_CHG_MAX,        \
        .cold_throttle_mc = HH_COLD, .cold_permille = HH_COLD_PM                                   \
    }

static const cl_profile_t g_profiles[] = {
    /* Pioneer (OEM_CONSUMER s2, s4; PLATFORM_COMMON s6.2, s8.2): Quiet
     * <= 45 W, Balanced <= 85 W, Performance <= 120 W; 150 W sustained
     * heat; 230 W internal supply as the peak. OEM_CONSUMER s2 says "55 W
     * quiet" while its s4 table says Quiet <= 45 W; the thermal-mode table
     * (s4) and PLATFORM_COMMON s6.3's 45 W silent target are used. The APU
     * includes the NPU, so there is no separate neural rail. Fixed loads are
     * not budgeted in the specs: 0 [default]. Projector rail: accessory,
     * <= 90 W (PLATFORM_COMMON s4). */
    BRIEFCASE("pioneer", "Pioneer", 150, 230, 120, 0, 0, PREF_ALL, 45, 85, 120, CL_W(45), 95, true),
    /* Institutional (OEM_INSTITUTIONAL s4 ratings, s5 peaks). Fixed loads
     * = DRAM + storage + arbiter + NICs + displays + audio/fans. Only the
     * automatic Silent/Normal pair (Balanced) is defined for these tiers. */
    BRIEFCASE("vanguard", "Vanguard", 250, 340, 155, 70, 30 + 20 + 10 + 15 + 35 + 25, PREF_BAL, 155,
              155, 155, INST_SILENT_MW, 88, false),
    BRIEFCASE("sovereign", "Sovereign", 330, 450, 155, 130, 40 + 25 + 20 + 30 + 35 + 30, PREF_BAL,
              155, 155, 155, INST_SILENT_MW, 88, false),
    BRIEFCASE("citadel", "Citadel", 500, 650, 200, 260, 70 + 50 + 45 + 35 + 35 + 45, PREF_BAL, 200,
              200, 200, INST_SILENT_MW, 88, false),
    /* Servers (SERVER_COMMON s2 shell sizes and heaters, s2.2 compressors,
     * s4 IT sustained / peak; SERVER_CIVILIAN / SERVER_INSTITUTIONAL drive
     * tiers: the civilian models carry Exos M hard drives, the institutional
     * models ship all-flash). The S pedestal has one compressor unit, M and
     * L two (N+1). Hoard: two L shells, IT figures for both racks. Every
     * server figure is [CONFIRM] in the spec. */
    SERVER("hearth", "Hearth", 260, 360, 1, 100, 1, true),
    SERVER("homestead", "Homestead", 450, 650, 1, 400, 2, true),
    SERVER("guildhall", "Guildhall", 800, 1050, 1, 800, 2, true),
    SERVER("strongbox", "Strongbox", 750, 1100, 1, 800, 2, false),
    SERVER("count_house", "Count House", 2600, 3700, 1, 800, 2, false),
    SERVER("hoard", "Hoard", 5500, 7300, 2, 800, 2, false),
    /* Handhelds (MOBILE_DEVICES s1-3; HARDENING s5): device sustained limit
     * and processor cap. Signet 6 W / 5 W, Folio 12 W / 10 W, Courier open
     * 15 W / 10 W, performance 45 W / 45 W; operating to +60 C (phone,
     * tablet) or +55 C (laptop). The RISC-V board has no figures: the open
     * board's [default]. */
    HANDHELD("signet", "Signet", 6000u, 5000u, 60),
    HANDHELD("folio", "Folio", 12000u, 10000u, 60),
    HANDHELD("courier_open", "Courier (open board)", 15000u, 10000u, 55),
    HANDHELD("courier_riscv", "Courier (RISC-V board)", 15000u, 10000u, 55),
    HANDHELD("courier_perf", "Courier (performance board)", 45000u, 45000u, 55),
};

#define N_PROFILES ((uint32_t) (sizeof g_profiles / sizeof g_profiles[0]))

uint32_t cl_profile_count(void)
{
    return N_PROFILES;
}

const cl_profile_t *cl_profile(uint32_t i)
{
    return i < N_PROFILES ? &g_profiles[i] : (const cl_profile_t *) 0;
}

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const cl_profile_t *cl_profile_find(const char *slug)
{
    if (!slug) return (const cl_profile_t *) 0;
    for (uint32_t i = 0; i < N_PROFILES; i++)
        if (str_eq(g_profiles[i].slug, slug)) return &g_profiles[i];
    return (const cl_profile_t *) 0;
}

const char *cl_mode_name(cl_mode_t m)
{
    switch (m) {
    case CL_MODE_QUIET:
        return "Quiet";
    case CL_MODE_BALANCED:
        return "Balanced";
    case CL_MODE_PERFORMANCE:
        return "Performance";
    case CL_MODE_SCRAM:
        return "Scram";
    case CL_MODE_LIMP_HOME:
        return "Limp home";
    case CL_MODE_MANUAL:
        return "Manual";
    case CL_MODE_WARMUP:
        return "Warm-up";
    case CL_MODE_FREE_COOLING:
        return "Free cooling";
    case CL_MODE_ACTIVE_COOLING:
        return "Active cooling";
    case CL_MODE_FACILITY_WATER:
        return "Facility water";
    case CL_MODE_SERVICE:
        return "Service";
    case CL_MODE_DEHUMIDIFY:
        return "Dehumidify";
    case CL_MODE_NORMAL:
        return "Normal";
    case CL_MODE_COLD_THROTTLE:
        return "Cold throttle";
    case CL_MODE_NONE:
    case CL_MODE__COUNT:
        break;
    }
    return "?";
}

/* ---- integer dew point -------------------------------------------------- */

#define Q30     30
#define ONE_Q30 ((int64_t) 1 << Q30)
#define LN2_Q30 744261118LL /* ln 2 * 2^30, rounded */

static int64_t div_round(int64_t n, int64_t d) /* d > 0 */
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

int64_t cl_ln_q30(uint32_t x)
{
    if (x == 0) return 0;
    uint32_t k = 0;
    while ((x >> k) > 1u) k++;
    /* m = x / 2^k in [1, 2), Q30 */
    int64_t m = (int64_t) (((uint64_t) x << Q30) >> k);
    /* ln m = 2 atanh(s), s = (m - 1) / (m + 1) in [0, 1/3) */
    int64_t s = ((m - ONE_Q30) << Q30) / (m + ONE_Q30);
    int64_t s2 = (s * s) >> Q30;
    int64_t term = s, sum = s;
    for (int64_t n = 3; n <= 41 && term != 0; n += 2) {
        term = (term * s2) >> Q30;
        sum += term / n;
    }
    return (int64_t) k * LN2_Q30 + 2 * sum;
}

#define MAG_B_X100 1762LL   /* b = 17.62 */
#define MAG_C_MC   243120LL /* c = 243.12 C */

bool cl_dewpoint_mc(int32_t t_mc, uint32_t rh_x100, int32_t *td_mc)
{
    if (!td_mc || t_mc < CL_C(-45) || t_mc > CL_C(60) || rh_x100 < 1u || rh_x100 > 10000u)
        return false;
    /* gamma = ln(RH) + b t / (c + t), Q30 */
    int64_t g = cl_ln_q30(rh_x100) - cl_ln_q30(10000u);
    g += div_round(MAG_B_X100 * (int64_t) t_mc * ONE_Q30, 100 * (MAG_C_MC + t_mc));
    int64_t b = div_round(MAG_B_X100 * ONE_Q30, 100);
    /* td = c gamma / (b - gamma); b - gamma > 0 over the accepted range */
    *td_mc = (int32_t) div_round(MAG_C_MC * g, b - g);
    return true;
}

/* ---- start interlock (SERVER_COMMON s2.3) ------------------------------- */

#define IL_HDD_MIN   CL_C(10) /* s2.3 (1): every hard drive >= 10 C (Exos M minimum) */
#define IL_SSD_MIN   CL_C(0)  /* s2.3 (1): all-flash shells, every SSD >= 0 C */
#define IL_DP_MARGIN CL_C(3)  /* s2.3 (2): every surface at least 3 K above the dew point */
#define IL_LFP_MIN   CL_C(0)  /* s2.3 (3), s4: LFP above 0 C before it is charged */

uint32_t cl_interlock(const cl_profile_t *p, const cl_input_t *in, int32_t *dp_mc)
{
    int32_t dp = CL_NO_READING;
    if (dp_mc) *dp_mc = CL_NO_READING;
    if (!p || !in) return CL_IL_BAD_INPUT;
    if (in->n_hdd > CL_MAX_SENSORS || in->n_ssd > CL_MAX_SENSORS || in->n_surface > CL_MAX_SENSORS)
        return CL_IL_BAD_INPUT;
    uint32_t f = 0;
    if (p->hdd_fitted && in->n_hdd == 0) f |= CL_IL_NO_HDD;
    for (uint32_t i = 0; i < in->n_hdd; i++) {
        if (in->hdd_mc[i] == CL_NO_READING)
            f |= CL_IL_NO_HDD;
        else if (in->hdd_mc[i] < IL_HDD_MIN)
            f |= CL_IL_HDD_COLD;
    }
    /* s2.3 states the SSD condition for all-flash shells; flash in a
     * hard-drive shell has the same 0 C floor (s1), so every SSD present is
     * checked, and an all-flash shell must report at least one. */
    if (!p->hdd_fitted && in->n_ssd == 0) f |= CL_IL_NO_SSD;
    for (uint32_t i = 0; i < in->n_ssd; i++) {
        if (in->ssd_mc[i] == CL_NO_READING)
            f |= CL_IL_NO_SSD;
        else if (in->ssd_mc[i] < IL_SSD_MIN)
            f |= CL_IL_SSD_COLD;
    }
    bool have_dp = in->internal_mc != CL_NO_READING &&
                   cl_dewpoint_mc(in->internal_mc, in->internal_rh_x100, &dp);
    if (!have_dp) {
        dp = CL_NO_READING;
        f |= CL_IL_NO_DEWPOINT;
    }
    if (in->n_surface == 0) f |= CL_IL_NO_SURFACE;
    for (uint32_t i = 0; i < in->n_surface; i++) {
        if (in->surface_mc[i] == CL_NO_READING)
            f |= CL_IL_NO_SURFACE;
        else if (have_dp && (int64_t) in->surface_mc[i] < (int64_t) dp + IL_DP_MARGIN)
            f |= CL_IL_CONDENSATION;
    }
    if (in->lfp_mc == CL_NO_READING)
        f |= CL_IL_NO_LFP;
    else if (in->lfp_mc <= IL_LFP_MIN)
        f |= CL_IL_LFP_COLD;
    if (dp_mc) *dp_mc = dp;
    return f;
}

/* ---- controller ---------------------------------------------------------- */

/* Rising latch: on once v >= on, off once v <= off (off < on). */
static bool hyst_hi(bool cur, int32_t v, int32_t on, int32_t off)
{
    return cur ? v > off : v >= on;
}

/* Falling latch: on once v <= on, off once v >= off (off > on). */
static bool hyst_lo(bool cur, int32_t v, int32_t on, int32_t off)
{
    return cur ? v < off : v <= on;
}

static uint32_t umin(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t permille_of(uint32_t v, uint32_t pm)
{
    return (uint32_t) (((uint64_t) v * pm) / 1000u);
}

static bool profile_ok(const cl_profile_t *p)
{
    if (!p || !p->slug || !p->name) return false;
    if (p->sustained_mw == 0 || p->sustained_mw > p->peak_mw) return false;
    switch (p->cls) {
    case CL_CLASS_BRIEFCASE:
        if (!(p->prefs & (1u << CL_PREF_BALANCED)) || (p->prefs & ~(uint32_t) PREF_ALL))
            return false;
        for (uint32_t i = 0; i < CL_PREF__COUNT; i++)
            if (p->pref_cap_mw[i] == 0 || p->pref_cap_mw[i] > p->rail_peak_mw[CL_RAIL_CPU])
                return false;
        if (p->scram_die_mc <= CL_C(60) || p->scram_dt_mc <= CL_HYST_DT_MC ||
            p->scram_ambient_mc <= CL_HYST_TEMP_MC || p->hot_permille > 1000u)
            return false;
        return p->rail_peak_mw[CL_RAIL_IT] == 0;
    case CL_CLASS_SERVER:
        return p->shells >= 1 && p->heater_mw > 0 && p->compressors >= 1 && p->compressors <= 2 &&
               p->rail_peak_mw[CL_RAIL_IT] > 0 && p->rh_max_x100 > CL_HYST_RH &&
               p->rh_max_x100 <= 10000u && p->supply_max_mc > CL_C(18) &&
               p->outside_min_mc < p->free_cool_max_mc && p->free_cool_max_mc < p->outside_max_mc;
    case CL_CLASS_HANDHELD:
        return p->rail_peak_mw[CL_RAIL_CPU] > 0 &&
               p->rail_peak_mw[CL_RAIL_CPU] <= p->sustained_mw &&
               p->op_min_mc < p->cold_throttle_mc && p->cold_throttle_mc < p->charge_min_mc &&
               p->charge_min_mc + CL_HYST_CHARGE_MC < p->charge_max_mc - CL_HYST_TEMP_MC &&
               p->charge_max_mc < p->op_max_mc && p->cold_permille <= 1000u;
    }
    return false;
}

bool cl_init(cl_ctl_t *c, const cl_profile_t *p)
{
    if (!c || !profile_ok(p)) return false;
    *c = (cl_ctl_t){0};
    c->p = p;
    c->mode = CL_MODE_NONE;
    return true;
}

void cl_power_down(cl_ctl_t *c)
{
    if (c) c->rails_on = false;
}

bool cl_service_clear(cl_ctl_t *c, uint32_t faults)
{
    if (!c || !c->p || (faults & CL_F_JET_ANY)) return false;
    c->limp = false;
    return true;
}

/* Blowers on a temperature curve (PLATFORM_COMMON s6.3 "on a temperature
 * curve"; the curve is not given): lo per-mille at or below 50 C, rising
 * linearly to full 10 K below the scram trigger [default]. */
static uint32_t blower_curve(const cl_profile_t *p, int32_t die, uint32_t lo)
{
    int32_t t0 = CL_C(50), t1 = p->scram_die_mc - CL_C(10);
    if (die == CL_NO_READING || die >= t1) return CL_PERMILLE_MAX;
    if (die <= t0) return lo;
    return lo + (uint32_t) (((uint64_t) (CL_PERMILLE_MAX - lo) * (uint32_t) (die - t0)) /
                            (uint32_t) (t1 - t0));
}

/* Shed `amt` from *a, then the rest from *b; returns what could not be shed. */
static uint32_t shed2(uint32_t *a, uint32_t *b, uint32_t amt)
{
    uint32_t x = umin(*a, amt);
    *a -= x;
    amt -= x;
    x = umin(*b, amt);
    *b -= x;
    return amt - x;
}

static void step_briefcase(cl_ctl_t *c, const cl_input_t *in, cl_output_t *o)
{
    const cl_profile_t *p = c->p;
    /* scram causes, each with its own hysteresis; a missing reading is a
     * cause (the arbiter cannot see the temperature) [default] */
    bool die = in->die_mc == CL_NO_READING || hyst_hi(c->h_die, in->die_mc, p->scram_die_mc + 1,
                                                      p->scram_die_mc - CL_HYST_TEMP_MC);
    bool dt;
    if (in->intake_mc == CL_NO_READING || in->exhaust_mc == CL_NO_READING)
        dt = true;
    else {
        int64_t d = (int64_t) in->exhaust_mc - in->intake_mc;
        if (d > INT32_MAX) d = INT32_MAX;
        if (d < INT32_MIN + 1) d = INT32_MIN + 1;
        dt = hyst_hi(c->h_dt, (int32_t) d, p->scram_dt_mc + 1, p->scram_dt_mc - CL_HYST_DT_MC);
    }
    bool amb = in->ambient_mc == CL_NO_READING ||
               hyst_hi(c->h_amb, in->ambient_mc, p->scram_ambient_mc + 1,
                       p->scram_ambient_mc - CL_HYST_TEMP_MC);
    bool jet = (in->faults & CL_F_JET_ANY) != 0;
    uint32_t causes = (die ? CL_SC_DIE : 0) | (dt ? CL_SC_DELTA_T : 0) | (amb ? CL_SC_AMBIENT : 0) |
                      (jet && p->jet_fault_scrams ? CL_SC_JET : 0);
    /* Limp home on a micro-jet fault, held until service (PLATFORM_COMMON
     * s6.3). On the Pioneer the fault is first a scram cause (OEM_CONSUMER
     * s4); limp home follows once the scram clears. */
    bool limp = c->limp || jet;

    uint32_t pref = (p->prefs & (1u << in->pref)) ? in->pref : (uint32_t) CL_PREF_BALANCED;
    cl_mode_t mode;
    if (causes)
        mode = CL_MODE_SCRAM;
    else if (in->manual)
        mode = CL_MODE_MANUAL;
    else if (limp)
        mode = CL_MODE_LIMP_HOME;
    else if (pref == CL_PREF_QUIET)
        mode = CL_MODE_QUIET;
    else if (pref == CL_PREF_PERFORMANCE)
        mode = CL_MODE_PERFORMANCE;
    else
        mode = CL_MODE_BALANCED;

    /* Balanced runs the blowers only above the silent ceiling. */
    bool blowers = hyst_hi(
        c->h_blowers, (int32_t) umin(in->power_mw, INT32_MAX), (int32_t) p->silent_ceiling_mw + 1,
        (int32_t) (p->silent_ceiling_mw > CL_HYST_POWER_MW ? p->silent_ceiling_mw - CL_HYST_POWER_MW
                                                           : 0));

    uint32_t fan;
    switch (mode) {
    case CL_MODE_QUIET:
        fan = 0;
        break; /* OEM_CONSUMER s4: 0 rpm, micro-jets only */
    case CL_MODE_BALANCED:
        fan = blowers ? blower_curve(p, in->die_mc, 250u) : 0;
        break;
    case CL_MODE_PERFORMANCE:
        fan = blower_curve(p, in->die_mc, 700u);
        break; /* "high" [default] */
    case CL_MODE_LIMP_HOME:
        fan = 500u;
        break; /* s6.3: locked at 50 % */
    default:
        fan = CL_PERMILLE_MAX;
        break; /* Scram, Manual: maximum */
    }

    /* Power caps. Limp home keeps the Balanced cap [default]. */
    uint32_t cpu_norm =
        p->pref_cap_mw[mode == CL_MODE_LIMP_HOME ? (uint32_t) CL_PREF_BALANCED : pref];
    uint32_t cpu = cpu_norm, neural = p->rail_peak_mw[CL_RAIL_NEURAL], proj = 0;
    /* OEM_INSTITUTIONAL s5: the all-on peak exceeds the rating; the CPU and
     * neural caps hold the rating. Neural is shed first, then CPU [default
     * order; the spec does not split it]. */
    uint64_t all = (uint64_t) cpu + neural + p->other_mw;
    if (all > p->peak_mw) shed2(&neural, &cpu, (uint32_t) (all - p->peak_mw));
    uint32_t cpu_base = cpu, neural_base = neural;
    /* Projector: off unless the hatch is open (PLATFORM_COMMON s4); when on,
     * neural power is shed by the same amount (OEM_INSTITUTIONAL s5), and
     * whatever neural cannot give comes from the CPU [default]. */
    uint32_t flags = 0;
    if (p->rail_peak_mw[CL_RAIL_PROJECTOR] && in->hatch_open && in->projector_req) {
        proj = p->rail_peak_mw[CL_RAIL_PROJECTOR];
        if (p->rail_peak_mw[CL_RAIL_NEURAL]) {
            shed2(&neural, &cpu, proj);
            flags |= CL_O_PROJECTOR_SHED;
        }
    }
    uint64_t sum = (uint64_t) cpu + neural + proj + p->other_mw;
    if (sum > p->peak_mw) shed2(&cpu, &neural, (uint32_t) (sum - p->peak_mw));
    if (mode == CL_MODE_SCRAM) {
        cpu = umin(cpu, permille_of(p->rail_peak_mw[CL_RAIL_CPU], p->hot_permille));
        neural = umin(neural, permille_of(p->rail_peak_mw[CL_RAIL_NEURAL], p->hot_permille));
    }
    if (cpu < cpu_base || neural < neural_base) flags |= CL_O_THROTTLED;

    /* commit */
    c->h_die = die && in->die_mc != CL_NO_READING;
    c->h_dt = dt && in->intake_mc != CL_NO_READING && in->exhaust_mc != CL_NO_READING;
    c->h_amb = amb && in->ambient_mc != CL_NO_READING;
    c->h_blowers = blowers;
    c->limp = limp;
    c->last_power_mw = in->power_mw;
    c->last_headroom_mc = in->die_mc == CL_NO_READING ? 0 : p->scram_die_mc - in->die_mc;
    c->last_compute_cap_mw = cpu + neural;

    o->mode = mode;
    o->fan_permille = fan;
    o->jets_on = !jet; /* the micro-jets run in every mode unless faulted */
    o->cap_mw[CL_RAIL_CPU] = cpu;
    o->cap_mw[CL_RAIL_NEURAL] = neural;
    o->cap_mw[CL_RAIL_PROJECTOR] = proj;
    o->total_cap_mw = cpu + neural + proj + p->other_mw;
    o->scram_causes = causes;
    o->flags = flags;
    /* No briefcase spec gates charging on temperature; the packs' own
     * management (UN 38.3, IEC 62133-2, PLATFORM_COMMON s8.1) governs. */
    o->charge_enable = true;
}

static uint32_t supply_fan(const cl_profile_t *p, int32_t supply)
{
    /* Internal fans: 30 % at or below 18 C supply air, full at the supply
     * limit [default; the spec gives no fan curve]. */
    int32_t t0 = CL_C(18), t1 = p->supply_max_mc;
    if (supply == CL_NO_READING || supply >= t1) return CL_PERMILLE_MAX;
    if (supply <= t0) return 300u;
    return 300u + (uint32_t) (((uint64_t) 700u * (uint32_t) (supply - t0)) / (uint32_t) (t1 - t0));
}

static void step_server(cl_ctl_t *c, const cl_input_t *in, cl_output_t *o)
{
    const cl_profile_t *p = c->p;
    int32_t dp;
    uint32_t il = cl_interlock(p, in, &dp);
    bool rails = c->rails_on || il == 0;

    /* Regime (s2.2): free cooling below about +15 C outside, active above;
     * a missing outside reading runs active cooling [default]. */
    bool active =
        in->outside_mc == CL_NO_READING || hyst_hi(c->h_active, in->outside_mc, p->free_cool_max_mc,
                                                   p->free_cool_max_mc - CL_HYST_TEMP_MC);
    /* Load: supply air over the internal envelope also starts the compressor. */
    bool supply_hot = in->supply_mc == CL_NO_READING ||
                      hyst_hi(c->h_supply_hot, in->supply_mc, p->supply_max_mc + 1,
                              p->supply_max_mc - CL_HYST_TEMP_MC);
    /* Humidity envelope (s1): dew point <= 15 C and RH <= 60 %. */
    bool humid;
    if (dp == CL_NO_READING)
        humid = true;
    else if (c->h_humid || c->dehum)
        humid = !(dp <= p->dp_max_mc - CL_HYST_DP_MC &&
                  in->internal_rh_x100 + CL_HYST_RH <= p->rh_max_x100);
    else
        humid = dp > p->dp_max_mc || in->internal_rh_x100 > p->rh_max_x100;

    /* Door service lock (s2.4): cooling is raised first, the door unlocks on
     * the following step; after it closes the dehumidify cycle runs before
     * any power limit is raised back. */
    bool svc = in->service_request || in->door_open;
    bool in_svc = c->mode == CL_MODE_SERVICE;
    bool prep = c->service_prep, unlocked = c->door_unlocked, opened = c->door_opened;
    bool dehum = c->dehum;
    uint32_t hold = c->hold_cap_mw;
    uint32_t it_cap = rails ? p->rail_peak_mw[CL_RAIL_IT] : 0;
    if (svc) {
        if (!in_svc) {
            prep = true;
            unlocked = false;
            opened = false;
            /* a second service during a dehumidify cycle keeps the older hold */
            hold = dehum ? umin(hold, it_cap) : it_cap;
        } else if (prep) {
            prep = false;
            unlocked = true;
        }
        if (in->door_open) opened = true;
    } else if (in_svc) {
        prep = false;
        unlocked = false;
        if (opened) dehum = true;
        opened = false;
    }
    /* the cycle runs for at least one step, and ends inside the envelope */
    if (c->dehum && !svc && !humid) dehum = false;

    cl_mode_t mode;
    if (svc)
        mode = CL_MODE_SERVICE;
    else if (!rails)
        mode = CL_MODE_WARMUP;
    else if (dehum || humid)
        mode = CL_MODE_DEHUMIDIFY;
    else if (in->facility_water)
        mode = CL_MODE_FACILITY_WATER;
    else if (active || supply_hot)
        mode = CL_MODE_ACTIVE_COOLING;
    else
        mode = CL_MODE_FREE_COOLING;

    /* Compressor: active cooling, raised cooling in service, and the
     * evaporator's dehumidifying (s2.4). Not fitted on facility water. */
    bool want = !in->facility_water && (mode == CL_MODE_ACTIVE_COOLING || mode == CL_MODE_SERVICE ||
                                        mode == CL_MODE_DEHUMIDIFY);
    bool a_ok = !(in->faults & CL_F_COMPRESSOR_A);
    bool b_ok = p->compressors >= 2 && !(in->faults & CL_F_COMPRESSOR_B);
    uint32_t flags = 0;
    o->compressor_on = want && (a_ok || b_ok);
    o->compressor_unit = a_ok ? 0u : 1u;
    if (!o->compressor_on) o->compressor_unit = 0;
    if (want && !a_ok && !b_ok) flags |= CL_O_NO_COMPRESSOR;

    /* Heaters (s2.3): cold start, and holding the shell above the LFP
     * charging limit. Hold band: on at or below 2 C, off at 5 C [default]. */
    bool lfp_heat =
        in->lfp_mc == CL_NO_READING || hyst_lo(c->h_lfp_heat, in->lfp_mc, CL_C(2), CL_C(5));
    bool warm_fail =
        (il & (CL_IL_HDD_COLD | CL_IL_SSD_COLD | CL_IL_CONDENSATION | CL_IL_LFP_COLD)) != 0;
    o->heater_on = (!rails && warm_fail) || (in->lfp_mc != CL_NO_READING && lfp_heat);
    /* LFP charging (s4): never at or below 0 C; re-enabled 1 K above. */
    bool lfp_chg = in->lfp_mc != CL_NO_READING &&
                   hyst_hi(c->h_lfp_charge, in->lfp_mc, IL_LFP_MIN + CL_HYST_CHARGE_MC, IL_LFP_MIN);

    o->fan_permille = mode == CL_MODE_SERVICE  ? CL_PERMILLE_MAX
                      : mode == CL_MODE_WARMUP ? 300u /* circulate heater air [default] */
                                               : supply_fan(p, in->supply_mc);
    if (mode == CL_MODE_SERVICE || dehum) it_cap = umin(it_cap, hold);
    if (it_cap < (rails ? p->rail_peak_mw[CL_RAIL_IT] : 0)) flags |= CL_O_THROTTLED;
    if (in->outside_mc != CL_NO_READING &&
        (in->outside_mc > p->outside_max_mc || in->outside_mc < p->outside_min_mc))
        flags |= CL_O_OUT_OF_ENVELOPE;

    /* commit */
    c->rails_on = rails;
    c->h_active = active && in->outside_mc != CL_NO_READING;
    c->h_supply_hot = supply_hot && in->supply_mc != CL_NO_READING;
    c->h_humid = humid && dp != CL_NO_READING;
    c->h_lfp_heat = lfp_heat && in->lfp_mc != CL_NO_READING;
    c->h_lfp_charge = lfp_chg;
    c->service_prep = prep;
    c->door_unlocked = unlocked;
    c->door_opened = opened;
    c->dehum = dehum;
    c->hold_cap_mw = hold;
    c->last_power_mw = in->power_mw;
    c->last_headroom_mc = in->supply_mc == CL_NO_READING ? 0 : p->supply_max_mc - in->supply_mc;
    c->last_compute_cap_mw = it_cap;

    o->mode = mode;
    o->rails_on = rails;
    o->door_unlock = unlocked;
    o->charge_enable = lfp_chg;
    o->cap_mw[CL_RAIL_IT] = it_cap;
    o->total_cap_mw = it_cap;
    o->interlock = il;
    o->flags = flags;
    o->dewpoint_mc = dp;
}

static void step_handheld(cl_ctl_t *c, const cl_input_t *in, cl_output_t *o)
{
    const cl_profile_t *p = c->p;
    bool have = in->cell_mc != CL_NO_READING;
    int32_t t = in->cell_mc;
    /* HARDENING s5: below about -20 C the devices throttle; a missing cell
     * reading throttles too [default]. */
    bool cold = !have || hyst_lo(c->h_cold, t, p->cold_throttle_mc - 1,
                                 p->cold_throttle_mc + CL_HYST_TEMP_MC);
    /* The cell heater runs from the charger and brings a cold cell up to
     * 0 C before charging; it stops 1 K above. */
    bool heat =
        have && in->charger_present &&
        hyst_lo(c->h_cell_heat, t, p->charge_min_mc - 1, p->charge_min_mc + CL_HYST_CHARGE_MC);
    /* Charging window 0 to +45 C: off below 0 C (back on 1 K above), off
     * above 45 C (back on 3 K below). */
    bool lo_ok = have && hyst_hi(c->h_charge_lo, t, p->charge_min_mc + CL_HYST_CHARGE_MC,
                                 p->charge_min_mc - 1);
    bool hi_bad = !have || hyst_hi(c->h_charge_hi, t, p->charge_max_mc + 1,
                                   p->charge_max_mc - CL_HYST_TEMP_MC);

    uint32_t cpu = p->rail_peak_mw[CL_RAIL_CPU];
    uint32_t flags = 0;
    if (cold) {
        cpu = permille_of(cpu, p->cold_permille);
        flags |= CL_O_THROTTLED;
    }
    if (heat) flags |= CL_O_COLD_CELL;
    if (have && (t < p->op_min_mc || t > p->op_max_mc)) flags |= CL_O_OUT_OF_ENVELOPE;

    /* commit */
    c->h_cold = cold && have;
    c->h_cell_heat = heat;
    c->h_charge_lo = lo_ok;
    c->h_charge_hi = hi_bad && have;
    c->last_power_mw = in->power_mw;
    c->last_headroom_mc = have ? p->op_max_mc - t : 0;
    c->last_compute_cap_mw = cpu;

    o->mode = cold ? CL_MODE_COLD_THROTTLE : CL_MODE_NORMAL;
    o->heater_on = heat;
    o->charge_enable = in->charger_present && lo_ok && !hi_bad;
    o->cap_mw[CL_RAIL_CPU] = cpu;
    o->total_cap_mw = p->sustained_mw; /* the arbiter holds the sustained limit */
    o->flags = flags;
}

bool cl_step(cl_ctl_t *c, const cl_input_t *in, cl_output_t *out)
{
    if (!c || !c->p || !in || !out) return false;
    if ((in->faults & ~CL_F__ALL) || in->pref >= CL_PREF__COUNT) return false;
    if (in->n_hdd > CL_MAX_SENSORS || in->n_ssd > CL_MAX_SENSORS || in->n_surface > CL_MAX_SENSORS)
        return false;
    cl_class_t cls = c->p->cls;
    if (cls != CL_CLASS_BRIEFCASE && cls != CL_CLASS_SERVER && cls != CL_CLASS_HANDHELD)
        return false;
    /* field by field: a whole-struct copy would call memcpy on some targets */
    out->mode = CL_MODE_NONE;
    out->fan_permille = 0;
    out->jets_on = out->heater_on = out->compressor_on = false;
    out->compressor_unit = 0;
    out->rails_on = out->door_unlock = out->charge_enable = false;
    for (uint32_t r = 0; r < CL_RAIL__COUNT; r++) out->cap_mw[r] = 0;
    out->total_cap_mw = out->interlock = out->scram_causes = out->flags = 0;
    out->dewpoint_mc = CL_NO_READING;
    if (cls == CL_CLASS_BRIEFCASE)
        step_briefcase(c, in, out);
    else if (cls == CL_CLASS_SERVER)
        step_server(c, in, out);
    else
        step_handheld(c, in, out);
    if (out->mode != c->mode) c->transitions++;
    c->mode = out->mode;
    c->ticks++;
    return true;
}
