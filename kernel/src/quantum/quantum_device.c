/* quantum_device.c — Quantum & Exotic Matter Device Module
 *
 * Hardware-as-code quantum devices implementing Casimir cavities,
 * ZPE extraction, wormhole throats, exotic matter, harmonic rendering,
 * and second quantization field operators.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include "quantum_device.h"

static const char *qdev_names[] = {
    "Casimir Cavity", "ZPE Extractor", "Wormhole Throat",
    "Exotic Matter", "Harmonic Renderer", "Entanglement Bus",
    "Qubit Array", "Quantum Annealer", "Photonic Lattice"
};

static const char *qmode_names[] = {
    "DC (Measurement)", "AC (Coherent)", "PC (Entanglement)"
};

const char *quantum_device_name(quantum_device_type_t t) {
    if (t < QDEV_MAX) return qdev_names[t];
    return "Unknown";
}

const char *quantum_mode_name(quantum_mode_t m) {
    if (m <= QMODE_PC) return qmode_names[m];
    return "Unknown";
}

void quantum_system_init(quantum_system_t *qs) {
    qs->num_casimir = 0;
    qs->num_zpe = 0;
    qs->num_wormholes = 0;
    qs->num_exotic = 0;
    qs->num_harmonic = 0;
    qs->num_quant_fields = 0;
    qs->system_coherence = SR_ONE;
    qs->system_entanglement = SR_ZERO;
    qs->system_zpe_total = SR_ZERO;
}

/* ===== Casimir Cavity ===== */

surplus_real_t quantum_casimir_force(surplus_real_t separation) {
    /* F = -π²ℏc / (240 · a⁴)
     * In practice, we compute the magnitude and handle sign separately */
    surplus_real_t a4 = SR_MUL(SR_MUL(separation, separation),
                                SR_MUL(separation, separation));
    if (a4 == SR_ZERO) return SR_ZERO;
    
    surplus_real_t numer = SR_MUL(Q_PI_SQUARED, SR_MUL(Q_PLANCK_REDUCED, Q_SPEED_OF_LIGHT));
    surplus_real_t denom = SR_MUL(SR_FROM_INT(240), a4);
    return SR_DIV(numer, denom); /* Magnitude only */
}

uint32_t quantum_casimir_create(quantum_system_t *qs,
                                 surplus_real_t separation,
                                 surplus_real_t area,
                                 uint32_t num_cavities) {
    if (qs->num_casimir >= 16) return 0xFFFFFFFF;
    casimir_array_t *cav = &qs->casimir_arrays[qs->num_casimir];
    
    cav->reg_plate_separation = separation;
    cav->reg_plate_area = area;
    cav->num_cavities = num_cavities;
    cav->cavity_spacing_nm = 0;
    cav->reg_temperature = SR_ZERO;
    
    /* Compute Casimir force */
    quantum_casimir_compute(cav);
    
    /* M⁵ */
    cav->m5.omega = qs->num_casimir;
    cav->m5.r = cav->reg_casimir_force;
    cav->m5.ell = SR_ONE;
    cav->m5.phi = SR_ZERO;
    cav->m5.chi = 0;
    
    surplus_real_t product = SR_MUL(cav->m5.r, cav->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    cav->coverage_ratio = SR_DIV(product, floor);
    
    /* DMA */
    cav->dma_head = 0;
    cav->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 16; i++) cav->dma_cavity_states[i] = 0;
    
    /* IRQs */
    cav->irq_casimir_threshold = (SR_CMP(cav->reg_casimir_force, SR_ONE) > 0);
    cav->irq_energy_extracted = false;
    cav->irq_coverage_breach = (SR_CMP(cav->coverage_ratio, SR_ONE) < 0);
    
    cav->active = true;
    return qs->num_casimir++;
}

void quantum_casimir_compute(casimir_array_t *cav) {
    /* F = -π²ℏc / (240 · a⁴) per unit area
     * Energy density: ρ = -π²ℏc / (720 · a⁴) */
    surplus_real_t a = cav->reg_plate_separation;
    surplus_real_t a4 = SR_MUL(SR_MUL(a, a), SR_MUL(a, a));
    if (a4 == SR_ZERO) {
        cav->reg_casimir_force = SR_ZERO;
        cav->reg_energy_density = SR_ZERO;
        return;
    }
    
    surplus_real_t coeff = SR_MUL(Q_PI_SQUARED, SR_MUL(Q_PLANCK_REDUCED, Q_SPEED_OF_LIGHT));
    
    /* Force × area */
    surplus_real_t force_per_area = SR_DIV(coeff, SR_MUL(SR_FROM_INT(240), a4));
    cav->reg_casimir_force = SR_MUL(force_per_area, cav->reg_plate_area);
    
    /* Energy density */
    cav->reg_energy_density = SR_DIV(coeff, SR_MUL(SR_FROM_INT(720), a4));
}

