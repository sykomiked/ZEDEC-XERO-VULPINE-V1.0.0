/* test_quantum_device.c — Functional correctness tests for the
 * Quantum & Exotic Matter Device Module (Casimir, ZPE, wormhole,
 * exotic matter, harmonic rendering, second quantization).
 *
 * These tests verify the module's internal math and state machines
 * are self-consistent and match their documented formulas. They do
 * NOT assert anything about physical realizability of the modeled
 * devices -- that is a separate, orthogonal question from "does the
 * code correctly implement the formulas/state machine it claims to."
 */
#include "quantum_device.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

int main(void) {
    /* ===== system_init ===== */
    quantum_system_t qs;
    quantum_system_init(&qs);
    assert(qs.num_casimir == 0);
    assert(qs.num_zpe == 0);
    assert(qs.num_wormholes == 0);
    assert(qs.num_exotic == 0);
    assert(qs.num_harmonic == 0);
    assert(qs.num_quant_fields == 0);
    assert(qs.system_coherence == SR_ONE);
    assert(qs.system_entanglement == SR_ZERO);
    assert(qs.system_zpe_total == SR_ZERO);

    /* ===== Casimir: bare force formula, independently recomputed ===== */
    {
        double a = 1e-6;
        double expected = (Q_PI_SQUARED * Q_PLANCK_REDUCED * Q_SPEED_OF_LIGHT)
                           / (240.0 * a * a * a * a);
        double got = quantum_casimir_force(a);
        assert(feq(got, expected, 1e-9));
        assert(quantum_casimir_force(0.0) == 0.0);
    }

    /* ===== Casimir: create/compute, force-energy density ratio ===== */
    {
        /* force_per_area / energy_density must be exactly 720/240 = 3,
         * independent of the coefficient's numeric value. */
        uint32_t id = quantum_casimir_create(&qs, 1e-6, 2.0, 4);
        assert(id == 0);
        casimir_array_t *cav = &qs.casimir_arrays[id];
        assert(cav->active);
        assert(cav->num_cavities == 4);
        double force_per_area = cav->reg_casimir_force / cav->reg_plate_area;
        assert(feq(force_per_area / cav->reg_energy_density, 3.0, 1e-9));
        assert(feq(cav->coverage_ratio, cav->reg_casimir_force / 1.8, 1e-12));

        /* Tiny separation -> force >> 1 -> threshold IRQ true */
        assert(cav->reg_casimir_force > 0.0);

        /* Large separation, tiny area -> force << 1 -> threshold IRQ false */
        uint32_t id2 = quantum_casimir_create(&qs, 1e-6, 1e-12, 1);
        casimir_array_t *cav2 = &qs.casimir_arrays[id2];
        assert(cav2->reg_casimir_force < 1.0);
        assert(cav2->irq_casimir_threshold == false);
        assert(cav->irq_casimir_threshold == (cav->reg_casimir_force > 1.0));
    }

    /* ===== Capacity limits (16 Casimir arrays total) ===== */
    {
        quantum_system_t qs_cap;
        quantum_system_init(&qs_cap);
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t id = quantum_casimir_create(&qs_cap, 1e-6, 1.0, 1);
            assert(id == i);
        }
        assert(quantum_casimir_create(&qs_cap, 1e-6, 1.0, 1) == 0xFFFFFFFF);
    }

    /* ===== ZPE extractor ===== */
    {
        uint32_t zid = quantum_zpe_create(&qs, 0);
        assert(zid == 0);
        zpe_extractor_t *zpe = &qs.zpe_extractors[zid];
        assert(zpe->reg_zpe_density == qs.casimir_arrays[0].reg_energy_density);
        assert(feq(zpe->reg_efficiency, 0.01, 1e-12));

        /* Invalid casimir id */
        assert(quantum_zpe_create(&qs, 999) == 0xFFFFFFFF);

        double running_total = 0.0;
        double dts[3] = {1.0, 2.0, 0.5};
        for (int i = 0; i < 3; i++) {
            double rate = quantum_zpe_extract(zpe, dts[i]);
            double expected_rate = zpe->reg_efficiency * zpe->reg_zpe_density * dts[i];
            assert(feq(rate, expected_rate, 1e-9));
            running_total += expected_rate;
            assert(feq(zpe->reg_total_extracted, running_total, 1e-9));
        }
        assert(zpe->irq_extraction_active);

        /* DMA ring wraps mod 8 */
        for (int i = 0; i < 6; i++) quantum_zpe_extract(zpe, 1.0); /* total 9 calls */
        assert(zpe->dma_tail == 1);
    }

    /* ===== Wormhole throat ===== */
    {
        /* Large radius: coverage check passes, throat opens */
        uint32_t wid = quantum_wormhole_create(&qs, 10.0, 1, 2);
        wormhole_throat_t *wh = &qs.wormholes[wid];
        assert(wh->endpoint_a_id == 1 && wh->endpoint_b_id == 2);
        assert(feq(wh->reg_stability, 0.5, 1e-12));
        assert(quantum_wormhole_open(wh));
        assert(wh->irq_throat_open);
        assert(feq(wh->reg_traversable, 1.0, 1e-12));

        /* Small radius: coverage check fails (r*ell < 1.8), can't open */
        uint32_t wid2 = quantum_wormhole_create(&qs, 0.1, 3, 4);
        wormhole_throat_t *wh2 = &qs.wormholes[wid2];
        assert(!quantum_wormhole_open(wh2));
        assert(wh2->irq_instability);
        assert(quantum_wormhole_transit(wh2, 99) == -1);

        /* Transit degrades stability by 0.1 per trip, floors at 0,
         * and ring-buffers dma_transit mod 4 */
        for (int i = 0; i < 10; i++) {
            int32_t r = quantum_wormhole_transit(wh, (uint32_t)i);
            assert(r == 0);
        }
        assert(wh->reg_stability == 0.0); /* floored, never negative */
        assert(wh->dma_tail == (10 % 4));
    }

    /* ===== Exotic matter ===== */
    {
        uint32_t wid = 0; /* reuse open wormhole from above */
        uint32_t eid = quantum_exotic_create(&qs, wid);
        assert(eid == 0);
        exotic_matter_t *em = &qs.exotic_generators[eid];
        assert(feq(em->reg_negative_energy, -0.0001, 1e-12));
        assert(quantum_exotic_create(&qs, 999) == 0xFFFFFFFF);

        double energy = em->reg_negative_energy;
        for (int i = 0; i < 5; i++) {
            double produced = quantum_exotic_produce(em, 2.0);
            double expected_produced = em->reg_production_rate * 2.0;
            assert(feq(produced, expected_produced, 1e-9));
            energy -= produced;
            assert(feq(em->reg_negative_energy, energy, 1e-9));
        }
        assert(em->irq_production_active);
        /* Containment is never modified anywhere post-init in the
         * current implementation, so breach never fires from produce()
         * alone -- documenting actual behavior. */
        assert(!em->irq_containment_breach);
    }

    /* ===== Harmonic renderer ===== */
    {
        uint32_t hid = quantum_harmonic_create(&qs, 440.0, 8.0);
        harmonic_renderer_t *hr = &qs.harmonic_renderers[hid];
        assert(hr->num_harmonics == 1);
        assert(feq(hr->harmonics[0], 8.0, 1e-12));

        quantum_harmonic_add_resonance(hr, 2);
        quantum_harmonic_add_resonance(hr, 3);
        quantum_harmonic_add_resonance(hr, 4);
        assert(hr->num_harmonics == 4);
        assert(feq(hr->reg_harmonic_order, 4.0, 1e-12));
        assert(feq(hr->harmonics[1], 8.0 / 2.0, 1e-12));
        assert(feq(hr->harmonics[2], 8.0 / 3.0, 1e-12));
        assert(feq(hr->harmonics[3], 8.0 / 4.0, 1e-12));

        /* compute() recomputes by *index* parity (odd index+1 = odd
         * order): index 0->order1(odd)=A, index1->order2(even)=0,
         * index2->order3(odd)=A/3, index3->order4(even)=0 */
        quantum_harmonic_compute(hr);
        assert(feq(hr->harmonics[0], 8.0, 1e-12));
        assert(hr->harmonics[1] == 0.0);
        assert(feq(hr->harmonics[2], 8.0 / 3.0, 1e-12));
        assert(hr->harmonics[3] == 0.0);
        assert(hr->irq_resonance_achieved);
        assert(hr->irq_phase_locked);

        /* Capacity: 32 harmonic renderers total */
        quantum_system_t qs_cap;
        quantum_system_init(&qs_cap);
        for (uint32_t i = 0; i < 32; i++) {
            assert(quantum_harmonic_create(&qs_cap, 1.0, 1.0) == i);
        }
        assert(quantum_harmonic_create(&qs_cap, 1.0, 1.0) == 0xFFFFFFFF);
    }

    /* ===== Second quantization ===== */
    {
        uint32_t fid = quantum_field_create(&qs, 70); /* clamps to 64 */
        second_quant_t *field = &qs.quant_fields[fid];
        assert(field->num_modes == 64);

        uint32_t fid2 = quantum_field_create(&qs, 4);
        second_quant_t *f = &qs.quant_fields[fid2];
        assert(f->num_modes == 4);

        assert(quantum_field_create_particle(f, 0) == 0);
        assert(quantum_field_create_particle(f, 0) == 0);
        assert(quantum_field_create_particle(f, 0) == 0);
        assert(f->occupation[0] == 3);
        assert(feq(f->field_amplitude[0], sqrt(3.0), 1e-12));
        assert(quantum_field_create_particle(f, 99) == -1); /* out of range */

        assert(quantum_field_annihilate_particle(f, 0) == 0);
        assert(f->occupation[0] == 2);
        assert(feq(f->field_amplitude[0], sqrt(2.0), 1e-12));
        assert(!f->irq_vacuum_fluctuation);

        assert(quantum_field_annihilate_particle(f, 0) == 0);
        assert(quantum_field_annihilate_particle(f, 0) == 0);
        assert(f->occupation[0] == 0);
        assert(f->irq_vacuum_fluctuation); /* fires exactly at 0 */
        assert(quantum_field_annihilate_particle(f, 0) == -1); /* can't go below vacuum */

        assert(quantum_field_expectation_value(f, 0) == 0);
        quantum_field_create_particle(f, 1);
        quantum_field_create_particle(f, 1);
        assert(quantum_field_expectation_value(f, 1) == 2);
        assert(quantum_field_expectation_value(f, 99) == SR_ZERO); /* out of range */

        double omega[3] = {2.0, 4.0, 6.0};
        double expected_e0 = Q_PLANCK_REDUCED * (1.0 + 2.0 + 3.0);
        double e0 = quantum_field_vacuum_energy(f, omega, 3);
        assert(feq(e0, expected_e0, 1e-9));
        assert(feq(f->vacuum_energy, expected_e0, 1e-9));

        /* Capacity: 16 quantization fields total (2 already used above) */
        quantum_system_t qs_cap;
        quantum_system_init(&qs_cap);
        for (uint32_t i = 0; i < 16; i++) {
            assert(quantum_field_create(&qs_cap, 1) == i);
        }
        assert(quantum_field_create(&qs_cap, 1) == 0xFFFFFFFF);
    }

    /* ===== System-wide update: coherence averaging + ZPE summation ===== */
    {
        quantum_system_t sys;
        quantum_system_init(&sys);

        uint32_t c0 = quantum_casimir_create(&sys, 1e-6, 1.0, 1);
        uint32_t c1 = quantum_casimir_create(&sys, 2e-6, 1.0, 1);
        double cov0 = sys.casimir_arrays[c0].coverage_ratio;
        double cov1 = sys.casimir_arrays[c1].coverage_ratio;

        uint32_t hid = quantum_harmonic_create(&sys, 1.0, 1.0);
        double covh = sys.harmonic_renderers[hid].coverage_ratio;

        /* Inactive device must be excluded from the average */
        uint32_t c2 = quantum_casimir_create(&sys, 3e-6, 1.0, 1);
        sys.casimir_arrays[c2].active = false;

        quantum_system_update(&sys);
        double expected_coherence = (cov0 + cov1 + covh) / 3.0;
        assert(feq(sys.system_coherence, expected_coherence, 1e-9));

        uint32_t z0 = quantum_zpe_create(&sys, c0);
        uint32_t z1 = quantum_zpe_create(&sys, c1);
        quantum_zpe_extract(&sys.zpe_extractors[z0], 5.0);
        quantum_zpe_extract(&sys.zpe_extractors[z1], 3.0);
        double expected_zpe = sys.zpe_extractors[z0].reg_total_extracted
                             + sys.zpe_extractors[z1].reg_total_extracted;

        /* Inactive ZPE extractor must be excluded */
        uint32_t z2 = quantum_zpe_create(&sys, c0);
        quantum_zpe_extract(&sys.zpe_extractors[z2], 100.0);
        sys.zpe_extractors[z2].active = false;

        quantum_system_update(&sys);
        assert(feq(sys.system_zpe_total, expected_zpe, 1e-9));
    }

    /* ===== Name lookups ===== */
    {
        assert(strcmp(quantum_device_name(QDEV_CASIMIR_CAVITY), "Casimir Cavity") == 0);
        assert(strcmp(quantum_device_name(QDEV_WORMHOLE_THROAT), "Wormhole Throat") == 0);
        assert(strcmp(quantum_device_name(QDEV_PHOTONIC_LATTICE), "Photonic Lattice") == 0);
        assert(strcmp(quantum_device_name(QDEV_MAX), "Unknown") == 0);

        assert(strcmp(quantum_mode_name(QMODE_DC), "DC (Measurement)") == 0);
        assert(strcmp(quantum_mode_name(QMODE_PC), "PC (Entanglement)") == 0);
        assert(strcmp(quantum_mode_name((quantum_mode_t)3), "Unknown") == 0);
    }

    printf("All Quantum Device tests passed\n");
    return 0;
}
