/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate.h — thermal modes and the climate shell controller for the device
 * line: the briefcases (Pioneer, Vanguard, Sovereign, Citadel), the six
 * servers in their climate shells and the handhelds.
 *
 * Sources (the hardware specs; section numbers are cited at each rule):
 *   OEM_CONSUMER.md s4        Pioneer thermal modes: Quiet, Balanced,
 *                             Performance, Scram.
 *   PLATFORM_COMMON.md s6.3   briefcase fan modes: Silent, Normal, Scram,
 *                             Limp home, Manual; hot-ambient cap via ct_budget.
 *   OEM_INSTITUTIONAL.md s3-5 tier ratings and the per-subsystem power table;
 *                             projector on sheds neural power by the same watts.
 *   SERVER_COMMON.md s1-2, s4 climate contract, heat-out regimes, heaters and
 *                             the start interlock, humidity and the door rule,
 *                             LFP not charged below 0 C.
 *   HARDENING.md s5,          handheld operating and charging windows, the
 *   MOBILE_COMMON.md s2,      cell heater, cold throttle; per-device power
 *   MOBILE_DEVICES.md s1-3    limits.
 *
 * Units: temperature in milli-degrees C (int32), power in milliwatts
 * (uint32), relative humidity in hundredths of a percent (0..10000), fan
 * duty in per-mille (0..1000). No floating point anywhere; no allocation;
 * every array is bounded by a compile-time constant. Every threshold has a
 * hysteresis band (on at one value, off at another), so a reading sitting
 * on a threshold cannot make a mode chatter.
 *
 * One controller (cl_ctl_t) runs one device; cl_step() takes one sample of
 * the inputs and produces the outputs. The order inside a step is fixed:
 * check the inputs, update the hysteresis latches, pick the mode, compute
 * the outputs, then commit the new state to the controller.
 */
#ifndef ZXV_CLIMATE_H
#define ZXV_CLIMATE_H

#include <stdint.h>
#include <stdbool.h>

#define CL_C(c)         ((int32_t) (c) * 1000)   /* whole degrees C to milli-degrees */
#define CL_W(w)         ((uint32_t) (w) * 1000u) /* whole watts to milliwatts */
#define CL_RH(p)        ((uint32_t) (p) * 100u)  /* whole percent RH to hundredths */
#define CL_NO_READING   INT32_MIN                /* a temperature sensor with no reading */
#define CL_PERMILLE_MAX 1000u

/* Hysteresis widths. The specs give trigger values but no release values;
 * these widths are defaults taken here, not spec figures. */
#define CL_HYST_TEMP_MC   3000  /* 3 K below (or above) a temperature trigger [default] */
#define CL_HYST_DT_MC     2000  /* 2 K on the exhaust-minus-intake trigger [default] */
#define CL_HYST_POWER_MW  5000u /* 5 W below a power ceiling [default] */
#define CL_HYST_RH        500u  /* 5 % RH below the humidity limit [default] */
#define CL_HYST_DP_MC     1000  /* 1 K below the dew-point limit [default] */
#define CL_HYST_CHARGE_MC 1000  /* charging re-enables 1 K inside the window [default] */

#define CL_MAX_SENSORS 16u /* per sensor group (hard drives, SSDs, surfaces) */

typedef enum {
    CL_CLASS_BRIEFCASE = 1,
    CL_CLASS_SERVER = 2,
    CL_CLASS_HANDHELD = 3,
} cl_class_t;

