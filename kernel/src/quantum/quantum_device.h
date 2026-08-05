/* quantum_device.h — Quantum & Exotic Matter Device Module
 *
 * Hardware-as-code quantum devices with:
 *   - Casimir effect cavity arrays
 *   - Zero-point energy (ZPE) extraction
 *   - Wormhole throat at zero-point
 *   - Exotic matter generation
 *   - Harmonic rendering
 *   - Second quantization (field operators)
 *
 * Each device is a virtual quantum processor with register maps,
 * quantum state registers, and M⁵ coverage verification.
 *
 * Three execution modes:
 *   DC: Direct quantum measurement (collapse)
 *   AC: Alternating quantum oscillation (coherent evolution)
 *   PC: Phase/photonic quantum (entanglement operations)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef QUANTUM_DEVICE_H
#define QUANTUM_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* ===== Quantum Device Types ===== */

typedef enum {
    QDEV_CASIMIR_CAVITY    = 0,  /* Casimir effect cavity array */
    QDEV_ZPE_EXTRACTOR     = 1,  /* Zero-point energy extractor */
    QDEV_WORMHOLE_THROAT   = 2,  /* Wormhole throat at zero-point */
    QDEV_EXOTIC_MATTER     = 3,  /* Exotic matter generator */
    QDEV_HARMONIC_RENDERER = 4,  /* Harmonic quantum renderer */
    QDEV_ENTANGLEMENT_BUS  = 5,  /* Quantum entanglement bus */
    QDEV_QUBIT_ARRAY       = 6,  /* Qubit register array */
    QDEV_QUBIT_ANNEALER    = 7,  /* Quantum annealer */
    QDEV_PHOTONIC_LATTICE  = 8,  /* Photonic quantum lattice */
    QDEV_MAX               = 9
} quantum_device_type_t;

/* ===== Quantum Execution Mode ===== */

typedef enum {
    QMODE_DC = 0,  /* Direct: measurement/collapse */
    QMODE_AC = 1,  /* Alternating: coherent oscillation */
    QMODE_PC = 2,  /* Phase: entanglement/photonic */
} quantum_mode_t;

/* ===== Casimir Cavity Array ===== */

typedef struct {
    /* Register map */
    surplus_real_t reg_plate_separation;  /* a (meters) */
    surplus_real_t reg_plate_area;        /* A (m²) */
    surplus_real_t reg_casimir_force;     /* F = -π²ℏc/(240a⁴) */
    surplus_real_t reg_energy_density;    /* ρ = -π²ℏc/(720a⁴) */
    surplus_real_t reg_temperature;       /* T (Kelvin) */
    
    /* Array configuration */
    uint32_t num_cavities;
    uint32_t cavity_spacing_nm;
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA: cavity state buffer */
    uint32_t dma_cavity_states[16];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQs */
    bool irq_casimir_threshold;
    bool irq_energy_extracted;
    bool irq_coverage_breach;
    
    bool active;
} casimir_array_t;

/* ===== ZPE Extractor ===== */

typedef struct {
    surplus_real_t reg_zpe_density;     /* ρ_zpe = ℏω/2 per mode */
    surplus_real_t reg_extraction_rate;  /* dE/dt */
    surplus_real_t reg_efficiency;       /* η ∈ [0,1] */
    surplus_real_t reg_total_extracted;  /* Cumulative energy */
    
    /* Coupled Casimir array */
    uint32_t casimir_array_id;
    
    /* M⁵ */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA */
    uint32_t dma_energy_log[8];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQs */
    bool irq_extraction_active;
    bool irq_threshold_reached;
    
    bool active;
} zpe_extractor_t;

/* ===== Wormhole Throat ===== */

typedef struct {
    surplus_real_t reg_throat_radius;    /* r_t (Schwarzschild-like) */
    surplus_real_t reg_exotic_density;   /* ρ_exotic < 0 (negative energy) */
    surplus_real_t reg_traversable;      /* [0,1] — traversability index */
    surplus_real_t reg_stability;        /* [0,1] — throat stability */
    
    /* Throat endpoints */
    uint32_t endpoint_a_id;
    uint32_t endpoint_b_id;
    surplus_real_t reg_distance;         /* Proper distance through throat */
    
    /* M⁵ */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA: transit log */
    uint32_t dma_transit[4];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQs */
    bool irq_throat_open;
    bool irq_instability;
    bool irq_transit_requested;
    
    bool active;
} wormhole_throat_t;

/* ===== Exotic Matter Generator ===== */

typedef struct {
    surplus_real_t reg_negative_energy;  /* E < 0 */
    surplus_real_t reg_production_rate;   /* dE_exotic/dt */
    surplus_real_t reg_stability;         /* [0,1] */
    surplus_real_t reg_containment;       /* [0,1] */
    
    /* Coupled to wormhole */
    uint32_t wormhole_id;
    
    /* M⁵ */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* IRQs */
    bool irq_production_active;
    bool irq_containment_breach;
    
    bool active;
} exotic_matter_t;

/* ===== Harmonic Renderer ===== */

