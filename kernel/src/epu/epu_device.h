/* epu_device.h — Emotional Processing Unit (EPU)
 *
 * ############################################################
 * ##                                                        ##
 * ##   THIS IS A MODEL. IT IS NOT A DEVICE DRIVER.          ##
 * ##                                                        ##
 * ##   There is no EPU silicon. There is no magnetoelectric ##
 * ##   post-quantum accelerator, no 144-qubit buffer, no     ##
 * ##   room-temperature superconducting coil, no 1 THz       ##
 * ##   "EmotionBus", and no crystal nanofabric. Nothing in   ##
 * ##   this file reads or writes a hardware register, a      ##
 * ##   memory-mapped aperture, an IRQ line, or a bus.        ##
 * ##                                                        ##
 * ##   Every function here is arithmetic on the in-memory    ##
 * ##   epu_device_t struct. It SIMULATES; it does not DRIVE. ##
 * ##   The word "device" in the type names is historical.    ##
 * ##   Treat this module as a numeric toy model / test       ##
 * ##   fixture, in the same class as a physics sandbox.      ##
 * ##                                                        ##
 * ##   No result computed here is evidence about the         ##
 * ##   physical world.                                       ##
 * ##                                                        ##
 * ############################################################
 *
 * WHAT THE MODEL ACTUALLY COMPUTES (all of this is real arithmetic,
 * and all of it is asserted against hand-computed values in
 * test_epu_device.c):
 *
 *   - 5-D emotion vector algebra: Euclidean intensity, and a
 *     norm-preserving Givens rotation that couples the "mind" pair
 *     (joy, awe) against the "heart" pair (love, gratitude) by an
 *     angle driven by the mean magnetoelectric coupling of the cell
 *     array. At full coupling the angle is exactly 90 degrees, which
 *     is the only sense in which anything here is "orthogonal".
 *
 *   - Cell array: a linear constitutive chain V -> E -> B -> H with
 *     a real Terfenol-D coercivity threshold, so a drive below the
 *     threshold produces NO magnetic response and says so.
 *
 *   - Phyllotaxis ("Fibonacci") spiral placement of the 256 cells:
 *     theta_i = i * golden_angle, r_i = sqrt(theta_i / golden_angle).
 *
 *   - Coils: B = mu0 * N * I / (2R) at the centre of a circular loop,
 *     with a golden-ratio shaping multiplier and an I^2R power term.
 *
 *   - Crystal layers: piezo response scaled by nanocrystal density and
 *     drive frequency, and an acoustic resonance that shifts with the
 *     modelled die temperature through a linear temperature coefficient.
 *
 *   - Qubit buffer: a genuine single-qubit state vector per qubit
 *     (two complex amplitudes), unitary gates, coherence decay per
 *     operation, fidelity decay per gate, and a seedable, deterministic
 *     Born-rule measurement that really draws from a PRNG and really
 *     collapses the state. Re-measuring a collapsed qubit returns the
 *     same outcome without drawing again, as it must.
 *
 *   - Harmonics: solfeggio octave doubling, and vortex ("3-6-9")
 *     doubling whose digital roots close on 3-6-3-6 / 9-9-9 — a
 *     property the test checks numerically rather than asserting.
 *
 *   - Power and temperature: bus + per-cell + coil I^2R, feeding a
 *     linear thermal resistance, feeding the crystal resonance shift.
 *
 * ================== LIMITATIONS (read these) ==================
 *
 * L1. NO HARDWARE. See the banner. Every "irq_*" flag is a bool this
 *     module sets from its own state when you call epu_handle_irq();
 *     nothing is ever raised by a real interrupt controller.
 *
 * L2. THE QUANTUM MODEL IS SINGLE-QUBIT ONLY. Each qubit carries its
 *     own 2-amplitude state. epu_qubit_entangle() does NOT build a
 *     4-amplitude two-qubit state. It sets a correlation flag whose
 *     only effect is that measuring one partner collapses the other to
 *     the SAME outcome. That reproduces the |00>+|11> correlation and
 *     NOTHING ELSE: no Bell-inequality violation, no basis-dependent
 *     correlation, no partial entanglement, no decoherence-free
 *     subspaces. Do not use it to study entanglement.
 *
 * L3. THE PHYSICAL CONSTANTS IN THE ORIGINAL SCHEMATIC ARE MUTUALLY
 *     INCONSISTENT AND THE MODEL DOES NOT PAPER OVER IT. 10 V across a
 *     100 nm PZT film is 1e8 V/m, which is ten times the quoted 10 MV/m
 *     Al2O3 breakdown field. The model therefore enforces the +/-10 V
 *     limit as the governing constraint and DOES NOT simulate dielectric
 *     breakdown at all. Nothing here predicts whether such a stack could
 *     be built. It could not, as far as anyone knows.
 *
 * L4. "1 THz EmotionBus", "PCIe 6.0 fallback", "room-temperature
 *     superconducting", "1597-turn Fibonacci coil": these are stored
 *     numbers and flags. bus_bandwidth_hz only ever feeds the derived
 *     throughput ceiling. pcie_fallback_active is stored and never
 *     acted on, because there is no bus to fall back to. The
 *     superconducting flag, if you set it yourself, only removes that
 *     coil's I^2R term from the power total. No API turns it on,
 *     because no room-temperature superconductor exists.
 *
 * L5. emotion_throughput IS NOT MEASURED. There is no clock in this
 *     module. It holds a DERIVED CEILING, bus_bandwidth_hz divided by
 *     the bits in one DMA frame — an arithmetic consequence of two
 *     constants, not an observation. The honest counter of work done is
 *     emotions_processed.
 *
 * L6. THE DMA BUFFERS ARE HOST-ORDER SNAPSHOTS, NOT A WIRE FORMAT.
 *     epu_process_emotion() serialises its input into tx_buffer and its
 *     output into rx_buffer as raw in-memory doubles. The layout is
 *     whatever this machine's IEEE-754 byte order is. It is a debug
 *     trace, not a protocol; do not send it anywhere.
 *
 * L7. THE GENERATED HDL IS A PARAMETER SKELETON, NOT A DESIGN.
 *     epu_generate_hdl() emits a module/entity header, an integer
 *     generic/parameter block carrying this device's configuration, and
 *     a port list. It contains NO implementation of the arithmetic
 *     above. It has never been through a synthesiser, a linter, or a
 *     simulator. Do not describe it as "synthesisable"; it is an export
 *     of the parameter set in HDL syntax. It is also written into one
 *     shared static buffer, so it is NOT reentrant and the returned
 *     pointer is invalidated by the next call.
 *
 * L8. COVERAGE IS NOT THE M5 HYPERBOLA. epu_verify_coverage() computes
 *     r and l as FRACTIONS in [0,1], so the EDP coverage floor of 1.8
 *     (edp_risk.h) is unreachable here by construction. Using it would
 *     make the check either always-false or a tautology. The floor used
 *     is EPU_COVERAGE_FLOOR and it is a device-population metric, not an
 *     M5 economic quantity. The m5 field is mirrored for inspection
 *     only; nothing in this module consumes it.
 *
 *     Further: activation is ALL-OR-NOTHING. epu_device_activate() turns
 *     every cell on and epu_device_deactivate() turns every cell off, and
 *     no API activates one cell, so r is only ever exactly 0.0 or exactly
 *     1.0. In practice the coverage verdict is therefore driven entirely
 *     by l, the fraction of qubits that still have coherence budget AND
 *     fidelity at or above EPU_FIDELITY_FLOOR. Do not read r as a
 *     continuously varying quantity; it is a device on/off bit wearing a
 *     fraction's clothes.
 *
 * L9. THE SOLFEGGIO AND VORTEX FREQUENCY SETS ARE NUMEROLOGY. They are
 *     modelled exactly (the tables and the doubling arithmetic are
 *     correct) but they carry no physical or physiological meaning, and
 *     this module makes no claim that they do. EPU_SOLFEGGIO_432 is used
 *     only as a normalising divisor; note it is NOT a member of the
 *     nine-frequency solfeggio_freq_t set.
 *
 * L10. epu_diagnostic_dump() writes through an OPTIONAL text sink you
 *     bind with epu_bind_sink(). With no sink bound it writes nothing
 *     and reports nothing. It does not fall back to printf, because in
 *     a freestanding kernel there is no printf to fall back to.
 *     It is NOT a pure observer: it calls epu_refresh_aggregates(),
 *     epu_recompute_power(), epu_get_system_health() and
 *     epu_verify_coverage(), so it rewrites the derived fields
 *     (average_fidelity, total_coherence_us, power_consumption_mw,
 *     operating_temp_k, coverage_r, coverage_l, m5) from current state.
 *     Those recomputations are idempotent, but the dump is a "refresh
 *     and print", not a read-only snapshot.
 *
 * L11. TWO OF THE FIVE HEALTH SUB-SCORES CANNOT FAIL IN THIS MODEL, AND
 *     THAT IS ARITHMETIC, NOT AN OVERSIGHT. The largest draw the model
 *     can produce is bus 12 mW + 256 cells x 0.25 mW + 8 coils x
 *     (0.1 A)^2 x 0.05 ohm x 1000 = 12 + 64 + 4 = 80.000 mW exactly,
 *     against an EPU_POWER_BUDGET_MW of 100. No API can push it higher:
 *     the cell count is fixed, the coil current is hard-limited to
 *     EPU_COIL_MAX_A, and epu_get_system_health() recomputes power from
 *     the arrays before scoring, so poking power_consumption_mw by hand
 *     does not survive either. Therefore s_pow is ALWAYS exactly 1.0 and
 *     s_th is always in [1 - 4/300, 1] = [0.98667, 1]. The health score
 *     is bounded below by (0 + 0 + 0 + 1 + 0.98667)/5 = 0.39733; it can
 *     never reach 0 for a non-NULL device. The sub-scores that actually
 *     move are s_cells, s_fid and s_coh. test_epu_device.c derives the
 *     80.000 mW ceiling by saturating the model rather than assuming it.
 *
 * L12. THE SYSTEM-LEVEL AGGREGATES ARE NOT LIVE. epu_system_t's
 *     system_coherence_us (MIN over devices) and system_fidelity (MEAN
 *     over devices) are recomputed only by epu_system_create_device()
 *     and by an explicit epu_system_refresh(). Nothing that mutates a
 *     device updates them, so after you run gates on sys.devices[i] the
 *     two system fields are STALE until you call epu_system_refresh().
 *     They are a snapshot with a refresh button, not a live view.
 *
 * L13. NON-FINITE INPUT IS REFUSED, NOT PROPAGATED. Every entry point
 *     that takes a double rejects NaN and the infinities rather than
 *     letting them into the model state: the range checks are written in
 *     the negated form (!(x >= lo && x <= hi)), the clamps send NaN to
 *     the low bound, and epu_process_emotion() / epu_emotion_to_quantum()
 *     refuse a non-finite emotion vector outright and count nothing.
 *
 * L14. HOST AND TARGET DO NOT USE THE SAME TRANSCENDENTALS, AND THE GAP IS
 *     MEASURED RATHER THAN ASSUMED. Square roots are computed by this
 *     module's own epu_sqrt(), which is bit-exact against IEEE-754
 *     correctly-rounded sqrt and is the SAME code on both host and target,
 *     so those agree exactly. cos() and sin() are not: on the host they are
 *     libm, on the target they are freestanding.h's 15-term Taylor series.
 *     Over the only two argument sets this module uses — the Givens angle
 *     in [0, pi/2] and the single golden-phase angle 2*pi/phi — the two
 *     agree to within 4.441e-16 absolute, measured, which is inside every
 *     tolerance test_epu_device.c asserts (the tightest is 1e-15 on gate
 *     amplitudes, and the trig-dependent ones are 1e-12/1e-13). The whole
 *     suite has been run a second time with the target's Taylor cos/sin
 *     substituted in, and passes identically. Do not tighten a
 *     trig-dependent tolerance below 1e-14 without re-running that.
 *
 * L15. THIS MODULE IS NOT LINKED INTO ANY KERNEL IMAGE. epu_device.c does
 *     not appear in kernel/Makefile or build_system/Makefile.arm64, and no
 *     file outside src/epu/ includes epu_device.h. It compiles freestanding
 *     (verified at -O0, -O1, -O2, -Os and -O3, zero undefined symbols) and
 *     it has a host test, but nothing calls it. Treat it as a library that
 *     is ready to be linked, not as running kernel code.
 *
 * ================== ERROR DISCIPLINE ==================
 *
 * Functions returning int return EPU_OK (0) only when the described work
 * actually happened, a documented positive status when a DIFFERENT
 * documented thing happened, and a negative EPU_ERR_* otherwise.
 * Functions returning double return a negative sentinel for an invalid
 * index, because every physical quantity they report is non-negative.
 * Counters (cycle_count, emotions_processed) advance only for work that
 * actually occurred.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC
 */
