/* test_epu_device.c — the EPU MODEL against hand-computed values.
 *
 * The anchors here are numbers worked out on paper from the constants in
 * epu_device.h, not values read back out of the implementation:
 *
 *   - the Terfenol-D coercive drive is  V_c = H_c * mu0 * d / alpha
 *     = 160 * 1.2566370614359173e-6 * 100e-9 / 1e-9 = 20.106 mV, so 20 mV
 *     must produce NO magnetic response and 21 mV must produce one;
 *   - a coil at the 100 mA limit produces mu0*1597*0.1/(2*0.01)
 *     = 10.034 mT at its centre;
 *   - an activated device draws 12 + 256*0.25 = 76.000 mW and therefore
 *     sits at 300 + 76*0.05 = 303.800 K, which shifts a 528 Hz crystal
 *     layer to 528*(1 - 1e-4*3.8) = 527.79936 Hz;
 *   - full magnetoelectric coupling rotates the emotion vector by exactly
 *     90 degrees, so joy <- -awe and awe <- joy;
 *   - all eight coils at the limit give a gain of exactly phi;
 *   - 0.999^106 = 0.89938 is the first gate count below the 0.90 fidelity
 *     floor, so 104 gates must NOT raise the decoherence IRQ and 105 must.
 *
 * The measurement tests do not merely check that measure() "worked". They
 * check that a fixed seed yields an exact, reproducible count of ones,
 * that a different seed yields a different count (so the PRNG is really
 * being consulted), that the frequency tracks the Born-rule probability,
 * and that re-measuring a collapsed qubit consumes no randomness at all.
 *
 * Coverage and health are driven to FAILURE on purpose, because a check
 * that cannot fail proves nothing.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "epu_device.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c,m) do{ checks++; if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static int near(double a, double b, double tol) {
    double d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

/* NaN and the infinities by bit pattern, so the test does not need math.h
 * and does not have to perform 0.0/0.0 (which UBSan objects to). */
static double bits2d(uint64_t b) {
    union { uint64_t u; double d; } x;
    x.u = b;
    return x.d;
}
#define D_NAN   bits2d(0x7FF8000000000000ULL)
#define D_INF   bits2d(0x7FF0000000000000ULL)
#define D_NINF  bits2d(0xFFF0000000000000ULL)

/* ---- INDEPENDENT reference PRNG ----
 * Written from the published SplitMix64 algorithm (Steele/Lea/Flood 2014),
 * not copied from epu_device.c's call sites. It exists so the exact
 * measurement counts asserted below are RE-DERIVED here from the algorithm
 * and the Born-rule probability, instead of being magic numbers lifted out
 * of a previous run of the implementation. If epu_qubit_measure() stopped
 * drawing, or drew a different number of times per call, or compared
 * against the wrong probability, these two counts would diverge. */
static uint64_t ref_splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
/* One fresh Born draw per trial against a fixed P(1). */
static int ref_ones(uint64_t seed, int n, double p1) {
    uint64_t s = seed;
    int ones = 0;
    for (int t = 0; t < n; t++) {
        double u = (double)(ref_splitmix64(&s) >> 11) * (1.0 / 9007199254740992.0);
        if (u < p1) ones++;
    }
    return ones;
}

static double f64at(const uint8_t *p) {
    union { double d; uint8_t b[8]; } u;
    for (int i = 0; i < 8; i++) u.b[i] = p[i];
    return u.d;
}
static uint32_t u32at(const uint8_t *p) {
    union { uint32_t w; uint8_t b[4]; } u;
    for (int i = 0; i < 4; i++) u.b[i] = p[i];
    return u.w;
}

/* digital root of a positive integer */
static uint32_t droot(uint64_t n) {
    if (n == 0) return 0;
    return (uint32_t)(1u + (uint32_t)((n - 1u) % 9u));
}

static double qnorm(const epu_qubit_t *q) {
    return q->a_re*q->a_re + q->a_im*q->a_im + q->b_re*q->b_re + q->b_im*q->b_im;
}
static double qp1(const epu_qubit_t *q) {
    return q->b_re*q->b_re + q->b_im*q->b_im;
}

/* ---- capture sink for epu_diagnostic_dump ---- */
typedef struct { char buf[8192]; unsigned len; } cap_t;
static void cap_write(void *ctx, const char *s) {
    cap_t *c = (cap_t *)ctx;
    for (unsigned i = 0; s[i] && c->len + 1 < sizeof(c->buf); i++)
        c->buf[c->len++] = s[i];
    c->buf[c->len] = '\0';
}

/* Big structs live in .bss so the sanitiser stack stays small. */
static epu_system_t sys;
static epu_device_t dev;
static epu_device_t dev2;
static cap_t cap;

/* A fixed op sequence that touches every mutable corner of the model.
 * Used to show the model is a pure function of (init inputs, seed): two
 * devices driven identically must end up BYTE-identical, which is only
 * possible if nothing here consults a clock, a device, or a global. */
static void epu_exercise(epu_device_t *d) {
    emotion_vector_t v = {1.0, -2.0, 3.0, 4.0, -5.0};
    uint32_t ids[EPU_EMOTION_DIMS];
    epu_device_activate(d);
    for (uint32_t i = 0; i < 64; i++) epu_cell_stimulate(d, i, 10.0);
    for (uint32_t i = 0; i < 8; i++) epu_coil_activate(d, i, 0.05);
    epu_coil_set_golden_ratio(d, 3, 1.3);
    epu_cell_set_resonance(d, 9, 2.0 * EPU_RESONANCE_HZ);
    epu_cell_update_spiral(d, 9, 4.0 * EPU_GOLDEN_ANGLE_RAD);
    epu_crystal_activate(d, 1, SOLFEGGIO_639);
    epu_set_vortex_frequency(d, VORTEX_369);
    for (uint32_t i = 0; i < 40; i++)
        epu_qubit_apply_gate(d, i, (uint8_t)(i % EPU_NUM_GATES));
    epu_qubit_entangle(d, 100, 101);
    for (uint32_t i = 0; i < 40; i++) (void)epu_qubit_measure(d, i);
    (void)epu_qubit_measure(d, 100);
    (void)epu_process_emotion(d, &v);
    (void)epu_emotion_to_quantum(d, &v, ids);
    epu_handle_irq(d);
    (void)epu_verify_coverage(d);
    (void)epu_get_system_health(d);
}