/* ===== ZPE Extractor ===== */

uint32_t quantum_zpe_create(quantum_system_t *qs, uint32_t casimir_id) {
    if (qs->num_zpe >= 16) return 0xFFFFFFFF;
    if (casimir_id >= qs->num_casimir) return 0xFFFFFFFF;
    
    zpe_extractor_t *zpe = &qs->zpe_extractors[qs->num_zpe];
    casimir_array_t *cav = &qs->casimir_arrays[casimir_id];
    
    zpe->reg_zpe_density = cav->reg_energy_density;
    zpe->reg_extraction_rate = SR_ZERO;
    zpe->reg_efficiency = SR_FROM_FLOAT(0.01); /* 1% initial */
    zpe->reg_total_extracted = SR_ZERO;
    zpe->casimir_array_id = casimir_id;
    
    /* M⁵ */
    zpe->m5.omega = qs->num_zpe;
    zpe->m5.r = zpe->reg_zpe_density;
    zpe->m5.ell = SR_ONE;
    zpe->m5.phi = SR_ZERO;
    zpe->m5.chi = 0;
    zpe->coverage_ratio = SR_ONE;
    
    /* DMA */
    zpe->dma_head = 0;
    zpe->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) zpe->dma_energy_log[i] = 0;
    
    zpe->irq_extraction_active = false;
    zpe->irq_threshold_reached = false;
    zpe->active = true;
    
    return qs->num_zpe++;
}

surplus_real_t quantum_zpe_extract(zpe_extractor_t *zpe, surplus_real_t dt) {
    /* dE/dt = η · ρ_zpe · A · N_cavities */
    surplus_real_t rate = SR_MUL(SR_MUL(zpe->reg_efficiency, zpe->reg_zpe_density), dt);
    zpe->reg_extraction_rate = rate;
    zpe->reg_total_extracted = SR_ADD(zpe->reg_total_extracted, rate);
    zpe->irq_extraction_active = true;
    
    /* Log to DMA */
    zpe->dma_energy_log[zpe->dma_tail] = (uint32_t)((uint64_t)zpe->reg_total_extracted >> 32);
    zpe->dma_tail = (zpe->dma_tail + 1) % 8;
    
    return rate;
}

/* ===== Wormhole Throat ===== */

uint32_t quantum_wormhole_create(quantum_system_t *qs,
                                  surplus_real_t throat_radius,
                                  uint32_t endpoint_a,
                                  uint32_t endpoint_b) {
    if (qs->num_wormholes >= 8) return 0xFFFFFFFF;
    wormhole_throat_t *wh = &qs->wormholes[qs->num_wormholes];
    
    wh->reg_throat_radius = throat_radius;
    wh->reg_exotic_density = SR_FROM_FLOAT(-0.001); /* Negative energy density */
    wh->reg_traversable = SR_ZERO;
    wh->reg_stability = SR_FROM_FLOAT(0.5);
    wh->endpoint_a_id = endpoint_a;
    wh->endpoint_b_id = endpoint_b;
    wh->reg_distance = SR_ZERO;
    
    /* M⁵ */
    wh->m5.omega = qs->num_wormholes;
    wh->m5.r = throat_radius;
    wh->m5.ell = wh->reg_stability;
    wh->m5.phi = wh->reg_exotic_density; /* Phase = exotic loading */
    wh->m5.chi = 0;
    
    surplus_real_t product = SR_MUL(wh->m5.r, wh->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    wh->coverage_ratio = SR_DIV(product, floor);
    
    /* DMA */
    wh->dma_head = 0;
    wh->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 4; i++) wh->dma_transit[i] = 0;
    
    wh->irq_throat_open = false;
    wh->irq_instability = false;
    wh->irq_transit_requested = false;
    wh->active = true;
    
    return qs->num_wormholes++;
}