#ifndef EPU_DEVICE_H
#define EPU_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"   /* m5_coords_t */

/* ===== Nominal constants (from the schematic; see L3) ===== */
#define EPU_PHI              1.6180339887498949
#define EPU_PHI_INV          0.6180339887498949
#define EPU_RESONANCE_HZ     28318.5     /* phi-scaled PZT resonance */
#define EPU_SOLFEGGIO_432    432.0       /* normalising divisor only — see L9 */
#define EPU_SOLFEGGIO_528    528.0       /* transformation frequency */
#define EPU_SOLFEGGIO_963    963.0       /* pineal activation */
#define EPU_VORTEX_BASE      3.0         /* vortex math base (3-6-9) */
#define EPU_COHERENCE_US     16180.0     /* phi x 10 ms nominal T2* */
#define EPU_FIDELITY_TARGET  0.999
#define EPU_ME_COUPLING_MAX  1e-9        /* s/m — see EPU_ME notes below */
#define EPU_NUM_CELLS        256         /* 16x16 ME core array */
#define EPU_NUM_QUBITS       144         /* 12^2 matrix */
#define EPU_FIBONACCI_TURNS  1597        /* F17 */
#define EPU_EMOTION_DIMS     5           /* 5D emotion vector */

/* ===== Model constants introduced by the implementation =====
 *
 * These are the numbers the arithmetic actually uses. Where the
 * schematic gave a value we used it; where it gave none we picked one
 * that is traceable to a stated constant and said so, rather than
 * inventing an unexplained magic number.
 *
 * Dimensional note for EPU_ME_COUPLING_MAX: alpha has units s/m, and
 * [s/m] * [V/m] = V*s/m^2 = Wb/m^2 = T, so B = alpha * E is dimensionally
 * exact. That is the whole constitutive chain: V -> E -> B -> H.
 */