typedef enum {
    CL_MODE_NONE = 0,
    /* briefcase: OEM_CONSUMER s4 names, PLATFORM_COMMON s6.3 for the rest.
     * PLATFORM_COMMON's Silent and Normal are Balanced with the blowers off
     * (package power under the silent ceiling) and on. */
    CL_MODE_QUIET,
    CL_MODE_BALANCED,
    CL_MODE_PERFORMANCE,
    CL_MODE_SCRAM,
    CL_MODE_LIMP_HOME,
    CL_MODE_MANUAL,
    /* server shell: SERVER_COMMON s2.2 to s2.4 */
    CL_MODE_WARMUP, /* start interlock holding the IT rails off, heaters on */
    CL_MODE_FREE_COOLING,
    CL_MODE_ACTIVE_COOLING,
    CL_MODE_FACILITY_WATER,
    CL_MODE_SERVICE,    /* door service lock */
    CL_MODE_DEHUMIDIFY, /* after the door closes, or humidity out of envelope */
    /* handheld: HARDENING s5 */
    CL_MODE_NORMAL,
    CL_MODE_COLD_THROTTLE,
    CL_MODE__COUNT
} cl_mode_t;

/* Owner preference for the briefcase (OEM_CONSUMER s4 modes). */
typedef enum {
    CL_PREF_QUIET = 0,
    CL_PREF_BALANCED = 1,
    CL_PREF_PERFORMANCE = 2,
    CL_PREF__COUNT
} cl_pref_t;

/* Power rails the controller caps. A rail whose profile peak is 0 is not
 * fitted on that device and its cap is always 0. */
typedef enum {
    CL_RAIL_CPU = 0,   /* APU / CPU / application processor */
    CL_RAIL_NEURAL,    /* institutional neural module(s) */
    CL_RAIL_PROJECTOR, /* rear projector, own switched rail */
    CL_RAIL_IT,        /* server: every IT rail behind the start interlock */
    CL_RAIL__COUNT
} cl_rail_t;

/* Fault flags (cl_input_t.faults). */
#define CL_F_JET_OPEN     0x01u /* micro-jet bank open circuit */
#define CL_F_JET_OVERCURR 0x02u /* micro-jet bank over-current */
#define CL_F_JET_ANY      (CL_F_JET_OPEN | CL_F_JET_OVERCURR)
#define CL_F_COMPRESSOR_A 0x04u /* server compressor unit A failed */
#define CL_F_COMPRESSOR_B 0x08u /* server compressor unit B failed */
#define CL_F__ALL         0x0Fu

/* Scram causes (cl_output_t.scram_causes). */
#define CL_SC_DIE     0x01u /* die over the profile's trigger */
#define CL_SC_DELTA_T 0x02u /* exhaust minus intake over 45 K */
#define CL_SC_AMBIENT 0x04u /* ambient over 48 C */
#define CL_SC_JET     0x08u /* micro-jet fault (Pioneer, OEM_CONSUMER s4) */

/* Start interlock failures (cl_output_t.interlock), 0 = all conditions hold.
 * SERVER_COMMON s2.3. A missing reading fails its condition (no evidence is
 * not a pass). */
#define CL_IL_HDD_COLD     0x001u /* a hard drive below 10 C */
#define CL_IL_SSD_COLD     0x002u /* an SSD below 0 C */
#define CL_IL_CONDENSATION 0x004u /* a surface less than 3 K above the dew point */
#define CL_IL_LFP_COLD     0x008u /* LFP battery not above 0 C */
#define CL_IL_NO_HDD       0x010u /* hard drives fitted, no reading */
#define CL_IL_NO_SSD       0x020u /* all-flash shell, no SSD reading */
#define CL_IL_NO_SURFACE   0x040u /* no surface reading */
#define CL_IL_NO_DEWPOINT  0x080u /* internal air T or RH missing or out of range */
#define CL_IL_NO_LFP       0x100u /* no LFP reading */
#define CL_IL_BAD_INPUT    0x200u /* sensor count above CL_MAX_SENSORS */

/* Output flags (cl_output_t.flags). */
#define CL_O_THROTTLED       0x01u /* a cap is below the profile's normal cap */
#define CL_O_OUT_OF_ENVELOPE 0x02u /* outside the spec's operating envelope */
#define CL_O_NO_COMPRESSOR   0x04u /* compressor wanted, every unit failed */
#define CL_O_PROJECTOR_SHED  0x08u /* neural (then CPU) shed for the projector */
#define CL_O_COLD_CELL       0x10u /* handheld: cell heater warming before charge */