typedef struct {
    surplus_real_t reg_frequency;        /* f (Hz) */
    surplus_real_t reg_amplitude;        /* A */
    surplus_real_t reg_phase;            /* φ */
    surplus_real_t reg_harmonic_order;   /* n (nth harmonic) */
    surplus_real_t reg_resonance_q;      /* Q factor */
    
    /* Harmonic series */
    surplus_real_t harmonics[16];        /* Up to 16th harmonic */
    uint32_t num_harmonics;
    
    /* M⁵ */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA: waveform buffer */
    uint32_t dma_waveform[32];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQs */
    bool irq_resonance_achieved;
    bool irq_phase_locked;
    
    bool active;
} harmonic_renderer_t;

/* ===== Second Quantization Field ===== */

typedef struct {
    /* Field operators: a† (creation) and a (annihilation) */
    uint32_t num_modes;
    
    /* Occupation numbers (Fock state) */
    uint32_t occupation[64];             /* n_k for each mode */
    
    /* Field amplitudes */
    surplus_real_t field_amplitude[64];   /* ⟨ψ|a_k|ψ⟩ */
    surplus_real_t field_phase[64];       /* arg(ψ_k) */
    
    /* Vacuum energy */
    surplus_real_t vacuum_energy;         /* E_0 = Σ ℏω_k/2 */
    
    /* Total particle number */
    surplus_real_t total_particles;       /* N = Σ n_k */
    
    /* M⁵ */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Execution mode */
    quantum_mode_t mode;
    
    /* IRQs */
    bool irq_particle_created;
    bool irq_particle_annihilated;
    bool irq_vacuum_fluctuation;
    
    bool active;
} second_quant_t;

/* ===== Quantum Device System ===== */

typedef struct {
    casimir_array_t      casimir_arrays[16];
    zpe_extractor_t      zpe_extractors[16];
    wormhole_throat_t    wormholes[8];
    exotic_matter_t      exotic_generators[8];
    harmonic_renderer_t  harmonic_renderers[32];
    second_quant_t       quant_fields[16];
    
    uint32_t num_casimir;
    uint32_t num_zpe;
    uint32_t num_wormholes;
    uint32_t num_exotic;
    uint32_t num_harmonic;
    uint32_t num_quant_fields;
    
    /* System-wide quantum state */
    surplus_real_t system_coherence;     /* [0,1] */
    surplus_real_t system_entanglement;  /* [0,1] */
    surplus_real_t system_zpe_total;     /* Total ZPE harvested */
} quantum_system_t;

/* ===== Physical Constants (in fixed-point) ===== */

#ifdef TEST_HOST
#define Q_PLANCK_REDUCED  (1.054571817e-34)  /* ℏ (J·s) */
#define Q_SPEED_OF_LIGHT  (2.99792458e8)      /* c (m/s) */
#define Q_PI_SQUARED      (9.869604401089358) /* π² */
#else
/* In fixed-point, we store scaled values */
#define Q_PLANCK_REDUCED  SR_FROM_FLOAT(1.054571817e-34)
#define Q_SPEED_OF_LIGHT  SR_FROM_FLOAT(2.99792458e8)
#define Q_PI_SQUARED      SR_FROM_FLOAT(9.869604401089358)
#endif

/* ===== API ===== */

void quantum_system_init(quantum_system_t *qs);

/* Casimir cavity */
uint32_t quantum_casimir_create(quantum_system_t *qs,
                                 surplus_real_t separation,
                                 surplus_real_t area,
                                 uint32_t num_cavities);
void quantum_casimir_compute(casimir_array_t *cav);
surplus_real_t quantum_casimir_force(surplus_real_t separation);

/* ZPE extractor */
uint32_t quantum_zpe_create(quantum_system_t *qs, uint32_t casimir_id);
surplus_real_t quantum_zpe_extract(zpe_extractor_t *zpe, surplus_real_t dt);

/* Wormhole throat */
uint32_t quantum_wormhole_create(quantum_system_t *qs,
                                  surplus_real_t throat_radius,
                                  uint32_t endpoint_a,
                                  uint32_t endpoint_b);
bool quantum_wormhole_open(wormhole_throat_t *wh);
int32_t quantum_wormhole_transit(wormhole_throat_t *wh, uint32_t payload_id);

/* Exotic matter */
uint32_t quantum_exotic_create(quantum_system_t *qs, uint32_t wormhole_id);
surplus_real_t quantum_exotic_produce(exotic_matter_t *em, surplus_real_t dt);

/* Harmonic renderer */
uint32_t quantum_harmonic_create(quantum_system_t *qs,
                                  surplus_real_t frequency,
                                  surplus_real_t amplitude);
void quantum_harmonic_compute(harmonic_renderer_t *hr);
void quantum_harmonic_add_resonance(harmonic_renderer_t *hr, uint32_t order);

/* Second quantization */
uint32_t quantum_field_create(quantum_system_t *qs, uint32_t num_modes);
int32_t quantum_field_create_particle(second_quant_t *field, uint32_t mode);
int32_t quantum_field_annihilate_particle(second_quant_t *field, uint32_t mode);
surplus_real_t quantum_field_vacuum_energy(second_quant_t *field,
                                            const surplus_real_t *omega,
                                            uint32_t num_modes);
surplus_real_t quantum_field_expectation_value(const second_quant_t *field,
                                                uint32_t mode);

/* System update */
void quantum_system_update(quantum_system_t *qs);

/* Device type name */
const char *quantum_device_name(quantum_device_type_t t);
const char *quantum_mode_name(quantum_mode_t m);

#endif /* QUANTUM_DEVICE_H */