bool quantum_wormhole_open(wormhole_throat_t *wh) {
    /* Throat is traversable if exotic matter density is sufficient
     * and stability > 0.5 */
    if (SR_CMP(wh->reg_stability, SR_FROM_FLOAT(0.5)) < 0) {
        wh->irq_instability = true;
        return false;
    }
    
    /* Check coverage */
    surplus_real_t product = SR_MUL(wh->m5.r, wh->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    if (SR_CMP(product, floor) < 0) {
        wh->irq_instability = true;
        return false;
    }
    
    wh->reg_traversable = SR_ONE;
    wh->irq_throat_open = true;
    return true;
}

int32_t quantum_wormhole_transit(wormhole_throat_t *wh, uint32_t payload_id) {
    if (!wh->irq_throat_open) {
        if (!quantum_wormhole_open(wh)) return -1;
    }
    
    wh->irq_transit_requested = true;
    
    /* Log transit */
    wh->dma_transit[wh->dma_tail] = payload_id;
    wh->dma_tail = (wh->dma_tail + 1) % 4;
    
    /* Transit reduces stability */
    wh->reg_stability = SR_SUB(wh->reg_stability, SR_FROM_FLOAT(0.1));
    if (wh->reg_stability < 0) wh->reg_stability = SR_ZERO;
    
    /* Update M⁵ */
    wh->m5.ell = wh->reg_stability;
    
    return 0;
}

/* ===== Exotic Matter ===== */

uint32_t quantum_exotic_create(quantum_system_t *qs, uint32_t wormhole_id) {
    if (qs->num_exotic >= 8) return 0xFFFFFFFF;
    if (wormhole_id >= qs->num_wormholes) return 0xFFFFFFFF;
    
    exotic_matter_t *em = &qs->exotic_generators[qs->num_exotic];
    
    em->reg_negative_energy = SR_FROM_FLOAT(-0.0001);
    em->reg_production_rate = SR_FROM_FLOAT(0.0001);
    em->reg_stability = SR_FROM_FLOAT(0.5);
    em->reg_containment = SR_ONE;
    em->wormhole_id = wormhole_id;
    
    /* M⁵ */
    em->m5.omega = qs->num_exotic;
    em->m5.r = em->reg_production_rate;
    em->m5.ell = em->reg_stability;
    em->m5.phi = em->reg_negative_energy;
    em->m5.chi = 0;
    em->coverage_ratio = SR_ONE;
    
    em->irq_production_active = false;
    em->irq_containment_breach = false;
    em->active = true;
    
    return qs->num_exotic++;
}

surplus_real_t quantum_exotic_produce(exotic_matter_t *em, surplus_real_t dt) {
    surplus_real_t produced = SR_MUL(em->reg_production_rate, dt);
    em->reg_negative_energy = SR_ADD(em->reg_negative_energy, -produced);
    em->irq_production_active = true;
    
    /* Check containment */
    if (SR_CMP(em->reg_containment, SR_FROM_FLOAT(0.3)) < 0) {
        em->irq_containment_breach = true;
    }
    
    return produced;
}

/* ===== Harmonic Renderer ===== */

uint32_t quantum_harmonic_create(quantum_system_t *qs,
                                  surplus_real_t frequency,
                                  surplus_real_t amplitude) {
    if (qs->num_harmonic >= 32) return 0xFFFFFFFF;
    harmonic_renderer_t *hr = &qs->harmonic_renderers[qs->num_harmonic];
    
    hr->reg_frequency = frequency;
    hr->reg_amplitude = amplitude;
    hr->reg_phase = SR_ZERO;
    hr->reg_harmonic_order = SR_ONE;
    hr->reg_resonance_q = SR_FROM_FLOAT(100.0);
    hr->num_harmonics = 1;
    hr->harmonics[0] = amplitude;
    
    /* M⁵ */
    hr->m5.omega = qs->num_harmonic;
    hr->m5.r = amplitude;
    hr->m5.ell = SR_ONE;
    hr->m5.phi = hr->reg_phase;
    hr->m5.chi = 0;
    hr->coverage_ratio = SR_ONE;
    
    /* DMA */
    hr->dma_head = 0;
    hr->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 32; i++) hr->dma_waveform[i] = 0;
    
    hr->irq_resonance_achieved = false;
    hr->irq_phase_locked = false;
    hr->active = true;
    
    return qs->num_harmonic++;
}

void quantum_harmonic_compute(harmonic_renderer_t *hr) {
    /* Compute harmonic series: A_n = A / n for odd harmonics
     * (square wave approximation) */
    uint32_t n;
    for (n = 0; n < hr->num_harmonics && n < 16; n++) {
        uint32_t order = n + 1;
        if (order % 2 == 1) {
            hr->harmonics[n] = SR_DIV(hr->reg_amplitude, SR_FROM_INT(order));
        } else {
            hr->harmonics[n] = SR_ZERO;
        }
    }
    
    /* Check resonance */
    if (SR_CMP(hr->reg_resonance_q, SR_FROM_FLOAT(50.0)) > 0) {
        hr->irq_resonance_achieved = true;
        hr->irq_phase_locked = true;
    }
}

void quantum_harmonic_add_resonance(harmonic_renderer_t *hr, uint32_t order) {
    if (hr->num_harmonics >= 16) return;
    surplus_real_t amp = SR_DIV(hr->reg_amplitude, SR_FROM_INT(order));
    hr->harmonics[hr->num_harmonics] = amp;
    hr->num_harmonics++;
    hr->reg_harmonic_order = SR_FROM_INT(hr->num_harmonics);
}