#define EPU_PZT_THICKNESS_M    100e-9    /* 100 nm PZT film */
#define EPU_PZT_MAX_V          10.0      /* +/-10 V drive limit (governing, see L3) */
#define EPU_COERCIVITY_A_M     160.0     /* Terfenol-D coercivity, A/m */
#define EPU_MU0                1.2566370614359173e-6 /* 4*pi*1e-7, H/m */
#define EPU_Q_NOMINAL          1597.0    /* = F17; the schematic gives no Q */
#define EPU_GOLDEN_ANGLE_RAD   2.3999632297286533   /* 2*pi/phi^2 */
#define EPU_COIL_RADIUS_M      0.01      /* 1 cm loop radius */
#define EPU_COIL_MAX_A         0.1       /* 100 mA drive limit */
#define EPU_COIL_R_OHM         0.05      /* per-coil resistance (non-SC) */
#define EPU_CRYSTAL_DENSITY    1.0e12    /* layer 0 crystals/cm^2; layer i = base*phi^-i */
#define EPU_CRYSTAL_TEMPCO     (-1.0e-4) /* fractional resonance shift per kelvin */
#define EPU_TEMP_AMBIENT_K     300.0
#define EPU_THERMAL_K_PER_MW   0.05      /* linear thermal resistance */
#define EPU_BUS_POWER_MW       12.0      /* bus static draw when active */
#define EPU_CELL_POWER_MW      0.25      /* per active cell */
#define EPU_FIDELITY_FLOOR     0.90      /* below this a qubit is "degraded" */
#define EPU_GATE_TIME_US       1.0       /* coherence consumed per operation */
#define EPU_COVERAGE_FLOOR     0.5       /* see L8 — NOT the M5 1.8 floor */
#define EPU_DMA_FRAME_BYTES    64        /* 4096 / 64 = exactly 64 frames */
#define EPU_DMA_FRAMES         64
#define EPU_RESONANCE_TOL      0.01      /* 1% harmonic match window */
#define EPU_FIELD_IMBALANCE_T  0.005     /* coil spread that trips the IRQ */
#define EPU_POWER_BUDGET_MW    100.0     /* the "<100 mW" design target */
#define EPU_HDL_BUF_BYTES      4096      /* shared static HDL buffer — see L7 */
#define EPU_MAX_OCTAVE         20        /* harmonic index ceiling */
#define EPU_MAX_HARMONIC       32