int main(void) {
    printf("=== EPU MODEL (simulation only; drives no hardware) ===\n");

    /* =============== 1. system + device lifecycle =============== */
    epu_system_init(&sys);
    CHECK(sys.num_devices == 0 && sys.system_fidelity == 0.0,
          "a fresh system holds no devices and claims no fidelity");
    CHECK(epu_system_create_device(&sys, "epu0") == 1, "first device gets id 1");
    CHECK(epu_system_create_device(&sys, "epu1") == 2, "second device gets id 2");
    epu_system_create_device(&sys, "epu2");
    epu_system_create_device(&sys, "epu3");
    CHECK(sys.num_devices == 4, "four devices fit");
    CHECK(epu_system_create_device(&sys, "epu4") == 0,
          "the fifth device is refused with id 0, not silently dropped");
    CHECK(near(sys.system_coherence_us, 16180.0 * 144.0, 1e-6),
          "system coherence = 144 qubits x 16180 us = 2329920 us");
    CHECK(near(sys.system_fidelity, 0.999, 1e-12), "system fidelity = 0.999");
    CHECK(epu_system_create_device(0, "x") == 0, "NULL system yields id 0");

    /* =============== 2. device init is fully populated =============== */
    epu_device_init(&dev, 7, "unit-under-test");
    CHECK(dev.device_id == 7 && strcmp(dev.name, "unit-under-test") == 0,
          "identity is stored");
    CHECK(!dev.bus_active, "a device comes up INACTIVE, not magically running");
    CHECK(dev.power_consumption_mw == 0.0 && dev.operating_temp_k == 300.0,
          "an inactive device draws 0 mW and sits at ambient 300 K");
    CHECK(near(dev.total_coherence_us, 2329920.0, 1e-6),
          "total coherence is the SUM over qubits, 2329920 us");
    CHECK(near(dev.emotion_throughput, 1.0e12 / 512.0, 1e-3),
          "throughput ceiling = 1 THz / 512 bits = 1953125000 (derived, not measured)");
    CHECK(dev.cells[0].spiral_r == 0.0 && near(dev.cells[1].spiral_r, 1.0, 1e-12)
          && near(dev.cells[4].spiral_r, 2.0, 1e-12),
          "phyllotaxis placement: r_i = sqrt(i), so cells 0/1/4 sit at 0/1/2");
    CHECK(near(dev.cells[1].spiral_theta, 2.3999632297286533, 1e-12),
          "cell 1 sits one golden angle round, 2.39996 rad");
    CHECK(near(dev.qubits[0].t1_time_us, 16180.0 * 1.6180339887498949, 1e-9),
          "T1 = phi * T2 = 26179.79 us (so T2 <= 2*T1 holds)");
    CHECK(dev.qubits[143].fib_row == 11 && dev.qubits[143].fib_col == 11,
          "qubit 143 is at row 11, col 11 of the 12x12 matrix");
    CHECK(near(dev.crystal_layers[1].nanocrystal_density, 1.0e12 * 0.6180339887498949, 1.0),
          "layer 1 density = base * phi^-1 = 6.1803e11 crystals/cm^2");
    CHECK(dev.coils[3].turns == 1597 && dev.coils[3].golden_ratio_field == 1.0,
          "coils carry F17 = 1597 turns with unity shaping");
    CHECK(!dev.coils[0].superconducting,
          "no coil claims to be superconducting, because no such material exists");

    /* =============== 3. activation, power, temperature =============== */
    CHECK(epu_device_activate(&dev) == EPU_OK, "activation succeeds once");
    CHECK(epu_device_activate(&dev) == EPU_ERR_BUSY,
          "a second activation is refused, not reported as success");
    CHECK(dev.power_consumption_mw == 76.0,
          "power = 12 mW bus + 256 x 0.25 mW cells = 76.000 mW exactly");
    CHECK(near(dev.operating_temp_k, 303.8, 1e-12),
          "temperature = 300 + 76 x 0.05 = 303.800 K");
    CHECK(near(epu_get_system_health(&dev), 0.9974666666666666, 1e-12),
          "health of a healthy activated device = 0.99746667");
    CHECK(epu_device_deactivate(&dev) == EPU_OK, "deactivation succeeds once");
    CHECK(epu_device_deactivate(&dev) == EPU_ERR_BUSY, "a second deactivation is refused");
    CHECK(dev.power_consumption_mw == 0.0 && dev.operating_temp_k == 300.0,
          "a deactivated device is back to 0 mW / 300 K");
    CHECK(epu_device_activate(0) == EPU_ERR_NULL, "NULL device -> EPU_ERR_NULL");
    epu_device_activate(&dev);

    /* =============== 4. cells: the coercivity gate is real =============== */
    CHECK(epu_cell_stimulate(&dev, 0, 10.0) == EPU_OK, "10 V drive is accepted");
    CHECK(near(dev.cells[0].terfenol_field, 0.1, 1e-15),
          "B = alpha*E = 1e-9 * (10/100nm) = 0.100 T");
    CHECK(near(dev.cells[0].me_coupling, 1.0e-9, 1e-21),
          "coupling saturates at alpha_max = 1e-9 s/m at the 10 V limit");
    CHECK(dev.cells[0].cycle_count == 1, "one drive cycle was counted");

    CHECK(epu_cell_stimulate(&dev, 0, 0.020) == EPU_OK_SUBCOERCIVE,
          "20 mV is BELOW the 20.106 mV coercive point -> EPU_OK_SUBCOERCIVE, not OK");
    CHECK(dev.cells[0].terfenol_field == 0.0 && dev.cells[0].me_coupling == 0.0,
          "and no magnetic response is recorded for it");
    CHECK(dev.cells[0].pzt_voltage == 0.020 && dev.cells[0].cycle_count == 2,
          "the electrical drive DID happen, so the voltage and the cycle count stand");

    CHECK(epu_cell_stimulate(&dev, 0, 0.021) == EPU_OK,
          "21 mV is ABOVE the coercive point -> the domain switches");
    CHECK(near(dev.cells[0].me_coupling, 2.1e-12, 1e-24),
          "coupling at 21 mV = 1e-9 * 0.021/10 = 2.1e-12 s/m");

    CHECK(epu_cell_stimulate(&dev, 0, 11.0) == EPU_ERR_RANGE, "11 V exceeds the +/-10 V limit");
    CHECK(dev.cells[0].pzt_voltage == 0.021 && dev.cells[0].cycle_count == 3,
          "a refused drive changes nothing and counts nothing");
    CHECK(epu_cell_stimulate(&dev, 256, 1.0) == EPU_ERR_RANGE, "cell 256 is out of range");
    CHECK(epu_cell_stimulate(0, 0, 1.0) == EPU_ERR_NULL, "NULL device -> EPU_ERR_NULL");
    CHECK(near(epu_cell_read_coupling(&dev, 0), 2.1e-12, 1e-24), "read-back matches");
    CHECK(epu_cell_read_coupling(&dev, 999) == EPU_BAD_READING,
          "a bad cell index reads back the negative sentinel, not 0.0");

    /* =============== 5. resonance and Q =============== */
    CHECK(epu_cell_set_resonance(&dev, 0, EPU_RESONANCE_HZ) == EPU_OK, "nominal resonance is accepted");
    CHECK(near(dev.cells[0].q_factor, 1597.0, 1e-9), "Q at f0 is the full 1597");
    CHECK(epu_cell_set_resonance(&dev, 0, 2.0 * EPU_RESONANCE_HZ) == EPU_OK, "one octave up is in band");
    CHECK(near(dev.cells[0].q_factor, 1597.0 / (1.0 + 1.6180339887498949), 1e-9),
          "Q at 2*f0 falls to 1597/(1+phi) = 609.9997");
    CHECK(epu_cell_set_resonance(&dev, 0, 1.0) == EPU_ERR_RANGE, "1 Hz is out of band");
    CHECK(epu_cell_set_resonance(&dev, 0, 1.0e9) == EPU_ERR_RANGE, "1 GHz is out of band");
    CHECK(dev.cells[0].resonance_freq == 2.0 * EPU_RESONANCE_HZ,
          "the refused retunes left the resonance where it was");

    /* =============== 6. spiral update =============== */
    epu_cell_update_spiral(&dev, 3, EPU_GOLDEN_ANGLE_RAD);
    CHECK(near(dev.cells[3].spiral_r, 1.0, 1e-12), "one golden angle -> r = 1");
    epu_cell_update_spiral(&dev, 3, 4.0 * EPU_GOLDEN_ANGLE_RAD);
    CHECK(near(dev.cells[3].spiral_r, 2.0, 1e-12), "four golden angles -> r = 2");
    epu_cell_update_spiral(&dev, 3, -5.0);
    CHECK(dev.cells[3].spiral_theta == 0.0 && dev.cells[3].spiral_r == 0.0,
          "a negative angle clamps to the origin");
    epu_cell_update_spiral(&dev, 3, 1.0e9);
    CHECK(near(dev.cells[3].spiral_r, 16.0, 1e-9),
          "a huge angle clamps to the 256th turn, r = sqrt(256) = 16");

    /* =============== 7. harmonic series =============== */
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_528, 0) == 528.0, "528 Hz at octave 0");
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_528, 1) == 1056.0, "octave 1 doubles to 1056");
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_528, 3) == 4224.0, "octave 3 is 528*8 = 4224");
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_174, 0) == 174.0, "the table starts at 174");
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_963, 8) == 963.0 * 256.0, "963 at octave 8");
    CHECK(epu_compute_solfeggio_harmonic(SOLFEGGIO_963, 21) == 0.0, "octave 21 is refused with 0.0");
    CHECK(epu_compute_solfeggio_harmonic((solfeggio_freq_t)99, 0) == 0.0,
          "an out-of-range solfeggio index yields 0.0");
    CHECK(epu_compute_vortex_harmonic(VORTEX_3, 0) == 3.0, "vortex base 3");
    CHECK(epu_compute_vortex_harmonic(VORTEX_369, 0) == 369.0, "vortex base 369");
    CHECK(epu_compute_vortex_harmonic(VORTEX_9, 3) == 72.0, "9 doubled three times is 72");
    CHECK(epu_compute_vortex_harmonic(VORTEX_3, 33) == 0.0, "harmonic 33 is refused with 0.0");
    CHECK(epu_compute_vortex_harmonic((vortex_freq_t)-1, 0) == 0.0,
          "a negative vortex index yields 0.0");
    {
        int alt_ok = 1, nine_ok = 1;
        for (uint32_t k = 0; k <= 20; k++) {
            uint32_t d3 = droot((uint64_t)epu_compute_vortex_harmonic(VORTEX_3, k));
            uint32_t d9 = droot((uint64_t)epu_compute_vortex_harmonic(VORTEX_9, k));
            if (d3 != ((k % 2 == 0) ? 3u : 6u)) alt_ok = 0;
            if (d9 != 9u) nine_ok = 0;
        }
        CHECK(alt_ok, "the 3-family digital roots really alternate 3,6,3,6 over 21 doublings");
        CHECK(nine_ok, "the 9-family digital root is invariably 9 over 21 doublings");
    }

    /* =============== 8. emotion algebra =============== */
    {
        emotion_vector_t a = {3.0, 4.0, 0.0, 0.0, 0.0};
        emotion_vector_t z = {0.0, 0.0, 0.0, 0.0, 0.0};
        CHECK(epu_compute_emotion_intensity(&a) == 5.0, "|(3,4,0,0,0)| = 5 exactly");
        CHECK(epu_compute_emotion_intensity(&z) == 0.0, "the null emotion has intensity 0");
        CHECK(epu_compute_emotion_intensity(0) == 0.0, "NULL vector -> 0.0");
    }

    epu_device_init(&dev, 1, "emotion");
    epu_device_activate(&dev);
    {
        emotion_vector_t in = {1.0, 2.0, 3.0, 4.0, 5.0};
        emotion_vector_t out = epu_process_emotion(&dev, &in);
        CHECK(out.joy == 1.0 && out.love == 2.0 && out.serenity == 3.0
              && out.awe == 4.0 && out.gratitude == 5.0,
              "with zero coupling and no field the transform is the identity");
        CHECK(dev.emotions_processed == 1 && dev.irq_emotion_ready,
              "one emotion was counted and the ready flag was raised");
        CHECK(dev.tx_head == 64 && dev.tx_tail == 0 && dev.rx_head == 64,
              "the trace ring advanced by exactly one 64-byte frame");
        CHECK(f64at(dev.tx_buffer + 0) == 1.0 && f64at(dev.tx_buffer + 32) == 5.0,
              "the tx frame really contains the submitted vector");
        CHECK(near(f64at(dev.tx_buffer + 40), 7.416198487095663, 1e-12),
              "the tx frame carries the intensity sqrt(55) = 7.4161985");
        CHECK(u32at(dev.tx_buffer + 48) == 0 && u32at(dev.tx_buffer + 52) == 1,
              "the frame is stamped with sequence 0 and device id 1");

        /* full coupling -> exactly 90 degrees */
        for (uint32_t i = 0; i < 256; i++) epu_cell_stimulate(&dev, i, 10.0);
        out = epu_process_emotion(&dev, &in);
        CHECK(near(out.joy, -4.0, 1e-13) && near(out.awe, 1.0, 1e-13),
              "at full coupling the mind pair rotates 90 deg: joy <- -awe, awe <- joy");
        CHECK(near(out.love, -5.0, 1e-13) && near(out.gratitude, 2.0, 1e-13),
              "and the heart pair likewise: love <- -gratitude, gratitude <- love");
        CHECK(out.serenity == 3.0, "serenity is the rotation axis and is untouched");
        CHECK(near(epu_compute_emotion_intensity(&out),
                   epu_compute_emotion_intensity(&in), 1e-12),
              "the rotation preserves the norm exactly (it is a Givens rotation)");

        /* half the array driven -> 45 degrees */
        for (uint32_t i = 0; i < 256; i++) epu_cell_stimulate(&dev, i, 0.0);
        for (uint32_t i = 0; i < 128; i++) epu_cell_stimulate(&dev, i, 10.0);
        out = epu_process_emotion(&dev, &in);
        CHECK(near(out.joy, -2.121320343559643, 1e-12),
              "half the array driven -> 45 deg -> joy = (1-4)/sqrt(2) = -2.1213203");
        CHECK(near(out.awe, 3.5355339059327378, 1e-12),
              "and awe = (1+4)/sqrt(2) = 3.5355339");

        CHECK(dev.emotions_processed == 3, "exactly three emotions have been processed");

        /* refusals must not count */
        emotion_vector_t zero_out = epu_process_emotion(0, &in);
        CHECK(zero_out.joy == 0.0 && zero_out.gratitude == 0.0,
              "a NULL device returns the zero vector");
        zero_out = epu_process_emotion(&dev, 0);
        CHECK(zero_out.joy == 0.0, "a NULL input returns the zero vector");
        epu_device_deactivate(&dev);
        zero_out = epu_process_emotion(&dev, &in);
        CHECK(zero_out.joy == 0.0 && dev.emotions_processed == 3,
              "an inactive device processes nothing AND counts nothing");
        epu_device_activate(&dev);
    }

    /* ring wrap */
    epu_device_init(&dev, 1, "ring");
    epu_device_activate(&dev);
    {
        emotion_vector_t in = {1.0, 0.0, 0.0, 0.0, 0.0};
        for (int i = 0; i < 64; i++) epu_process_emotion(&dev, &in);
        CHECK(dev.tx_head == 0 && dev.tx_tail == 0,
              "after 64 frames the ring is exactly full (head wraps to 0)");
        epu_process_emotion(&dev, &in);
        CHECK(dev.tx_head == 64 && dev.tx_tail == 64,
              "the 65th frame overwrites the oldest, so tail follows head");
        CHECK(u32at(dev.tx_buffer + 48) == 64, "slot 0 now holds sequence 64");
    }

    /* =============== 9. coils =============== */
    epu_device_init(&dev, 2, "coils");
    epu_device_activate(&dev);
    {
        const double BMAX = 1.2566370614359173e-6 * 1597.0 * 0.1 / 0.02;
        CHECK(near(BMAX, 0.0100342469355658, 1e-15), "hand-computed B at the limit is 10.0342469 mT");
        CHECK(epu_coil_activate(&dev, 0, 0.1) == EPU_OK, "100 mA is accepted");
        CHECK(near(dev.coils[0].field_strength, BMAX, 1e-15),
              "B = mu0*N*I/(2R) = 10.034 mT at the coil centre");
        CHECK(near(epu_coil_compute_field(&dev, 0), BMAX, 1e-15), "read-back agrees");
        CHECK(epu_coil_compute_field(&dev, 8) == EPU_BAD_READING,
              "coil 8 does not exist and reads the negative sentinel");
        CHECK(epu_coil_activate(&dev, 0, 0.2) == EPU_ERR_RANGE, "200 mA exceeds the limit");
        CHECK(dev.coils[0].current == 0.1, "and the refused current did not stick");
        CHECK(epu_coil_activate(&dev, 8, 0.05) == EPU_ERR_RANGE, "coil 8 is refused");
        CHECK(near(dev.power_consumption_mw, 76.5, 1e-12),
              "one coil at 100 mA adds I^2R = 0.5 mW -> 76.500 mW");

        dev.coils[0].superconducting = true;
        epu_coil_activate(&dev, 0, 0.1);
        CHECK(near(dev.power_consumption_mw, 76.0, 1e-12),
              "flagging that coil superconducting removes its I^2R term -> 76.000 mW");
        dev.coils[0].superconducting = false;

        for (uint32_t i = 0; i < 8; i++) epu_coil_activate(&dev, i, 0.1);
        CHECK(near(dev.power_consumption_mw, 80.0, 1e-12),
              "all eight coils at the limit cost 4 mW -> 80.000 mW");

        emotion_vector_t in = {1.0, 0.0, 0.0, 0.0, 0.0};
        emotion_vector_t out = epu_process_emotion(&dev, &in);
        CHECK(near(out.joy, 1.6180339887498949, 1e-12),
              "with the field saturated the emotion gain is exactly phi");

        epu_coil_set_golden_ratio(&dev, 0, 100.0);
        CHECK(near(dev.coils[0].golden_ratio_field, 2.618033988749895, 1e-12),
              "the shaping factor clamps to phi^2 = 2.618034");
        CHECK(near(dev.coils[0].field_strength, BMAX * 2.618033988749895, 1e-15),
              "and the field really scales with it");
        epu_coil_set_golden_ratio(&dev, 0, -5.0);
        CHECK(near(dev.coils[0].golden_ratio_field, 0.3819660112501051, 1e-12),
              "a negative shaping factor clamps to phi^-2 = 0.381966");
        epu_coil_set_golden_ratio(&dev, 0, 1.0);
    }

    /* =============== 10. crystal layers =============== */
    epu_device_init(&dev, 3, "crystal");
    CHECK(epu_crystal_activate(&dev, 0, SOLFEGGIO_528) == EPU_ERR_INACTIVE,
          "an inactive device cannot light a crystal layer");
    epu_device_activate(&dev);
    CHECK(epu_crystal_activate(&dev, 0, SOLFEGGIO_528) == EPU_OK, "layer 0 lights at 528 Hz");
    CHECK(near(dev.crystal_layers[0].piezo_response, 528.0 / 432.0, 1e-12),
          "layer 0 piezo response = 528/432 = 1.2222222");
    CHECK(epu_crystal_activate(&dev, 1, SOLFEGGIO_528) == EPU_OK, "layer 1 lights too");
    CHECK(near(dev.crystal_layers[1].piezo_response, 0.6180339887498949 * 528.0 / 432.0, 1e-12),
          "layer 1 has phi^-1 of the material so 0.7553751 of the response");
    CHECK(epu_crystal_activate(&dev, 4, SOLFEGGIO_528) == EPU_ERR_RANGE, "layer 4 does not exist");
    CHECK(epu_crystal_activate(&dev, 0, (solfeggio_freq_t)99) == EPU_ERR_RANGE,
          "an out-of-range solfeggio index is refused");
    CHECK(epu_crystal_get_resonance(&dev, 2) == 0.0,
          "an unlit layer has no resonance (0.0), which is not an error");
    CHECK(epu_crystal_get_resonance(&dev, 9) == EPU_BAD_READING,
          "a bad layer index reads the negative sentinel");
    CHECK(near(epu_crystal_get_resonance(&dev, 0), 527.79936, 1e-9),
          "at 303.8 K the 528 Hz layer sits at 528*(1-1e-4*3.8) = 527.79936 Hz");
    dev.operating_temp_k = 310.0;
    CHECK(near(epu_crystal_get_resonance(&dev, 0), 527.472, 1e-9),
          "raise the die to 310 K and it drops to 528*0.999 = 527.472 Hz");
    dev.operating_temp_k = 303.8;

    /* =============== 11. IRQ evaluation =============== */
    epu_device_init(&dev, 4, "irq");
    epu_device_activate(&dev);
    epu_handle_irq(&dev);
    CHECK(!dev.irq_coherence_lost && !dev.irq_quantum_decoherence
          && !dev.irq_field_imbalance && !dev.irq_crystal_resonance,
          "a fresh, healthy device raises no level IRQ");
    epu_coil_activate(&dev, 0, 0.1);
    epu_handle_irq(&dev);
    CHECK(dev.irq_field_imbalance,
          "one coil at 10 mT against seven at zero is a 10 mT spread -> imbalance IRQ");
    for (uint32_t i = 0; i < 8; i++) epu_coil_activate(&dev, i, 0.1);
    epu_handle_irq(&dev);
    CHECK(!dev.irq_field_imbalance, "energising all eight equally clears it");
    epu_set_solfeggio_frequency(&dev, SOLFEGGIO_528);
    CHECK(dev.current_frequency_hz == 528.0, "the solfeggio setting drives the frequency");
    epu_crystal_activate(&dev, 0, SOLFEGGIO_528);
    epu_handle_irq(&dev);
    CHECK(dev.irq_crystal_resonance,
          "a 528 Hz layer under a 528 Hz drive is on resonance");
    epu_set_vortex_frequency(&dev, VORTEX_3);
    CHECK(dev.current_frequency_hz == 3.0, "the vortex setting overrides it to 3 Hz");
    epu_handle_irq(&dev);
    CHECK(!dev.irq_crystal_resonance,
          "no doubling of 528 Hz lands near 3 Hz, so the resonance IRQ clears");
    {
        vortex_freq_t before = dev.vortex_mode;
        epu_set_vortex_frequency(&dev, (vortex_freq_t)42);
        CHECK(dev.vortex_mode == before && dev.current_frequency_hz == 3.0,
              "an out-of-range vortex mode is ignored, leaving the device unchanged");
        epu_set_solfeggio_frequency(&dev, (solfeggio_freq_t)42);
        CHECK(dev.current_frequency_hz == 3.0, "likewise an out-of-range solfeggio mode");
    }
    {
        emotion_vector_t in = {1.0, 1.0, 1.0, 1.0, 1.0};
        epu_process_emotion(&dev, &in);
        CHECK(dev.irq_emotion_ready, "processing an emotion raises the ready edge");
        epu_handle_irq(&dev);
        CHECK(!dev.irq_emotion_ready, "and epu_handle_irq acknowledges it");
    }

    /* =============== 12. quantum gates =============== */
    epu_device_init(&dev, 5, "quantum");
    CHECK(epu_qubit_apply_gate(&dev, 0, EPU_GATE_H) == EPU_ERR_INACTIVE,
          "an inactive device runs no gates");
    epu_device_activate(&dev);
    CHECK(epu_qubit_apply_gate(&dev, 0, EPU_GATE_H) == EPU_OK, "Hadamard applies");
    CHECK(near(dev.qubits[0].a_re, 0.7071067811865476, 1e-15)
          && near(dev.qubits[0].b_re, 0.7071067811865476, 1e-15),
          "H|0> = (|0>+|1>)/sqrt(2), amplitudes 0.70710678");
    CHECK(near(qp1(&dev.qubits[0]), 0.5, 1e-15), "so P(1) = 0.5 exactly");
    CHECK(near(dev.qubits[0].fidelity, 0.999 * 0.999, 1e-15),
          "one gate costs one fidelity factor: 0.999^2 = 0.998001");
    CHECK(dev.qubits[0].coherence_time_us == 16179.0, "and one microsecond of coherence");

    CHECK(epu_qubit_apply_gate(&dev, 0, 99) == EPU_ERR_RANGE, "gate 99 does not exist");
    CHECK(dev.qubits[0].coherence_time_us == 16179.0
          && near(dev.qubits[0].fidelity, 0.998001, 1e-15),
          "a refused gate costs NOTHING — no coherence, no fidelity");
    CHECK(epu_qubit_apply_gate(&dev, 144, EPU_GATE_X) == EPU_ERR_RANGE, "qubit 144 does not exist");
    CHECK(epu_qubit_apply_gate(0, 0, EPU_GATE_X) == EPU_ERR_NULL, "NULL device -> EPU_ERR_NULL");

    epu_qubit_apply_gate(&dev, 1, EPU_GATE_X);
    CHECK(dev.qubits[1].a_re == 0.0 && dev.qubits[1].b_re == 1.0, "X|0> = |1>");
    epu_qubit_apply_gate(&dev, 1, EPU_GATE_Z);
    CHECK(dev.qubits[1].b_re == -1.0, "Z|1> = -|1>");
    epu_qubit_apply_gate(&dev, 2, EPU_GATE_X);
    epu_qubit_apply_gate(&dev, 2, EPU_GATE_S);
    CHECK(dev.qubits[2].b_re == 0.0 && dev.qubits[2].b_im == 1.0, "S|1> = i|1>");
    epu_qubit_apply_gate(&dev, 3, EPU_GATE_X);
    epu_qubit_apply_gate(&dev, 3, EPU_GATE_T);
    CHECK(near(dev.qubits[3].b_re, 0.7071067811865476, 1e-15)
          && near(dev.qubits[3].b_im, 0.7071067811865476, 1e-15),
          "T|1> = exp(i*pi/4)|1>");
    epu_qubit_apply_gate(&dev, 4, EPU_GATE_Y);
    CHECK(dev.qubits[4].a_re == 0.0 && near(dev.qubits[4].b_im, 1.0, 1e-15),
          "Y|0> = i|1>");
    epu_qubit_apply_gate(&dev, 6, EPU_GATE_X);
    epu_qubit_apply_gate(&dev, 6, EPU_GATE_PHI);
    CHECK(near(qp1(&dev.qubits[6]), 1.0, 1e-12),
          "the golden phase gate is a pure phase — it moves no probability");
    {
        double worst = 0.0;
        for (int i = 0; i < 1000; i++) {
            epu_qubit_apply_gate(&dev, 5, (uint8_t)(i % EPU_NUM_GATES));
            double d = qnorm(&dev.qubits[5]) - 1.0;
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        printf("       worst unitarity drift over 1000 gates: %.3e\n", worst);
        CHECK(worst < 1e-12, "1000 mixed gates keep |psi| = 1 (the gates are really unitary)");
    }

    /* fidelity floor: 0.999^106 is the first value below 0.90 */
    epu_device_init(&dev, 6, "fidelity");
    epu_device_activate(&dev);
    for (int i = 0; i < 104; i++) epu_qubit_apply_gate(&dev, 7, EPU_GATE_I);
    epu_handle_irq(&dev);
    printf("       fidelity after 104 gates: %.9f\n", dev.qubits[7].fidelity);
    CHECK(dev.qubits[7].fidelity > 0.90 && !dev.irq_quantum_decoherence,
          "after 104 gates fidelity is 0.9002786, still above the 0.90 floor");
    epu_qubit_apply_gate(&dev, 7, EPU_GATE_I);
    epu_handle_irq(&dev);
    printf("       fidelity after 105 gates: %.9f\n", dev.qubits[7].fidelity);
    CHECK(dev.qubits[7].fidelity < 0.90 && dev.irq_quantum_decoherence,
          "the 105th gate crosses to 0.8993786 and raises the decoherence IRQ");

    /* coherence exhaustion is a hard stop */
    for (int i = 0; i < 16180; i++) epu_qubit_apply_gate(&dev, 8, EPU_GATE_I);
    CHECK(dev.qubits[8].coherence_time_us == 0.0, "16180 gates exhaust the 16180 us budget");
    CHECK(epu_qubit_apply_gate(&dev, 8, EPU_GATE_I) == EPU_ERR_DECOHERED,
          "the next gate is refused with EPU_ERR_DECOHERED");
    CHECK(epu_qubit_measure(&dev, 8) == EPU_ERR_DECOHERED,
          "and a decohered qubit cannot be measured either");
    CHECK(epu_qubit_get_coherence(&dev, 8) == 0.0, "its coherence reads back as 0");
    CHECK(epu_qubit_get_coherence(&dev, 144) == EPU_BAD_READING,
          "a bad qubit index reads the negative sentinel");

    /* =============== 13. entanglement (correlation model) =============== */
    epu_device_init(&dev, 9, "entangle");
    epu_device_activate(&dev);
    CHECK(epu_qubit_entangle(&dev, 0, 1) == EPU_OK, "qubits 0 and 1 entangle");
    CHECK(near(dev.qubits[0].coherence_time_us, 16180.0 * 0.6180339887498949, 1e-9),
          "the pair keeps phi^-1 of the weaker coherence: 9999.79 us");
    CHECK(near(dev.qubits[0].fidelity, 0.998001, 1e-15),
          "and the pair fidelity is the product 0.999*0.999 = 0.998001");
    CHECK(dev.qubits[0].entangled_with == 1 && dev.qubits[1].entangled_with == 0,
          "the partnership is recorded from both sides");
    CHECK(epu_qubit_entangle(&dev, 0, 2) == EPU_ERR_BUSY, "an entangled qubit cannot be re-paired");
    CHECK(epu_qubit_entangle(&dev, 3, 3) == EPU_ERR_RANGE, "a qubit cannot entangle with itself");
    CHECK(epu_qubit_entangle(&dev, 3, 144) == EPU_ERR_RANGE, "qubit 144 does not exist");
    {
        int mismatches = 0;
        for (uint32_t p = 1; p < 72; p++) epu_qubit_entangle(&dev, 2*p, 2*p+1);
        for (uint32_t p = 0; p < 72; p++) {
            int a = epu_qubit_measure(&dev, 2*p);
            int b = epu_qubit_measure(&dev, 2*p+1);
            if (a != b) mismatches++;
        }
        CHECK(mismatches == 0,
              "all 72 entangled pairs collapse to the SAME outcome (the |00>+|11> correlation)");
    }

    /* =============== 14. measurement is a real Born-rule collapse =============== */
    epu_device_init(&dev, 10, "measure");
    epu_device_activate(&dev);
    CHECK(epu_qubit_measure(&dev, 144) == EPU_ERR_RANGE, "qubit 144 cannot be measured");
    CHECK(epu_qubit_measure(0, 0) == EPU_ERR_NULL, "NULL device -> EPU_ERR_NULL");
    {
        int all_zero = 1;
        for (uint32_t i = 0; i < 144; i++) if (epu_qubit_measure(&dev, i) != 0) all_zero = 0;
        CHECK(all_zero, "|0> measures 0 for all 144 qubits — P(1) = 0 really means never");
    }
    epu_device_init(&dev, 10, "measure1");
    epu_device_activate(&dev);
    {
        int all_one = 1;
        for (uint32_t i = 0; i < 144; i++) {
            epu_qubit_apply_gate(&dev, i, EPU_GATE_X);
            if (epu_qubit_measure(&dev, i) != 1) all_one = 0;
        }
        CHECK(all_one, "|1> measures 1 for all 144 qubits — P(1) = 1 really means always");
    }
    {
        /* Re-measuring must not consume randomness. */
        uint64_t s_before = dev.rng_state;
        int first = epu_qubit_measure(&dev, 0);
        int again = epu_qubit_measure(&dev, 0);
        CHECK(first == again && dev.rng_state == s_before,
              "re-measuring a collapsed qubit repeats the outcome and draws NO randomness");
    }
    {
        /* Fair coin: exact reproducible count, and a different seed differs.
         * The expected counts are NOT magic numbers from a previous run —
         * ref_ones() recomputes them from the published SplitMix64 recurrence
         * and the qubit's own Born probability. */
        const int N = 20000;
        int ones_a = 0, ones_b = 0;
        double r2 = 0.7071067811865476;
        double state_p1 = r2 * r2;      /* the model's own P(1) for this state */
        epu_seed(&dev, 12345u);
        for (int t = 0; t < N; t++) {
            epu_qubit_t *q = &dev.qubits[0];
            q->a_re = r2; q->a_im = 0; q->b_re = r2; q->b_im = 0;
            q->collapsed = false; q->coherence_time_us = 16180.0;
            if (epu_qubit_measure(&dev, 0) == 1) ones_a++;
        }
        epu_seed(&dev, 999u);
        for (int t = 0; t < N; t++) {
            epu_qubit_t *q = &dev.qubits[0];
            q->a_re = r2; q->a_im = 0; q->b_re = r2; q->b_im = 0;
            q->collapsed = false; q->coherence_time_us = 16180.0;
            if (epu_qubit_measure(&dev, 0) == 1) ones_b++;
        }
        int exp_a = ref_ones(12345u, N, state_p1);
        int exp_b = ref_ones(999u,   N, state_p1);
        printf("       seed 12345 -> %d ones (reference %d) ; seed 999 -> %d (reference %d)\n",
               ones_a, exp_a, ones_b, exp_b);
        CHECK(exp_a == 10003 && exp_b == 10070,
              "the INDEPENDENT SplitMix64 reference predicts 10003 and 10070 ones");
        CHECK(ones_a == exp_a,
              "seed 12345 matches the reference exactly — one Born draw per measurement");
        CHECK(ones_b == exp_b, "seed 999 matches its reference exactly too");
        CHECK(ones_a != ones_b,
              "the two seeds disagree, so the PRNG is genuinely being consulted");
        CHECK(near((double)ones_a / N, 0.5, 0.02),
              "and the frequency tracks the Born probability 0.5");
    }
    {
        /* Biased coin: P(1) = phi^-2 = 0.3819660 */
        const int N = 20000;
        int ones = 0;
        double p1 = 0.3819660112501051;
        double br = sqrt(p1), ar = sqrt(1.0 - p1);
        double state_p1 = br * br;
        epu_seed(&dev, 2024u);
        for (int t = 0; t < N; t++) {
            epu_qubit_t *q = &dev.qubits[1];
            q->a_re = ar; q->a_im = 0; q->b_re = br; q->b_im = 0;
            q->collapsed = false; q->coherence_time_us = 16180.0;
            if (epu_qubit_measure(&dev, 1) == 1) ones++;
        }
        int expect = ref_ones(2024u, N, state_p1);
        printf("       biased P(1)=0.381966 -> %d ones (reference %d) / %d (%.5f)\n",
               ones, expect, N, (double)ones / N);
        CHECK(expect == 7652,
              "the reference predicts 7652 ones for seed 2024 at P(1) = phi^-2");
        CHECK(ones == expect,
              "the golden-biased state matches the reference exactly");
        CHECK(ones != ref_ones(2024u, N, 0.5),
              "and it differs from what a fair coin on the same seed would give, so "
              "the amplitude really drives the outcome");
        CHECK(near((double)ones / N, p1, 0.015),
              "and the frequency tracks P(1) = phi^-2 = 0.381966, not 0.5");
    }

    /* =============== 15. emotion -> qubit encoding =============== */
    epu_device_init(&dev, 11, "e2q");
    epu_device_activate(&dev);
    {
        emotion_vector_t in = {1.0, 1.0, 1.0, 1.0, 1.0};
        emotion_vector_t nul = {0.0, 0.0, 0.0, 0.0, 0.0};
        uint32_t ids[EPU_EMOTION_DIMS] = {99, 99, 99, 99, 99};
        CHECK(epu_emotion_to_quantum(&dev, &in, ids) == EPU_OK, "a unit emotion encodes");
        CHECK(ids[0] == 0 && ids[4] == 4, "it took the first five free qubits");
        double sum = 0.0;
        int each_ok = 1;
        for (int d = 0; d < 5; d++) {
            double p = qp1(&dev.qubits[ids[d]]);
            sum += p;
            if (!near(p, 0.2, 1e-12)) each_ok = 0;
        }
        CHECK(each_ok, "each axis of (1,1,1,1,1) carries P(1) = 1/5 = 0.2");
        CHECK(near(sum, 1.0, 1e-12),
              "and the five probabilities sum to exactly 1 — it is a real direction cosine set");
        CHECK(epu_emotion_to_quantum(&dev, &nul, ids) == EPU_ERR_RANGE,
              "the null emotion has no direction and is refused");
        CHECK(epu_emotion_to_quantum(&dev, &in, 0) == EPU_ERR_NULL, "a NULL id array is refused");
    }
    epu_device_init(&dev, 11, "e2q-full");
    epu_device_activate(&dev);
    {
        emotion_vector_t in = {1.0, 0.0, 0.0, 0.0, 0.0};
        uint32_t ids[EPU_EMOTION_DIMS] = {77, 77, 77, 77, 77};
        for (uint32_t p = 0; p < 72; p++) epu_qubit_entangle(&dev, 2*p, 2*p+1);
        CHECK(epu_emotion_to_quantum(&dev, &in, ids) == EPU_ERR_NO_RESOURCE,
              "with all 144 qubits entangled there is nothing free -> EPU_ERR_NO_RESOURCE");
        CHECK(ids[0] == 77, "and the caller's array was left untouched");
    }

    /* =============== 16. coverage MUST be able to fail =============== */
    epu_device_init(&dev2, 12, "coverage");
    CHECK(epu_verify_coverage(&dev2) == false,
          "an inactive device FAILS coverage — no cells are carrying anything");
    CHECK(dev2.coverage_r == 0.0 && dev2.coverage_l == 1.0,
          "r = 0/256 while the untouched qubit buffer is still l = 1.0");
    epu_device_activate(&dev2);
    CHECK(epu_verify_coverage(&dev2) == true, "an activated, healthy device passes");
    CHECK(dev2.coverage_r == 1.0 && dev2.coverage_l == 1.0, "r = l = 1.0");
    CHECK(near((double)dev2.m5.r, 1.0, 1e-9) && near((double)dev2.m5.ell, 1.0, 1e-9),
          "and the M5 mirror carries the same r and l");
    for (uint32_t i = 0; i < 50; i++)
        for (int g = 0; g < 105; g++) epu_qubit_apply_gate(&dev2, i, EPU_GATE_I);
    CHECK(epu_verify_coverage(&dev2) == true,
          "degrading 50 of 144 qubits leaves l = 0.653, still over the 0.5 floor");
    for (uint32_t i = 50; i < 80; i++)
        for (int g = 0; g < 105; g++) epu_qubit_apply_gate(&dev2, i, EPU_GATE_I);
    CHECK(epu_verify_coverage(&dev2) == false,
          "degrading 80 of 144 drops l to 0.444 and coverage FAILS");
    CHECK(near(dev2.coverage_l, 64.0 / 144.0, 1e-12), "l = 64/144 = 0.4444444 exactly");
    CHECK(epu_verify_coverage(0) == false, "a NULL device fails coverage");

    /* =============== 17. health MUST be able to fail =============== */
    epu_device_init(&dev2, 13, "health");
    CHECK(near(epu_get_system_health(&dev2), 0.8, 1e-12),
          "a fresh inactive device scores exactly 0.8 (4 of 5 sub-scores perfect)");
    epu_device_activate(&dev2);
    CHECK(near(epu_get_system_health(&dev2), 0.9974666666666666, 1e-12),
          "activated and healthy it scores 0.99746667");
    for (uint32_t i = 0; i < 144; i++) {
        dev2.qubits[i].coherence_time_us = 0.0;
        dev2.qubits[i].fidelity = 0.0;
    }
    {
        double h = epu_get_system_health(&dev2);
        printf("       health with a dead qubit buffer: %.6f\n", h);
        CHECK(h < 0.7, "a dead qubit buffer drags health below 0.7 — the score really moves");
        CHECK(near(h, (1.0 + 0.0 + 0.0 + 1.0 + (1.0 - 3.8/300.0)) / 5.0, 1e-12),
              "and it equals the hand-computed 0.5974667");
    }
    CHECK(epu_get_system_health(0) == 0.0, "a NULL device scores 0");

    /* =============== 18. diagnostic dump through a sink =============== */
    epu_device_init(&dev, 14, "dumpster");
    epu_device_activate(&dev);
    cap.len = 0; cap.buf[0] = '\0';
    epu_diagnostic_dump(&dev);
    CHECK(cap.len == 0, "with no sink bound the dump writes nothing and claims nothing");
    {
        epu_sink_t s;
        s.write = cap_write;
        s.ctx = &cap;
        epu_bind_sink(&dev, &s);
        epu_diagnostic_dump(&dev);
        CHECK(cap.len > 0, "with a sink bound the dump produces output");
        CHECK(strstr(cap.buf, "simulation only") != 0,
              "the dump says out loud that it is a simulation");
        CHECK(strstr(cap.buf, "name=dumpster") != 0, "it names the device");
        CHECK(strstr(cap.buf, "active_cells=256") != 0, "it reports 256 active cells");
        CHECK(strstr(cap.buf, "power_mw=76.000") != 0, "it reports the computed 76.000 mW");
        CHECK(strstr(cap.buf, "temp_k=303.800") != 0, "and the computed 303.800 K");
        unsigned before = cap.len;
        epu_bind_sink(&dev, 0);
        epu_diagnostic_dump(&dev);
        CHECK(cap.len == before, "unbinding the sink silences the dump again");
    }

    /* =============== 19. HDL parameter export =============== */
    {
        const char *v = epu_generate_hdl(&dev, "verilog");
        CHECK(v != 0, "verilog export succeeds");
        CHECK(v && strstr(v, "NOT A DESIGN") != 0,
              "and it says on its own face that it is not a design");
        CHECK(v && strstr(v, "module epu_core") != 0, "it declares module epu_core");
        CHECK(v && strstr(v, "NUM_CELLS       = 256") != 0, "it carries NUM_CELLS = 256");
        CHECK(v && strstr(v, "COIL_TURNS      = 1597") != 0, "it carries COIL_TURNS = 1597");
        CHECK(v && strstr(v, "DRIVE_FREQ_MHZ  = 28318500") != 0,
              "and the drive frequency 28318.5 Hz as 28318500 millihertz");
        CHECK(v && strstr(v, "device: dumpster") != 0, "the device name is carried through");

        epu_set_solfeggio_frequency(&dev, SOLFEGGIO_963);
        v = epu_generate_hdl(&dev, "VeRiLoG");
        CHECK(v && strstr(v, "DRIVE_FREQ_MHZ  = 963000") != 0,
              "the export tracks device state (963 Hz -> 963000) and is case-insensitive");

        const char *h = epu_generate_hdl(&dev, "vhdl");
        CHECK(h != 0 && strstr(h, "entity epu_core is") != 0, "vhdl export declares the entity");
        CHECK(h && strstr(h, "architecture skeleton") != 0, "with an empty skeleton architecture");
        CHECK(h && strstr(h, "DRIVE_FREQ_MHZ  : integer := 963000") != 0,
              "and the same generic value");
        CHECK(epu_generate_hdl(&dev, "cobol") == 0,
              "an unsupported language returns NULL rather than a plausible-looking file");
        CHECK(epu_generate_hdl(0, "verilog") == 0, "a NULL device returns NULL");
        CHECK(epu_generate_hdl(&dev, 0) == 0, "a NULL language returns NULL");
    }

    /* =============== 20. the fields that are STORED AND INERT ===============
     * These are pinned down so nobody later mistakes them for behaviour.
     * The header says in L4 that they are numbers, not mechanisms; these
     * checks are what "they are just numbers" looks like as a test. */
    epu_device_init(&dev, 15, "inert");
    CHECK(dev.pcie_bandwidth_gbps == 128.0 && dev.pcie_fallback_active == false,
          "the PCIe figure is stored and the fallback never engages — there is no bus");
    CHECK(dev.bus_bandwidth_hz == 1.0e12 && dev.bus_latency_ns == 0.1,
          "the 1 THz / 0.1 ns bus figures are stored constants, not measurements");
    epu_device_activate(&dev);
    CHECK(dev.pcie_fallback_active == false,
          "activation does NOT engage a PCIe fallback, because there is nothing to fall back to");
    CHECK(!dev.crystal_layers[1].fabric_mode && dev.crystal_layers[2].fabric_mode,
          "layers 2 and 3 carry the flexible-substrate flag, layers 0 and 1 do not");
    CHECK(epu_qubit_get_coherence(&dev, 3) == 16180.0,
          "an untouched qubit still reads its full 16180 us budget");
    epu_verify_coverage(&dev);
    CHECK(dev.m5.omega == 0 && dev.m5.chi == 15,
          "the M5 mirror carries the emotion count in omega and the device id in chi");
    CHECK(near((double)dev.m5.phi, 1.0, 1e-9),
          "and r*l in the phase slot — mirrored for inspection, consumed by nothing");

    /* =============== 21. system aggregates: MIN vs MEAN, and staleness ======
     * The original suite only ever compared four IDENTICAL devices, where
     * MIN, MEAN, MAX and "just take devices[0]" all give the same answer.
     * Here one device is degraded so the three disagree. */
    epu_system_init(&sys);
    epu_system_create_device(&sys, "s0");
    epu_system_create_device(&sys, "s1");
    epu_system_create_device(&sys, "s2");
    for (uint32_t i = 0; i < 144; i++) {
        sys.devices[1].qubits[i].coherence_time_us = 100.0;
        sys.devices[1].qubits[i].fidelity = 0.5;
    }
    CHECK(near(sys.system_coherence_us, 16180.0 * 144.0, 1e-6),
          "the system aggregates are STALE until refreshed — degrading a device did not move them (L12)");
    epu_system_refresh(&sys);
    CHECK(near(sys.system_coherence_us, 100.0 * 144.0, 1e-9),
          "after refresh system coherence is the MIN (14400 us), not the mean 1558080 nor devices[0]'s 2329920");
    CHECK(near(sys.system_fidelity, (0.999 + 0.5 + 0.999) / 3.0, 1e-12),
          "and system fidelity is the MEAN over devices, 0.8326667 — not a constant 0.999");
    {
        /* Prove the NULL guard did not disturb the system it was not given. */
        double c0 = sys.system_coherence_us, f0 = sys.system_fidelity;
        uint32_t n0 = sys.num_devices;
        epu_system_refresh(0);
        CHECK(sys.system_coherence_us == c0 && sys.system_fidelity == f0
              && sys.num_devices == n0,
              "epu_system_refresh(NULL) returns without crashing and without touching the real system");
    }
    epu_system_init(&sys);
    epu_system_refresh(&sys);
    CHECK(sys.system_coherence_us == 0.0 && sys.system_fidelity == 0.0,
          "an empty system refreshes to zero rather than dividing by zero");

    /* =============== 22. the power ceiling is DERIVED, not assumed (L11) === */
    epu_device_init(&dev, 22, "power-ceiling");
    epu_device_activate(&dev);
    for (uint32_t i = 0; i < 8; i++) epu_coil_activate(&dev, i, EPU_COIL_MAX_A);
    CHECK(dev.power_consumption_mw == 80.0,
          "saturating everything the API allows draws exactly 80.000 mW (12 + 64 + 4)");
    CHECK(near(dev.operating_temp_k, 304.0, 1e-12), "which is 300 + 80*0.05 = 304.000 K");
    CHECK(epu_coil_activate(&dev, 0, 0.100001) == EPU_ERR_RANGE,
          "and no API can push past it — 100.001 mA is refused");
    CHECK(80.0 < EPU_POWER_BUDGET_MW,
          "80.000 mW is under the 100 mW budget, so the power sub-score is pinned at 1.0 (L11)");
    {
        double h_sat = epu_get_system_health(&dev);
        CHECK(near(h_sat, (1.0 + 1.0 + 1.0 + 1.0 + (1.0 - 4.0 / 300.0)) / 5.0, 1e-12),
              "health at the ceiling is 0.99733333 — only the thermal term moved");
        dev.power_consumption_mw = 1.0e6;
        CHECK(near(epu_get_system_health(&dev), h_sat, 1e-12),
              "poking power_consumption_mw by hand does NOT change health — it is recomputed from the arrays (L11)");
    }

    /* =============== 23. power-down really powers everything down ========= */
    epu_device_init(&dev, 27, "powerdown");
    epu_device_activate(&dev);
    epu_crystal_activate(&dev, 0, SOLFEGGIO_528);
    epu_set_solfeggio_frequency(&dev, SOLFEGGIO_528);
    epu_coil_activate(&dev, 2, 0.1);
    epu_handle_irq(&dev);
    CHECK(dev.irq_crystal_resonance && dev.irq_field_imbalance,
          "lit, on resonance and unbalanced, this device raises both level IRQs");
    CHECK(epu_crystal_get_resonance(&dev, 0) > 0.0, "and the layer reports a resonance");
    CHECK(epu_device_deactivate(&dev) == EPU_OK, "power-down succeeds");
    CHECK(dev.coils[2].current == 0.0 && dev.coils[2].field_strength == 0.0,
          "the energised coil is de-energised and its field really goes to zero");
    CHECK(!dev.crystal_layers[0].active && dev.crystal_layers[0].piezo_response == 0.0,
          "the lit crystal layer is extinguished");
    CHECK(epu_crystal_get_resonance(&dev, 0) == 0.0,
          "an unpowered layer reports NO resonance — a device that is off is not ringing");
    CHECK(!dev.irq_crystal_resonance, "and the resonance IRQ drops with it");
    epu_handle_irq(&dev);
    CHECK(!dev.irq_field_imbalance && !dev.irq_crystal_resonance,
          "re-evaluating the IRQs on a powered-down device raises neither");
    CHECK(dev.power_consumption_mw == 0.0 && dev.operating_temp_k == 300.0,
          "and it draws nothing at ambient");

    /* =============== 24. irq_coherence_lost can be raised AND cleared ====== */
    epu_device_init(&dev, 24, "coh-irq");
    epu_device_activate(&dev);
    /* Threshold is 16180 * 144 * 0.5 = 1164960 us, and the test is strict <,
     * so 72 dead qubits sit exactly ON it and 73 cross it. */
    for (uint32_t i = 0; i < 72; i++) dev.qubits[i].coherence_time_us = 0.0;
    epu_handle_irq(&dev);
    CHECK(near(dev.total_coherence_us, 72.0 * 16180.0, 1e-6),
          "72 dead qubits leave 1164960 us, exactly the coherence-lost threshold");
    CHECK(!dev.irq_coherence_lost,
          "exactly at the threshold the IRQ does NOT fire (the test is strict <)");
    dev.qubits[72].coherence_time_us = 0.0;
    epu_handle_irq(&dev);
    CHECK(dev.irq_coherence_lost,
          "one more dead qubit drops below it and the coherence-lost IRQ FIRES");
    for (uint32_t i = 0; i < 73; i++) dev.qubits[i].coherence_time_us = 16180.0;
    epu_handle_irq(&dev);
    CHECK(!dev.irq_coherence_lost,
          "restoring the budget clears it again — the flag tracks state, it is not sticky");

    /* =============== 25. non-finite input is refused, not laundered (L13) == */
    epu_device_init(&dev, 25, "nonfinite");
    epu_device_activate(&dev);
    CHECK(epu_cell_stimulate(&dev, 0, D_NAN) == EPU_ERR_RANGE, "a NaN drive voltage is refused");
    CHECK(epu_cell_stimulate(&dev, 0, D_INF) == EPU_ERR_RANGE, "+inf volts is refused");
    CHECK(epu_cell_stimulate(&dev, 0, D_NINF) == EPU_ERR_RANGE, "-inf volts is refused");
    CHECK(dev.cells[0].pzt_voltage == 0.0 && dev.cells[0].cycle_count == 0,
          "and none of the three counted as a drive cycle");
    CHECK(epu_cell_set_resonance(&dev, 0, D_NAN) == EPU_ERR_RANGE, "a NaN retune is refused");
    CHECK(epu_cell_set_resonance(&dev, 0, D_INF) == EPU_ERR_RANGE, "an infinite retune is refused");
    CHECK(dev.cells[0].resonance_freq == EPU_RESONANCE_HZ,
          "and the resonance is untouched");
    CHECK(epu_coil_activate(&dev, 0, D_NAN) == EPU_ERR_RANGE, "a NaN coil current is refused");
    CHECK(epu_coil_activate(&dev, 0, D_INF) == EPU_ERR_RANGE, "an infinite coil current is refused");
    CHECK(dev.coils[0].current == 0.0, "and the coil stays de-energised");
    epu_cell_update_spiral(&dev, 5, D_NAN);
    CHECK(dev.cells[5].spiral_theta == 0.0 && dev.cells[5].spiral_r == 0.0,
          "a NaN spiral angle clamps to the origin rather than poisoning the placement");
    epu_coil_set_golden_ratio(&dev, 1, D_NAN);
    CHECK(near(dev.coils[1].golden_ratio_field, 0.3819660112501051, 1e-12),
          "a NaN shaping factor clamps to phi^-2, not to NaN");
    {
        emotion_vector_t bad = {1.0, D_NAN, 1.0, 1.0, 1.0};
        emotion_vector_t inf = {1.0, 1.0, D_INF, 1.0, 1.0};
        uint32_t ids[EPU_EMOTION_DIMS] = {88, 88, 88, 88, 88};
        uint64_t n0 = dev.emotions_processed;
        uint32_t h0 = dev.tx_head;
        dev.irq_emotion_ready = false;
        CHECK(epu_compute_emotion_intensity(&bad) == 0.0,
              "the intensity of a NaN vector is 0.0, not NaN");
        emotion_vector_t o = epu_process_emotion(&dev, &bad);
        CHECK(o.joy == 0.0 && o.love == 0.0 && o.awe == 0.0,
              "a NaN emotion returns the zero vector");
        CHECK(dev.emotions_processed == n0 && dev.tx_head == h0 && !dev.irq_emotion_ready,
              "and counts NOTHING, pushes no frame and raises no IRQ — a NaN transform is not a transform");
        o = epu_process_emotion(&dev, &inf);
        CHECK(o.serenity == 0.0 && dev.emotions_processed == n0,
              "an infinite emotion is refused on the same terms");
        CHECK(epu_emotion_to_quantum(&dev, &bad, ids) == EPU_ERR_RANGE,
              "encoding a NaN emotion onto qubits is refused with EPU_ERR_RANGE");
        CHECK(ids[0] == 88 && ids[4] == 88, "and the caller's id array is untouched");
        CHECK(dev.qubits[0].b_re == 0.0 && dev.qubits[0].a_re == 1.0,
              "and no qubit was written on the way out");
    }

    /* =============== 26. emotion->qubit keeps the SIGN of each axis ======== */
    epu_device_init(&dev, 26, "e2q-signed");
    epu_device_activate(&dev);
    {
        emotion_vector_t v = {3.0, -4.0, 0.0, 0.0, 0.0};   /* norm = 5 exactly */
        uint32_t ids[EPU_EMOTION_DIMS] = {0, 0, 0, 0, 0};
        double sum = 0.0;
        CHECK(epu_emotion_to_quantum(&dev, &v, ids) == EPU_OK, "a signed emotion encodes");
        CHECK(near(dev.qubits[ids[0]].b_re, 0.6, 1e-15),
              "joy 3/5 gives amplitude +0.6");
        CHECK(near(dev.qubits[ids[1]].b_re, -0.8, 1e-15),
              "love -4/5 gives amplitude -0.8 — the SIGN of the axis survives the encoding");
        CHECK(near(qp1(&dev.qubits[ids[1]]), 0.64, 1e-15),
              "while P(1) = 0.64 either way, because probability is the square");
        for (int d = 0; d < EPU_EMOTION_DIMS; d++) {
            sum += qp1(&dev.qubits[ids[d]]);
            if (!near(qnorm(&dev.qubits[ids[d]]), 1.0, 1e-15)) sum += 100.0;
        }
        CHECK(near(sum, 1.0, 1e-12),
              "the five probabilities still sum to 1 and every encoded state is normalised");
        for (int i = 0; i < 7; i++) (void)epu_process_emotion(&dev, &v);
        (void)epu_verify_coverage(&dev);
        CHECK(dev.m5.omega == 7 && dev.emotions_processed == 7,
              "the M5 mirror's omega really tracks emotions_processed; it is not pinned at 0");
    }

    /* =============== 27. bounded strings and bounded buffers ============== */
    {
        char longname[200];
        for (int i = 0; i < 199; i++) longname[i] = (char)('A' + (i % 26));
        longname[199] = '\0';
        epu_device_init(&dev, 28, longname);
        CHECK(strlen(dev.name) == 63,
              "a 199-character name is truncated to 63 characters, not copied over the struct");
        CHECK(memcmp(dev.name, longname, 63) == 0 && dev.name[63] == '\0',
              "the first 63 characters survive intact and the field is NUL-terminated");
        CHECK(dev.cells[255].cell_id == 255 && dev.qubits[143].qubit_id == 143
              && dev.device_id == 28,
              "and the rest of the device initialised normally around it");
        epu_device_init(&dev, 29, 0);
        CHECK(strcmp(dev.name, "epu") == 0, "a NULL name becomes \"epu\" rather than empty or garbage");
    }
    epu_device_init(&dev, 30, "evil*/name\nwith\"quotes\";--drop");
    epu_device_activate(&dev);
    {
        const char *v = epu_generate_hdl(&dev, "verilog");
        CHECK(v != 0, "a device with a hostile name still exports");
        CHECK(v && strstr(v, "device: evil__name_with_quotes__--drop") != 0,
              "and every character that could end the comment or the line is replaced by '_'");
        CHECK(v && strstr(v, "evil*/") == 0 && strstr(v, "\"quotes\"") == 0,
              "so no comment terminator and no quote survives into the output");
    }
    {
        static char full[EPU_HDL_BUF_BYTES];
        const char *v = epu_generate_hdl(&dev, "verilog");
        size_t n;
        char *exact, *tight;
        CHECK(v != 0, "the shared-buffer wrapper still works");
        strcpy(full, v ? v : "");
        n = strlen(full);
        printf("       verilog skeleton: %u bytes into a %d-byte buffer\n",
               (unsigned)n, EPU_HDL_BUF_BYTES);
        CHECK(n > 400 && n < (size_t)EPU_HDL_BUF_BYTES,
              "the skeleton is real content that fits the shared buffer with room to spare");
        exact = (char *)malloc(n + 1);
        CHECK(epu_generate_hdl_buf(&dev, "verilog", exact, (uint32_t)(n + 1)) == exact,
              "an exactly-sized caller buffer succeeds");
        CHECK(strcmp(exact, full) == 0,
              "and produces byte-identical output to the shared-buffer wrapper");
        tight = (char *)malloc(n);
        CHECK(epu_generate_hdl_buf(&dev, "verilog", tight, (uint32_t)n) == 0,
              "one byte short returns NULL — a truncated skeleton is never handed back");
        free(exact);
        free(tight);
        {
            char tiny[8];
            CHECK(epu_generate_hdl_buf(&dev, "verilog", tiny, (uint32_t)sizeof tiny) == 0,
                  "an 8-byte buffer returns NULL");
            CHECK(epu_generate_hdl_buf(&dev, "vhdl", tiny, 1u) == 0, "a 1-byte buffer returns NULL");
            CHECK(tiny[0] == '\0', "and leaves only a NUL behind, having written nothing else");
            CHECK(epu_generate_hdl_buf(&dev, "verilog", tiny, 0u) == 0, "a zero capacity returns NULL");
            CHECK(epu_generate_hdl_buf(&dev, "verilog", 0, 4096u) == 0, "a NULL buffer returns NULL");
            CHECK(epu_generate_hdl_buf(0, "verilog", tiny, 8u) == 0, "a NULL device returns NULL");
        }
        /* Exactly-sized, non-over-allocated language strings: ASan proves the
         * case-insensitive compare never reads past the terminator. */
        {
            char *lv = (char *)malloc(8); memcpy(lv, "verilog", 8);
            char *lh = (char *)malloc(5); memcpy(lh, "vhdl", 5);
            char *lc = (char *)malloc(6); memcpy(lc, "cobol", 6);
            char *le = (char *)malloc(1); le[0] = '\0';
            CHECK(epu_generate_hdl(&dev, lv) != 0, "an exactly-allocated \"verilog\" is accepted");
            CHECK(epu_generate_hdl(&dev, lh) != 0, "an exactly-allocated \"vhdl\" is accepted");
            CHECK(epu_generate_hdl(&dev, lc) == 0, "an exactly-allocated \"cobol\" is refused");
            CHECK(epu_generate_hdl(&dev, le) == 0, "an empty language string is refused");
            free(lv); free(lh); free(lc); free(le);
        }
        {
            char longlang[300];
            memset(longlang, 'v', 299);
            longlang[299] = '\0';
            CHECK(epu_generate_hdl(&dev, longlang) == 0,
                  "a 299-character language name is refused without running off the end");
            CHECK(epu_generate_hdl(&dev, "verilo") == 0, "a truncated language name is refused");
            CHECK(epu_generate_hdl(&dev, "verilogx") == 0, "a suffixed language name is refused");
            CHECK(epu_generate_hdl(&dev, "VHDL") != 0, "but uppercase VHDL is accepted");
        }
    }

    /* =============== 28. the fixed-point formatter really carries ========== */
    epu_device_init(&dev, 31, "rounding");
    epu_device_activate(&dev);
    for (uint32_t i = 0; i < 144; i++) {
        dev.qubits[i].coherence_time_us = 0.0;
        dev.qubits[i].fidelity = 0.9999996;
    }
    dev.qubits[0].coherence_time_us = 1.96;
    {
        epu_sink_t s;
        s.write = cap_write;
        s.ctx = &cap;
        cap.len = 0; cap.buf[0] = '\0';
        epu_bind_sink(&dev, &s);
        epu_diagnostic_dump(&dev);
        CHECK(strstr(cap.buf, "total_coherence_us=2.0") != 0,
              "1.96 us at one decimal place carries to 2.0, it does not print 1.10");
        CHECK(strstr(cap.buf, "avg_fidelity=1.000000") != 0,
              "0.9999996 at six places carries to 1.000000, it does not print 0.1000000");
        /* Regression: the dump used to print coverage_r/coverage_l left over
         * from the previous check next to a freshly computed verdict, so an
         * activated device reported "coverage_r=0.0000 ... pass=yes". */
        CHECK(strstr(cap.buf, "coverage_r=1.0000") != 0,
              "the dump reports the LIVE coverage fractions, not the ones from the last check");
        /* Only qubit 0 still has coherence, so l = 1/144 = 0.0069 and
         * r*l = 0.0069 is far under the 0.5 floor: the line must say no. */
        CHECK(strstr(cap.buf, "coverage_r=1.0000 coverage_l=0.0069 pass=no") != 0,
              "and the fractions on that line agree with the verdict on that line");
        epu_bind_sink(&dev, 0);
    }

    /* =============== 29. NULL is refused everywhere ======================= */
    {
        epu_sink_t s;
        s.write = cap_write;
        s.ctx = &cap;
        /* A live device is snapshotted first. If any NULL-guarded entry point
         * wrote through a stale pointer or a hidden global, the snapshot and
         * the device would diverge; if one dereferenced NULL outright, ASan
         * would have killed the process before this line. */
        epu_device_init(&dev, 39, "null-canary");
        epu_device_activate(&dev);
        epu_crystal_activate(&dev, 0, SOLFEGGIO_741);
        epu_coil_activate(&dev, 4, 0.03);
        memcpy(&dev2, &dev, sizeof dev);
        epu_system_init(0);
        epu_device_init(0, 1, "x");
        epu_seed(0, 1u);
        epu_bind_sink(0, &s);
        epu_handle_irq(0);
        epu_diagnostic_dump(0);
        epu_cell_update_spiral(0, 0, 1.0);
        epu_coil_set_golden_ratio(0, 0, 1.0);
        epu_set_vortex_frequency(0, VORTEX_3);
        epu_set_solfeggio_frequency(0, SOLFEGGIO_528);
        CHECK(memcmp(&dev, &dev2, sizeof dev) == 0,
              "nine void-returning entry points take a NULL device without crashing and without disturbing a live one");
        CHECK(epu_device_deactivate(0) == EPU_ERR_NULL, "deactivate(NULL) -> EPU_ERR_NULL");
        CHECK(epu_cell_set_resonance(0, 0, 28318.5) == EPU_ERR_NULL, "set_resonance(NULL) -> EPU_ERR_NULL");
        CHECK(epu_cell_read_coupling(0, 0) == EPU_BAD_READING, "read_coupling(NULL) -> sentinel");
        CHECK(epu_qubit_entangle(0, 0, 1) == EPU_ERR_NULL, "entangle(NULL) -> EPU_ERR_NULL");
        CHECK(epu_qubit_get_coherence(0, 0) == EPU_BAD_READING, "get_coherence(NULL) -> sentinel");
        CHECK(epu_coil_activate(0, 0, 0.05) == EPU_ERR_NULL, "coil_activate(NULL) -> EPU_ERR_NULL");
        CHECK(epu_coil_compute_field(0, 0) == EPU_BAD_READING, "compute_field(NULL) -> sentinel");
        CHECK(epu_crystal_activate(0, 0, SOLFEGGIO_528) == EPU_ERR_NULL, "crystal_activate(NULL) -> EPU_ERR_NULL");
        CHECK(epu_crystal_get_resonance(0, 0) == EPU_BAD_READING, "crystal_resonance(NULL) -> sentinel");
        CHECK(epu_emotion_to_quantum(0, 0, 0) == EPU_ERR_NULL, "emotion_to_quantum(NULL) -> EPU_ERR_NULL");
        CHECK(epu_system_create_device(0, "x") == 0, "create_device(NULL) -> 0");
    }
    /* out-of-range ids on the void-returning setters must change nothing */
    epu_device_init(&dev, 32, "oob");
    epu_device_activate(&dev);
    {
        double th = dev.cells[7].spiral_theta, gr = dev.coils[7].golden_ratio_field;
        epu_cell_update_spiral(&dev, EPU_NUM_CELLS, 1.0);
        epu_cell_update_spiral(&dev, 0xFFFFFFFFu, 1.0);
        epu_coil_set_golden_ratio(&dev, 8, 2.0);
        epu_coil_set_golden_ratio(&dev, 0xFFFFFFFFu, 2.0);
        CHECK(dev.cells[7].spiral_theta == th && dev.coils[7].golden_ratio_field == gr,
              "out-of-range cell and coil ids are ignored and write nothing (ASan is watching)");
        CHECK(epu_qubit_entangle(&dev, 0, 1) == EPU_OK, "sanity: the device still works afterwards");
    }
    epu_device_init(&dev, 33, "ent-inactive");
    CHECK(epu_qubit_entangle(&dev, 0, 1) == EPU_ERR_INACTIVE,
          "an inactive device entangles nothing");
    CHECK(!dev.qubits[0].entangled && !dev.qubits[1].entangled,
          "and records no partnership when it refuses");
    CHECK(epu_qubit_measure(&dev, 0) == EPU_ERR_INACTIVE,
          "an inactive device measures nothing either");

    /* =============== 30a. the coverage floor is tested AT the floor ======= */
    epu_device_init(&dev2, 34, "cov-boundary");
    epu_device_activate(&dev2);
    for (uint32_t i = 0; i < 72; i++)
        for (int g = 0; g < 105; g++) epu_qubit_apply_gate(&dev2, i, EPU_GATE_I);
    CHECK(epu_verify_coverage(&dev2) == true,
          "with exactly half the qubits good, r*l sits ON the 0.5 floor and PASSES (the test is >=, not >)");
    CHECK(dev2.coverage_r * dev2.coverage_l == EPU_COVERAGE_FLOOR,
          "and r*l is exactly 0.5 — 256/256 x 72/144, both exact in binary");
    for (int g = 0; g < 105; g++) epu_qubit_apply_gate(&dev2, 72, EPU_GATE_I);
    CHECK(epu_verify_coverage(&dev2) == false,
          "one more degraded qubit puts it under the floor and it FAILS");

    /* =============== 30b. entanglement takes the WEAKER half ============== */
    epu_device_init(&dev, 35, "entangle-weak");
    epu_device_activate(&dev);
    {
        const double PI_ = 0.6180339887498949;
        for (int g = 0; g < 1000; g++) epu_qubit_apply_gate(&dev, 5, EPU_GATE_I);
        CHECK(dev.qubits[5].coherence_time_us == 15180.0 && dev.qubits[4].coherence_time_us == 16180.0,
              "qubit 5 has spent 1000 us of coherence while qubit 4 is untouched");
        CHECK(epu_qubit_entangle(&dev, 4, 5) == EPU_OK, "the mismatched pair entangles");
        CHECK(near(dev.qubits[4].coherence_time_us, 15180.0 * PI_, 1e-6),
              "the pair inherits phi^-1 of the WEAKER half (9381.76 us), not of the stronger (9999.79)");
        CHECK(dev.qubits[4].coherence_time_us == dev.qubits[5].coherence_time_us,
              "and both sides of the pair carry the same number");
        /* same answer with the weaker qubit passed second, so this is a real
         * minimum and not "whichever argument came first" */
        for (int g = 0; g < 500; g++) epu_qubit_apply_gate(&dev, 31, EPU_GATE_I);
        CHECK(epu_qubit_entangle(&dev, 30, 31) == EPU_OK, "a pair whose weaker half is the SECOND argument entangles");
        CHECK(near(dev.qubits[30].coherence_time_us, 15680.0 * PI_, 1e-6),
              "and gets phi^-1 of 15680 us too — the minimum does not depend on argument order");
    }

    /* =============== 30c. the trace ring really uses distinct slots ======= */
    epu_device_init(&dev, 36, "ring-slots");
    epu_device_activate(&dev);
    {
        emotion_vector_t in = {0.0, 0.0, 0.0, 0.0, 0.0};
        for (uint32_t k = 0; k < 3; k++) {
            in.joy = (double)(k + 1);
            (void)epu_process_emotion(&dev, &in);
        }
        CHECK(f64at(dev.tx_buffer + 0 * 64) == 1.0
              && f64at(dev.tx_buffer + 1 * 64) == 2.0
              && f64at(dev.tx_buffer + 2 * 64) == 3.0,
              "three frames land in three DIFFERENT 64-byte slots, in order — the ring is not writing slot 0 forever");
        CHECK(u32at(dev.tx_buffer + 1 * 64 + 48) == 1
              && u32at(dev.tx_buffer + 2 * 64 + 48) == 2,
              "and each slot carries its own sequence number");
        CHECK(f64at(dev.rx_buffer + 2 * 64) == 3.0,
              "the rx ring tracks the transformed frames in the same slots");
        for (uint32_t k = 3; k < 65; k++) {
            in.joy = (double)(k + 1);
            (void)epu_process_emotion(&dev, &in);
        }
        CHECK(u32at(dev.tx_buffer + 0 * 64 + 48) == 64 && f64at(dev.tx_buffer + 0) == 65.0,
              "after 65 frames slot 0 has been reused by frame 64");
        CHECK(u32at(dev.tx_buffer + 1 * 64 + 48) == 1 && f64at(dev.tx_buffer + 64) == 2.0,
              "but slot 1 still holds frame 1 — only the single oldest slot was overwritten");
    }

    /* =============== 30d. the entangled correlation is not vacuous ========
     * The original pair test measured pairs that were both still |0>, where
     * "a == b" holds whether or not the partner is ever collapsed. Here the
     * measured half is put into a real superposition first. */
    epu_device_init(&dev, 37, "bell");
    epu_device_activate(&dev);
    epu_seed(&dev, 7u);
    {
        int mismatches = 0, ones = 0;
        for (uint32_t p = 0; p < 72; p++) {
            epu_qubit_entangle(&dev, 2 * p, 2 * p + 1);
            epu_qubit_apply_gate(&dev, 2 * p, EPU_GATE_H);
        }
        for (uint32_t p = 0; p < 72; p++) {
            int a = epu_qubit_measure(&dev, 2 * p);
            int b = epu_qubit_measure(&dev, 2 * p + 1);
            if (a != b) mismatches++;
            if (a == 1) ones++;
        }
        printf("       entangled superpositions: %d of 72 pairs collapsed to 1\n", ones);
        CHECK(mismatches == 0,
              "all 72 pairs held in a REAL superposition still collapse to the same outcome");
        CHECK(ones > 10 && ones < 62,
              "and the outcomes are genuinely mixed, so the agreement is correlation and not 'everything was 0 anyway'");
    }

    /* =============== 30e. the in-module square root is correctly rounded ==
     * epu_device.c no longer calls sqrt(): freestanding.h maps sqrt to
     * __builtin_sqrt, which at -O0 and -Os emits a CALL to libm and leaves
     * the freestanding object with an undefined `sqrt` symbol, so the kernel
     * would not link at those levels. The module computes its own root
     * instead. These checks confirm through the public API that the
     * replacement is BIT-EXACT against the host's libm, not merely close —
     * host and target run the same code, so exactness here is exactness
     * there. sqrt() below is the host's libm, the independent reference. */
    epu_device_init(&dev, 41, "sqrt");
    {
        long bad = 0;
        uint64_t st = 20260805u;
        for (long i = 0; i < 200000; i++) {
            double c0 = (double)(ref_splitmix64(&st) >> 11) * (1.0 / 9007199254740992.0);
            double c1 = (double)(ref_splitmix64(&st) >> 11) * (1.0 / 9007199254740992.0);
            double c2 = (double)(ref_splitmix64(&st) >> 11) * (1.0 / 9007199254740992.0);
            emotion_vector_t v;
            double s, got;
            v.joy = (c0 - 0.5) * 2000.0;
            v.love = (c1 - 0.5) * 2e-8;
            v.serenity = (c2 - 0.5) * 2e8;
            v.awe = c0 * 1e-4;
            v.gratitude = c1 * 7.0;
            s = v.joy * v.joy + v.love * v.love + v.serenity * v.serenity
              + v.awe * v.awe + v.gratitude * v.gratitude;
            got = epu_compute_emotion_intensity(&v);
            if (s > 0.0 && got != sqrt(s)) bad++;
        }
        CHECK(bad == 0,
              "over 200000 emotion vectors spanning 1e-8 to 1e8 the intensity is BIT-IDENTICAL to libm sqrt");
    }
    {
        long bad = 0;
        for (long i = 0; i <= 200000; i++) {
            double t = (double)i * 0.003;      /* 0 .. 600, inside the clamp */
            epu_cell_update_spiral(&dev, 0, t);
            if (dev.cells[0].spiral_r != sqrt(t / EPU_GOLDEN_ANGLE_RAD)) bad++;
        }
        CHECK(bad == 0,
              "and 200001 spiral radii are bit-identical to libm sqrt across the whole clamp range");
    }
    {
        /* The subnormal branch (scale up by 2^108, root, scale down by 2^54)
         * is reachable through the API: a subnormal angle divides down to a
         * subnormal argument. */
        long bad = 0;
        for (int k = 0; k < 40; k++) {
            double t = bits2d((uint64_t)(k + 1) * 0x0000000000000101ULL);  /* subnormals */
            epu_cell_update_spiral(&dev, 1, t);
            if (dev.cells[1].spiral_r != sqrt(t / EPU_GOLDEN_ANGLE_RAD)) bad++;
        }
        CHECK(bad == 0,
              "40 subnormal angles take the scaled path and still match libm exactly");
        epu_cell_update_spiral(&dev, 2, 0.0);
        CHECK(dev.cells[2].spiral_r == 0.0, "sqrt(0) is 0, not NaN");
    }

    /* =============== 30. the model is a pure function of its inputs =======
     * This replaces a CHECK(true, ...). Two devices built from the same
     * inputs and driven through the same op sequence must end up BYTE
     * identical: that is only possible if nothing in the module reads a
     * clock, a register, an IRQ line, or any global mutable state. */
    epu_device_init(&dev,  40, "pure");
    epu_device_init(&dev2, 40, "pure");
    CHECK(memcmp(&dev, &dev2, sizeof dev) == 0,
          "two devices initialised from the same inputs are byte-identical");
    epu_exercise(&dev);
    epu_exercise(&dev2);
    CHECK(memcmp(&dev, &dev2, sizeof dev) == 0,
          "and remain byte-identical after the same ~160 operations — no clock, no hardware, no hidden global is consulted");
    epu_device_init(&dev2, 40, "pure");
    epu_seed(&dev2, 424242u);
    epu_exercise(&dev2);
    CHECK(memcmp(&dev, &dev2, sizeof dev) != 0,
          "reseeding the measurement PRNG changes the result, so the comparison above is not vacuous");

    printf("\n%d checks, %s: %d failure(s)\n",
           checks, failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
