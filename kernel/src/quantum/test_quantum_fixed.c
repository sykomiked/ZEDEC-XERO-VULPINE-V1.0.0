/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_quantum_fixed.c — the quantum device model in the kernel's Q32.32
 * build (no TEST_HOST), where the physical constants are in nm, fs and eV
 * (quantum_device.h). Before the unit change hbar was 1.05e-34 J.s through SR_FROM_FLOAT,
 * which rounds to 0 in Q32.32, so every Casimir force and zero-point energy
 * the kernel computed was 0 (adversarial audit 2026-10-12). */
#include <stdio.h>
#include "quantum_device.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* |got - want| <= want * ppm / 1e6, all in Q32.32 */
static int near(int64_t got, int64_t want, int64_t ppm)
{
    int64_t d = got > want ? got - want : want - got;
    int64_t w = want < 0 ? -want : want;
    return d <= w / 1000000 * ppm + 2;
}

int main(void)
{
    CHECK(Q_PLANCK_REDUCED != 0, "hbar is not zero in Q32.32");
    CHECK(Q_SPEED_OF_LIGHT != 0, "c is not zero in Q32.32");
    /* hbar*c = 197.3269804 eV*nm */
    int64_t hbarc = SR_MUL(Q_PLANCK_REDUCED, Q_SPEED_OF_LIGHT);
    CHECK(near(hbarc, SR_FROM_FLOAT(197.3269804), 10), "hbar*c = 197.327 eV*nm, got %lld",
          (long long) hbarc);

    /* Casimir pressure at a = 10 nm: pi^2 * hbar*c / (240 * 10^4) eV/nm^4 */
    int64_t f = quantum_casimir_force(SR_FROM_INT(10));
    CHECK(f > 0, "Casimir force at 10 nm is positive");
    CHECK(near(f, SR_FROM_FLOAT(9.869604401089358 * 197.3269804 / 2400000.0), 2000),
          "Casimir force at 10 nm = 8.115e-4 eV/nm^4, got %lld", (long long) f);
    /* a^-4 law: halving the gap multiplies the force by 16 */
    int64_t f5 = quantum_casimir_force(SR_FROM_INT(5));
    CHECK(near(f5, f * 16, 2000), "F(5 nm) = 16 F(10 nm)");
    CHECK(quantum_casimir_force(SR_ZERO) == SR_ZERO, "zero gap gives zero, not a trap");

    /* force per area / energy density = 720 / 240 = 3 */
    static quantum_system_t qs;
    quantum_system_init(&qs);
    uint32_t id = quantum_casimir_create(&qs, SR_FROM_INT(10), SR_FROM_INT(100), 1);
    CHECK(id == 0, "cavity created");
    casimir_array_t *cav = &qs.casimir_arrays[id];
    CHECK(cav->reg_energy_density > 0, "energy density is not zero");
    CHECK(near(cav->reg_casimir_force, SR_MUL(f, SR_FROM_INT(100)), 2000),
          "force = pressure x area");
    CHECK(near(SR_DIV(f, cav->reg_energy_density), SR_FROM_INT(3), 2000), "pressure / density = 3");

    /* zero-point energy: sum hbar*omega/2 over omega = 1, 2, 3 rad/fs = 3 hbar */
    static second_quant_t field;
    field.num_modes = 3;
    int64_t omega[3] = {SR_FROM_INT(1), SR_FROM_INT(2), SR_FROM_INT(3)};
    int64_t e0 = quantum_field_vacuum_energy(&field, omega, 3);
    CHECK(near(e0, Q_PLANCK_REDUCED * 3, 10), "E0 = 3 hbar = 1.9746 eV, got %lld", (long long) e0);

    printf("test_quantum_fixed: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