/* ===== Return codes ===== */
#define EPU_OK                  0
#define EPU_OK_SUBCOERCIVE      1   /* drive applied; below coercivity, NO magnetic response */
#define EPU_ERR_NULL           (-1)
#define EPU_ERR_RANGE          (-2)
#define EPU_ERR_INACTIVE       (-3)  /* device not activated */
#define EPU_ERR_BUSY           (-4)  /* already in the requested state / already entangled */
#define EPU_ERR_DECOHERED      (-5)  /* qubit has no coherence budget left */
#define EPU_ERR_NO_RESOURCE    (-6)  /* not enough free qubits */

/* Sentinel returned by the double-valued accessors for a bad index.
 * Every quantity they report is non-negative, so a negative value is
 * unambiguously an error and not a reading. */
#define EPU_BAD_READING        (-1.0)

/* ===== Vortex mathematics frequencies (3-6-9 pattern) ===== */
typedef enum {
    VORTEX_3   = 0,    /* 3 Hz base */
    VORTEX_6   = 1,    /* 6 Hz base */
    VORTEX_9   = 2,    /* 9 Hz base */
    VORTEX_36  = 3,    /* 36 Hz */
    VORTEX_63  = 4,    /* 63 Hz */
    VORTEX_69  = 5,    /* 69 Hz */
    VORTEX_96  = 6,    /* 96 Hz */
    VORTEX_369 = 7,    /* 369 Hz (Tesla key) */
} vortex_freq_t;
#define EPU_NUM_VORTEX 8

