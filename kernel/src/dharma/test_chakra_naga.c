#include <stdio.h>
#include "chakra.h"
#include "naga_raja.h"
#include "rmag_core.h"
#include "lpres_core.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond)                                                                                  \
            printf("PASS: %s\n", msg);                                                             \
        else {                                                                                     \
            printf("FAIL: %s\n", msg);                                                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    printf("=== Chakra: nodes and yodes ===\n");
    chakra_system_t c;
    chakra_init(&c);
    CHECK(c.num_yodes == 6, "chakra_init builds the 6-link default Sushumna path");
    rational_t cond = chakra_conductance(&c, CHAKRA_MULADHARA, CHAKRA_SVADHISTHANA);
    CHECK(cond.num == 1 && cond.den == 1, "default Sushumna conductance is unit (1/1)");
    rational_t missing = chakra_conductance(&c, CHAKRA_MULADHARA, CHAKRA_SAHASRARA);
    CHECK(missing.num == 0,
          "a non-adjacent pair with no yode returns exact rational zero, not null");

    printf("\n=== Kundalini: inductive ascent ===\n");
    rational_t crown = kundalini_rise(&c, (rational_t){2, 1});
    CHECK(crown.num == 2 && crown.den == 1,
          "unit-conductance path preserves seed charge all the way to Sahasrara");
    CHECK(c.nodes[CHAKRA_MULADHARA].charge.num == 2, "Muladhara holds the seed charge after rise");

    printf("\n=== Appu: deductive descent ===\n");
    {
        chakra_system_t c2;
        chakra_init(&c2);
        bodhi_state_t obs;
        obs.coherence = (rational_t){5, 1};
        obs.dispersion = (rational_t){0, 1};
        obs.transcendent = true;
        appu_descend(&c2, obs);
        CHECK(c2.nodes[CHAKRA_SAHASRARA].charge.num == 5,
              "Sahasrara receives the transcendent observation directly");
        CHECK(c2.nodes[CHAKRA_MULADHARA].charge.num == 5,
              "unit-conductance descent particularizes the same value all the way to Muladhara");
    }
    {
        /* Asymmetry check: kundalini AGGREGATES (adds), appu OVERWRITES (scales). */
        chakra_system_t c3;
        chakra_init(&c3);
        c3.nodes[CHAKRA_SVADHISTHANA].charge = (rational_t){100, 1}; /* pre-existing charge */
        kundalini_rise(&c3, (rational_t){1, 1});
        CHECK(c3.nodes[CHAKRA_SVADHISTHANA].charge.num == 101,
              "kundalini_rise ADDS to pre-existing charge (aggregation), 100+1=101");
    }

    printf("\n=== Naga Raja: paraconsistent interface to Tantra ===\n");
    scheduler_t sched;
    oseq_state_t oseq;
    tantra_engine_t engine;
    sched_init(&sched);
    oseq_init(&oseq);
    rmag_init(4096);
    tantra_init(&engine, &sched, &oseq);
    sched.tasks[0].id = 1;
    sched.tasks[0].state = TASK_READY;
    sched.num_tasks = 1;
    sched.tasks[1].id = 2;
    sched.tasks[1].state = TASK_READY;
    sched.num_tasks = 2;
    naga_raja_init(&engine);

    naga_result_t sr = naga_raja_spawn(1);
    CHECK(sr.status == TRIT_GLUT_PLUS,
          "spawn returns GLUT_PLUS (source of emanation, not yet a definite outcome)");

    rmag_set_quota((ordinal_t) 1, (rational_t){10, 1});
    lpres_set_presence((ordinal_t) 1, TRIT_TRUE);
    naga_result_t yr = naga_raja_yield(1);
    CHECK(yr.status == TRIT_TRUE,
          "yield on a well-resolved task (strong RMAG+LPRES) returns TRIT_TRUE");
    CHECK(naga_raja_phase(1) == SEPH_MALKUTH,
          "naga_raja_phase reflects Malkuth after a clean yield");

    naga_result_t tr = naga_raja_terminate(1, 42);
    CHECK(
        tr.status == TRIT_FALSE && tr.value == 42,
        "terminate returns a definite TRIT_FALSE with the exit code, Ain bridges cleanly to FALSE");

    printf("\n=== Naga Raja: IPC as a ledger operation ===\n");
    rmag_set_quota((ordinal_t) 1, (rational_t){50, 1});
    naga_result_t send_ok = naga_raja_send(1, 2, NR_RES_RAW, (rational_t){20, 1});
    CHECK(send_ok.status == TRIT_TRUE && send_ok.value == 20,
          "full transfer (sender has enough) returns TRIT_TRUE with the transferred amount");
    naga_result_t recv_ok = naga_raja_recv(2);
    CHECK(recv_ok.status == TRIT_TRUE && recv_ok.value == 20,
          "receiver claims exactly the transferred amount");
    naga_result_t recv_again = naga_raja_recv(2);
    CHECK(recv_again.status == TRIT_FALSE, "claiming twice in a row returns TRIT_FALSE -- nothing "
                                           "left pending, a definite empty result");

    rmag_set_quota((ordinal_t) 1, (rational_t){5, 1});
    naga_result_t send_partial = naga_raja_send(1, 2, NR_RES_RAW, (rational_t){20, 1});
    CHECK(send_partial.status == TRIT_GLUT_MINUS && send_partial.value == 5,
          "insufficient funds: transfers only what's available (5) and reports GLUT_MINUS, never a "
          "hard failure");

    if (failures == 0)
        printf("\n=== ALL CHAKRA/NAGA-RAJA TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