/* ===== Second Quantization ===== */

uint32_t quantum_field_create(quantum_system_t *qs, uint32_t num_modes) {
    if (qs->num_quant_fields >= 16) return 0xFFFFFFFF;
    if (num_modes > 64) num_modes = 64;
    
    second_quant_t *field = &qs->quant_fields[qs->num_quant_fields];
    field->num_modes = num_modes;
    field->total_particles = SR_ZERO;
    field->vacuum_energy = SR_ZERO;
    field->mode = QMODE_AC;
    
    uint32_t i;
    for (i = 0; i < 64; i++) {
        field->occupation[i] = 0;
        field->field_amplitude[i] = SR_ZERO;
        field->field_phase[i] = SR_ZERO;
    }
    
    /* M⁵ */
    field->m5.omega = qs->num_quant_fields;
    field->m5.r = SR_ONE;
    field->m5.ell = SR_ONE;
    field->m5.phi = SR_ZERO;
    field->m5.chi = 0;
    field->coverage_ratio = SR_ONE;
    
    field->irq_particle_created = false;
    field->irq_particle_annihilated = false;
    field->irq_vacuum_fluctuation = false;
    field->active = true;
    
    return qs->num_quant_fields++;
}

int32_t quantum_field_create_particle(second_quant_t *field, uint32_t mode) {
    if (mode >= field->num_modes) return -1;
    
    /* a†_k |n_k⟩ = √(n_k + 1) |n_k + 1⟩ */
    field->occupation[mode]++;
    field->total_particles = SR_ADD(field->total_particles, SR_ONE);
    
    /* Update field amplitude: √(n_k) */
    surplus_real_t sqrt_n = SR_SQRT(SR_FROM_INT(field->occupation[mode]));
    field->field_amplitude[mode] = sqrt_n;
    
    field->irq_particle_created = true;
    return 0;
}

int32_t quantum_field_annihilate_particle(second_quant_t *field, uint32_t mode) {
    if (mode >= field->num_modes) return -1;
    if (field->occupation[mode] == 0) return -1; /* Can't annihilate vacuum */
    
    /* a_k |n_k⟩ = √(n_k) |n_k - 1⟩ */
    field->occupation[mode]--;
    field->total_particles = SR_SUB(field->total_particles, SR_ONE);
    
    surplus_real_t sqrt_n = SR_SQRT(SR_FROM_INT(field->occupation[mode]));
    field->field_amplitude[mode] = sqrt_n;
    
    if (field->occupation[mode] == 0) {
        field->irq_vacuum_fluctuation = true;
    }
    field->irq_particle_annihilated = true;
    return 0;
}

surplus_real_t quantum_field_vacuum_energy(second_quant_t *field,
                                            const surplus_real_t *omega,
                                            uint32_t num_modes) {
    /* E_0 = Σ_k ℏω_k/2 (zero-point energy) */
    surplus_real_t total = SR_ZERO;
    uint32_t k;
    for (k = 0; k < num_modes && k < field->num_modes; k++) {
        surplus_real_t half_omega = SR_DIV(omega[k], SR_FROM_INT(2));
        total = SR_ADD(total, SR_MUL(Q_PLANCK_REDUCED, half_omega));
    }
    field->vacuum_energy = total;
    return total;
}

surplus_real_t quantum_field_expectation_value(const second_quant_t *field,
                                                uint32_t mode) {
    if (mode >= field->num_modes) return SR_ZERO;
    /* ⟨n_k|a†_k a_k|n_k⟩ = n_k */
    return SR_FROM_INT(field->occupation[mode]);
}

void quantum_system_update(quantum_system_t *qs) {
    /* Update system coherence from all active devices */
    surplus_real_t total = SR_ZERO;
    uint32_t count = 0;
    
    uint32_t i;
    for (i = 0; i < qs->num_casimir; i++) {
        if (qs->casimir_arrays[i].active) {
            total = SR_ADD(total, qs->casimir_arrays[i].coverage_ratio);
            count++;
        }
    }
    for (i = 0; i < qs->num_harmonic; i++) {
        if (qs->harmonic_renderers[i].active) {
            total = SR_ADD(total, qs->harmonic_renderers[i].coverage_ratio);
            count++;
        }
    }
    
    if (count > 0) {
        qs->system_coherence = SR_DIV(total, SR_FROM_INT(count));
    }
    
    /* Sum ZPE */
    qs->system_zpe_total = SR_ZERO;
    for (i = 0; i < qs->num_zpe; i++) {
        if (qs->zpe_extractors[i].active) {
            qs->system_zpe_total = SR_ADD(qs->system_zpe_total,
                                           qs->zpe_extractors[i].reg_total_extracted);
        }
    }
}