/* ===== Solfeggio frequencies ===== */
typedef enum {
    SOLFEGGIO_174 = 0,
    SOLFEGGIO_285 = 1,
    SOLFEGGIO_396 = 2,
    SOLFEGGIO_417 = 3,
    SOLFEGGIO_528 = 4,
    SOLFEGGIO_639 = 5,
    SOLFEGGIO_741 = 6,
    SOLFEGGIO_852 = 7,
    SOLFEGGIO_963 = 8,
} solfeggio_freq_t;
#define EPU_NUM_SOLFEGGIO 9

/* ===== Single-qubit gates (see L2: single-qubit only) ===== */
typedef enum {
    EPU_GATE_I   = 0,   /* identity */
    EPU_GATE_X   = 1,   /* bit flip */
    EPU_GATE_Y   = 2,
    EPU_GATE_Z   = 3,   /* phase flip */
    EPU_GATE_H   = 4,   /* Hadamard */
    EPU_GATE_S   = 5,   /* sqrt(Z), phase i */
    EPU_GATE_T   = 6,   /* pi/4 phase */
    EPU_GATE_PHI = 7,   /* golden phase, exp(i*2*pi/phi) on |1> */
} epu_gate_t;
#define EPU_NUM_GATES 8

/* ===== Emotion vector (5D) ===== */
typedef struct {
    double joy;       /* mind pair */
    double love;      /* heart pair */
    double serenity;  /* rotation axis — invariant under the coupling */
    double awe;       /* mind pair */
    double gratitude; /* heart pair */
} emotion_vector_t;