typedef struct {
    const char *slug;
    const char *name;
    cl_class_t cls;
    uint32_t sustained_mw; /* device sustained rating */
    uint32_t peak_mw;      /* device peak rating: the total cap */
    uint32_t rail_peak_mw[CL_RAIL__COUNT];
    uint32_t other_mw; /* briefcase: fixed loads (DRAM, storage, arbiter, NICs, displays, fans) */

    /* briefcase */
    uint32_t prefs;                       /* bit per cl_pref_t the device offers */
    uint32_t pref_cap_mw[CL_PREF__COUNT]; /* CPU cap per owner preference */
    uint32_t silent_ceiling_mw;           /* Balanced: blowers off below this package power */
    int32_t scram_die_mc;                 /* scram when the hottest die is above this */
    int32_t scram_dt_mc;                  /* scram when exhaust minus intake is above this */
    int32_t scram_ambient_mc;             /* scram when ambient is above this */
    bool jet_fault_scrams;                /* micro-jet fault is a scram cause */
    uint32_t hot_permille;                /* hot-ambient / scram compute cap, per-mille of peak */

    /* server */
    uint32_t shells;          /* climate shells (Hoard has two) */
    uint32_t heater_mw;       /* PTC heater per shell */
    uint32_t compressors;     /* compressor units per shell (N+1 = 2) */
    bool hdd_fitted;          /* hard-drive capacity tier: the 10 C condition applies */
    int32_t free_cool_max_mc; /* outside temperature where active cooling starts */
    int32_t supply_max_mc;    /* internal supply air upper limit */
    int32_t dp_max_mc;        /* internal dew point upper limit */
    uint32_t rh_max_x100;     /* internal RH upper limit */
    int32_t outside_max_mc;   /* external operating envelope, upper */
    int32_t outside_min_mc;   /* external operating envelope, lower */

    /* handheld */
    int32_t op_min_mc, op_max_mc;         /* operating window (cell temperature) */
    int32_t charge_min_mc, charge_max_mc; /* charging window (cell temperature) */
    int32_t cold_throttle_mc;             /* throttle below this */
    uint32_t cold_permille;               /* processor cap while cold, per-mille */
} cl_profile_t;

typedef struct {
    /* briefcase */
    int32_t die_mc;     /* hottest die */
    int32_t intake_mc;  /* air intake */
    int32_t exhaust_mc; /* air exhaust */
    int32_t ambient_mc;
    uint32_t power_mw; /* measured compute power: package (briefcase, handheld), IT (server) */
    uint32_t faults;   /* CL_F_* */
    uint32_t pref;     /* cl_pref_t */
    bool manual;       /* port-trench toggle (PLATFORM_COMMON s6.3) */
    bool projector_req;
    bool hatch_open;

    /* server */
    int32_t outside_mc;
    int32_t supply_mc;   /* internal supply air */
    int32_t internal_mc; /* internal air, with internal_rh for the dew point */
    uint32_t internal_rh_x100;
    uint32_t n_hdd, n_ssd, n_surface;
    int32_t hdd_mc[CL_MAX_SENSORS];
    int32_t ssd_mc[CL_MAX_SENSORS];
    int32_t surface_mc[CL_MAX_SENSORS];
    int32_t lfp_mc;
    bool door_open;
    bool service_request;
    bool facility_water; /* site has facility chilled water: rear-door coil */

    /* handheld */
    int32_t cell_mc;
    bool charger_present;
} cl_input_t;

typedef struct {
    cl_mode_t mode;
    uint32_t fan_permille; /* briefcase blowers; server internal fans */
    bool jets_on;          /* briefcase micro-jets */
    bool heater_on;        /* server PTC heaters; handheld cell heater */
    bool compressor_on;
    uint32_t compressor_unit; /* 0 = A, 1 = B, when compressor_on */
    bool rails_on;            /* server IT rails (start interlock) */
    bool door_unlock;
    bool charge_enable;
    uint32_t cap_mw[CL_RAIL__COUNT];
    uint32_t total_cap_mw;
    uint32_t interlock;    /* CL_IL_* failures, 0 = pass */
    uint32_t scram_causes; /* CL_SC_* */
    uint32_t flags;        /* CL_O_* */
    int32_t dewpoint_mc;   /* internal dew point, CL_NO_READING when unknown */
} cl_output_t;