/* ===== ME core cell state ===== */
typedef struct {
    uint32_t cell_id;
    double pzt_voltage;         /* applied drive, volts, |V| <= EPU_PZT_MAX_V */
    double terfenol_field;      /* B = alpha * E, tesla (0 if sub-coercive) */
    double me_coupling;         /* alpha, s/m (0 if sub-coercive) */
    double resonance_freq;      /* Hz */
    double q_factor;            /* Q = Q0 / (1 + phi*|f-f0|/f0) */
    bool active;
    uint64_t cycle_count;       /* drive cycles actually applied to this cell */
    /* Phyllotaxis spiral position */
    double spiral_r;
    double spiral_theta;
} epu_me_cell_t;

/* ===== Quantum buffer qubit ===== */
typedef struct {
    uint32_t qubit_id;
    double coherence_time_us;   /* remaining T2* budget, microseconds */
    double fidelity;            /* accumulated gate fidelity, 0-1 */
    double t1_time_us;          /* T1 relaxation time (stored; T1 = phi*T2) */
    bool entangled;
    uint32_t entangled_with;    /* partner qubit id — correlation only, see L2 */
    uint32_t fib_row;           /* id / 12 */
    uint32_t fib_col;           /* id % 12 */

    /* --- state vector: added by the implementation, because without it
     * epu_qubit_apply_gate() and epu_qubit_measure() would have nothing
     * to act on and would be hollow. |psi> = a|0> + b|1>. --- */
    double a_re, a_im;          /* amplitude of |0> */
    double b_re, b_im;          /* amplitude of |1> */
    bool   collapsed;           /* true once measured, until a gate is applied */
    uint8_t outcome;            /* 0 or 1; meaningful only while collapsed */
} epu_qubit_t;

/* ===== Field coil ===== */
typedef struct {
    uint32_t coil_id;
    uint32_t turns;             /* F17 = 1597 */
    double golden_ratio_field;  /* shaping multiplier, default 1.0 */
    double current;             /* amperes, |I| <= EPU_COIL_MAX_A */
    double field_strength;      /* B at centre, tesla */
    bool superconducting;       /* model flag only — see L4 */
} epu_field_coil_t;

/* ===== Crystal blanket layer ===== */
typedef struct {
    uint32_t layer_id;
    double nanocrystal_density; /* crystals per cm^2 */
    double piezo_response;      /* dimensionless, 1.0 at layer 0 / 432 Hz */
    double thermal_stability;   /* fractional resonance shift per kelvin */
    bool fabric_mode;           /* flexible-substrate flag */
    solfeggio_freq_t active_freq;
    bool active;                /* added: an inactive layer has no resonance */
} epu_crystal_layer_t;

/* ===== Optional text sink for epu_diagnostic_dump (see L10) ===== */
typedef struct {
    void (*write)(void *ctx, const char *s);
    void *ctx;
} epu_sink_t;

/* ===== EPU model instance ===== */
typedef struct {
    uint32_t device_id;
    char name[64];

    epu_me_cell_t cells[EPU_NUM_CELLS];
    epu_qubit_t qubits[EPU_NUM_QUBITS];
    epu_field_coil_t coils[8];
    epu_crystal_layer_t crystal_layers[4];

    /* "EmotionBus" — stored numbers, see L4 */
    double bus_bandwidth_hz;
    double bus_latency_ns;
    bool bus_active;

    /* "PCIe 6.0 fallback" — stored, never acted on, see L4 */
    double pcie_bandwidth_gbps;
    bool pcie_fallback_active;

    /* Aggregates, recomputed from the arrays — never guessed */
    double total_coherence_us;   /* SUM over qubits of remaining T2* */
    double average_fidelity;     /* MEAN over qubits */
    double emotion_throughput;   /* DERIVED CEILING, not a measurement — L5 */
    double power_consumption_mw;
    double operating_temp_k;

    vortex_freq_t vortex_mode;
    solfeggio_freq_t solfeggio_mode;
    double current_frequency_hz;  /* whichever of the two was set last */

    /* M5 mirror — inspection only, see L8 */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;

    /* Debug trace rings — host order, see L6 */
    uint8_t tx_buffer[4096];
    uint8_t rx_buffer[4096];
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t rx_head;
    uint32_t rx_tail;

    /* Model flags, set by epu_handle_irq() from this struct's own state */
    bool irq_coherence_lost;
    bool irq_emotion_ready;
    bool irq_quantum_decoherence;
    bool irq_field_imbalance;
    bool irq_crystal_resonance;

    /* --- added by the implementation --- */
    uint64_t rng_state;          /* seedable measurement PRNG (SplitMix64) */
    uint64_t emotions_processed; /* honest count of completed emotion transforms */
    epu_sink_t sink;             /* optional diagnostic text sink */
} epu_device_t;