typedef struct {
    const cl_profile_t *p;
    cl_mode_t mode;
    /* hysteresis latches */
    bool h_die, h_dt, h_amb;       /* scram causes */
    bool h_blowers;                /* Balanced: package power above the silent ceiling */
    bool limp;                     /* micro-jet fault seen; held until cl_service_clear */
    bool h_active;                 /* outside warm enough for active cooling */
    bool h_supply_hot;             /* supply air over the limit */
    bool h_humid;                  /* internal air out of the humidity envelope */
    bool h_lfp_charge;             /* LFP warm enough to charge */
    bool h_lfp_heat;               /* LFP close to its charging limit: hold it warm */
    bool h_cold;                   /* handheld cold throttle */
    bool h_cell_heat;              /* handheld cell heater */
    bool h_charge_lo, h_charge_hi; /* handheld charging window, low and high side */
    /* server sequencing */
    bool rails_on;
    bool service_prep; /* cooling raised, door not yet unlocked */
    bool door_unlocked;
    bool door_opened;     /* door has opened during this service */
    bool dehum;           /* running the dehumidify cycle */
    uint32_t hold_cap_mw; /* IT cap held during service and dehumidify */
    /* what cl_ct_metrics reports to ct_budget */
    uint32_t last_power_mw;
    int32_t last_headroom_mc;
    uint32_t last_compute_cap_mw;
    uint32_t ticks, transitions;
} cl_ctl_t;

/* Integer dew point (Magnus form, Sonntag 1990 constants b = 17.62,
 * c = 243.12 C, over water). t in -45..+60 C, rh 1..10000 (0.01 % to
 * 100 %). Returns false (td untouched) outside that range. The integer
 * evaluation stays within 10 mK of the Magnus formula evaluated exactly.
 * Against the ITS-90 saturation-pressure equation (Hardy 1998) the Magnus
 * form is within 0.1 K for t from 0 to +60 C and within 0.6 K from -45 C,
 * for RH from 1 % up (the error grows as the dew point falls far below
 * 0 C: 1.6 K at 0.01 % RH, a dew point near -105 C). These bounds are
 * checked by test_climate against reference tables. */
bool cl_dewpoint_mc(int32_t t_mc, uint32_t rh_x100, int32_t *td_mc);

/* Natural log of x (x >= 1) in Q30, |error| < 2^-26. */
int64_t cl_ln_q30(uint32_t x);

/* Start interlock (SERVER_COMMON s2.3): CL_IL_* failures, 0 when every
 * condition holds. dp_mc receives the internal dew point (or
 * CL_NO_READING). */
uint32_t cl_interlock(const cl_profile_t *p, const cl_input_t *in, int32_t *dp_mc);

/* The profile table. */
uint32_t cl_profile_count(void);
const cl_profile_t *cl_profile(uint32_t i);            /* NULL past the end */
const cl_profile_t *cl_profile_find(const char *slug); /* NULL if absent */

/* Returns false for a NULL or inconsistent profile. */
bool cl_init(cl_ctl_t *c, const cl_profile_t *p);
/* One control step. Returns false (state and out untouched) for bad
 * arguments: NULL pointers, an uninitialised controller, unknown fault bits,
 * a preference out of range or a sensor count above CL_MAX_SENSORS. */
bool cl_step(cl_ctl_t *c, const cl_input_t *in, cl_output_t *out);
/* Limp home holds "until service" (PLATFORM_COMMON s6.3): service clears
 * the latch, but only once no micro-jet fault is present. */
bool cl_service_clear(cl_ctl_t *c, uint32_t faults);
/* The shell has been powered down: the start interlock gates the next
 * start again. */
void cl_power_down(cl_ctl_t *c);

const char *cl_mode_name(cl_mode_t m);

#endif /* ZXV_CLIMATE_H */