/* ===== Multi-instance container ===== */
typedef struct {
    epu_device_t devices[4];
    uint32_t num_devices;
    double system_coherence_us;  /* MIN over devices — the weakest link governs */
    double system_fidelity;      /* MEAN over devices */
} epu_system_t;

/* ===== API ===== */

/* System lifecycle */
void epu_system_init(epu_system_t *sys);
/* Returns the new device id (1-based), or 0 if full / sys is NULL. */
uint32_t epu_system_create_device(epu_system_t *sys, const char *name);
/* Recompute system_coherence_us (MIN over devices — the weakest link
 * governs) and system_fidelity (MEAN over devices) from the member
 * devices. Nothing else keeps them current; see L12. No-op for NULL. */
void epu_system_refresh(epu_system_t *sys);

/* Device lifecycle. init() zeroes and populates nominal values with the
 * cell array and bus OFF; activate() turns them on. `name` is copied into
 * a 64-byte field and TRUNCATED to 63 characters plus a NUL; a NULL name
 * becomes "epu". */
void epu_device_init(epu_device_t *dev, uint32_t id, const char *name);
int epu_device_activate(epu_device_t *dev);    /* EPU_ERR_BUSY if already active */
/* Powers the model down: every cell off, every coil de-energised, every
 * crystal layer extinguished (an unpowered layer has no resonance, so
 * epu_crystal_get_resonance() reads 0.0 afterwards), power and IRQ state
 * re-derived. EPU_ERR_BUSY if already inactive. */
int epu_device_deactivate(epu_device_t *dev);

/* Seed the measurement PRNG. Call after epu_device_init(), which seeds
 * deterministically from the device id. Same seed => same collapses. */
void epu_seed(epu_device_t *dev, uint64_t seed);

/* Bind (or unbind, with sink == NULL) the diagnostic text sink.
 * Call AFTER epu_device_init(), which zeroes the whole struct. */
void epu_bind_sink(epu_device_t *dev, const epu_sink_t *sink);

/* ME core cell operations */
/* EPU_OK, or EPU_OK_SUBCOERCIVE when |H| < coercivity (drive recorded,
 * no magnetic response), or EPU_ERR_RANGE when |voltage| > EPU_PZT_MAX_V
 * or cell_id is out of range — in which case the cell is NOT modified. */
int epu_cell_stimulate(epu_device_t *dev, uint32_t cell_id, double voltage);
/* alpha in s/m, or EPU_BAD_READING for a bad index. */
double epu_cell_read_coupling(epu_device_t *dev, uint32_t cell_id);
int epu_cell_set_resonance(epu_device_t *dev, uint32_t cell_id, double freq_hz);
/* theta is clamped to [0, EPU_NUM_CELLS*golden_angle]; r = sqrt(theta/gamma). */
void epu_cell_update_spiral(epu_device_t *dev, uint32_t cell_id, double theta);

/* Quantum buffer operations (single-qubit model — see L2) */
int epu_qubit_entangle(epu_device_t *dev, uint32_t q1, uint32_t q2);
/* Returns 0 or 1 on a real Born-rule collapse, or a negative EPU_ERR_*.
 * Re-measuring a collapsed qubit returns the SAME outcome and does not
 * draw again. */
int epu_qubit_measure(epu_device_t *dev, uint32_t qubit_id);
/* Remaining T2* budget in microseconds, or EPU_BAD_READING for a bad index. */
double epu_qubit_get_coherence(epu_device_t *dev, uint32_t qubit_id);
int epu_qubit_apply_gate(epu_device_t *dev, uint32_t qubit_id, uint8_t gate_type);

/* Emotion processing.
 * epu_process_emotion returns the all-zero vector (and counts nothing,
 * pushes no trace frame and raises no IRQ) if dev or input is NULL, the
 * device is not active, or ANY component of the input is NaN or
 * infinite — see L13. A NaN transform is not a completed transform, so
 * emotions_processed must not advance for one. */
emotion_vector_t epu_process_emotion(epu_device_t *dev, const emotion_vector_t *input);
/* Euclidean norm, or 0.0 for a NULL or non-finite vector. Note that 0.0
 * is therefore both "the null emotion" and "not a usable vector"; callers
 * that need to tell them apart must check the components themselves. */
double epu_compute_emotion_intensity(const emotion_vector_t *v);
/* Writes EPU_EMOTION_DIMS qubit ids; qubit_ids must have room for that
 * many. EPU_ERR_RANGE for the null or non-finite emotion (neither has a
 * direction), EPU_ERR_NO_RESOURCE when fewer than EPU_EMOTION_DIMS free
 * qubits remain — in both cases the caller's array is left untouched. */
int epu_emotion_to_quantum(epu_device_t *dev, const emotion_vector_t *emotion,
                           uint32_t *qubit_ids);

/* Field coil operations */
int epu_coil_activate(epu_device_t *dev, uint32_t coil_id, double current);
/* |B| at the loop centre in tesla (a MAGNITUDE — the sign of the current
 * carries the direction, so a negative return is unambiguously an error).
 * EPU_BAD_READING for a bad index. */
double epu_coil_compute_field(epu_device_t *dev, uint32_t coil_id);
/* phi_factor is clamped to [phi^-2, phi^2]; out-of-range ids are ignored. */
void epu_coil_set_golden_ratio(epu_device_t *dev, uint32_t coil_id, double phi_factor);

/* Crystal blanket operations */
int epu_crystal_activate(epu_device_t *dev, uint32_t layer_id, solfeggio_freq_t freq);
/* Temperature-shifted resonance in Hz; 0.0 if the layer is inactive,
 * EPU_BAD_READING for a bad index. */
double epu_crystal_get_resonance(epu_device_t *dev, uint32_t layer_id);

/* Frequency management. Out-of-range enum values leave the device unchanged. */
void epu_set_vortex_frequency(epu_device_t *dev, vortex_freq_t mode);
void epu_set_solfeggio_frequency(epu_device_t *dev, solfeggio_freq_t freq);
/* base * 2^harmonic; 0.0 if mode or harmonic is out of range. */
double epu_compute_vortex_harmonic(vortex_freq_t mode, uint32_t harmonic);
/* base * 2^octave; 0.0 if freq or octave is out of range. */
double epu_compute_solfeggio_harmonic(solfeggio_freq_t freq, uint32_t octave);

/* Coverage and health. BOTH OF THESE CAN AND DO FAIL — see the tests
 * that drive them to false / to a low score. */
bool epu_verify_coverage(epu_device_t *dev);
/* 0.0 .. 1.0, the mean of five sub-scores; recomputes the aggregates. */
double epu_get_system_health(epu_device_t *dev);
/* Writes a plain-text report through the bound sink. No sink => nothing. */
void epu_diagnostic_dump(epu_device_t *dev);

/* Parameter export in HDL syntax — NOT a design, see L7.
 * language is "verilog" or "vhdl" (case-insensitive). Any other value,
 * or a NULL device, returns NULL.
 *
 * epu_generate_hdl() writes into one shared static buffer and is NOT
 * reentrant (L7). epu_generate_hdl_buf() writes into a caller-supplied
 * buffer and IS reentrant; it returns `buf` on success and NULL if the
 * device or language is bad, or if `buf`/`cap` cannot hold the whole
 * skeleton — a truncated skeleton would be a lie dressed as output, so
 * it is never returned. It writes at most `cap` bytes including the
 * terminating NUL and never touches buf[cap] or beyond. */
const char *epu_generate_hdl(epu_device_t *dev, const char *language);
const char *epu_generate_hdl_buf(epu_device_t *dev, const char *language,
                                 char *buf, uint32_t cap);

/* Re-evaluate the level-triggered IRQ flags from current state, and
 * acknowledge (clear) the edge-triggered irq_emotion_ready. */
void epu_handle_irq(epu_device_t *dev);

#endif /* EPU_DEVICE_H */
