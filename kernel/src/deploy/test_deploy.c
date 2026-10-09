/* test_deploy.c — one OS adapts from a laptop to a supercomputer.
 *
 * The properties that matter: the class is chosen sensibly from the hardware,
 * the dials SCALE MONOTONICALLY with the machine (a bigger box gets more cells
 * and budget, never fewer), the fabric never exceeds its hard ceiling however
 * absurd the machine, and battery always means conserve.
 */
#include <stdio.h>
#include <string.h>
#include "deploy.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static deploy_profile_t resolve(uint32_t cores, uint32_t mem_mb, uint32_t nodes,
                                deploy_power_t pw, bool net) {
    deploy_caps_t c = { cores, mem_mb, nodes, pw, net };
    deploy_profile_t p; deploy_resolve(&c, &p); return p;
}

int main(void) {
    printf("=== deployment profiles: laptop -> supercomputer, one OS ===\n");

    /* ---- classification ---- */
    CHECK(resolve(1, 512, 1, POWER_BATTERY, true).klass == DEPLOY_EMBEDDED,
          "1 core / 512MB -> embedded");
    CHECK(resolve(4, 8192, 1, POWER_BATTERY, true).klass == DEPLOY_WORKSTATION,
          "4 cores / 8GB laptop -> workstation");
    CHECK(resolve(8, 16384, 1, POWER_MAINS, true).klass == DEPLOY_WORKSTATION,
          "8 cores / 16GB desktop -> workstation");
    CHECK(resolve(32, 131072, 1, POWER_MAINS, true).klass == DEPLOY_SERVER,
          "32 cores / 128GB single box -> server");
    CHECK(resolve(16, 32768, 4, POWER_MAINS, true).klass == DEPLOY_CLUSTER,
          "4 nodes x 16 cores -> cluster");
    CHECK(resolve(128, 524288, 64, POWER_MAINS, true).klass == DEPLOY_HPC,
          "64 nodes x 128 cores / 512GB -> supercomputer");

    /* ---- the SAME OS, different dials ---- */
    {
        deploy_profile_t laptop = resolve(4, 8192, 1, POWER_BATTERY, true);
        deploy_profile_t server = resolve(32, 131072, 1, POWER_MAINS, true);
        deploy_profile_t hpc    = resolve(128, 524288, 64, POWER_MAINS, true);

        CHECK(laptop.max_cells < server.max_cells && server.max_cells < hpc.max_cells,
              "cells scale up: laptop < server < supercomputer");
        CHECK(laptop.event_budget < server.event_budget && server.event_budget < hpc.event_budget,
              "the per-cycle event budget scales up too");
        CHECK(laptop.target_concurrency == 4 && hpc.target_concurrency == 128,
              "concurrency tracks the actual core count");
        CHECK(!laptop.enable_constellation && hpc.enable_constellation,
              "the laptop stays single-node; the supercomputer joins a constellation");
        CHECK(laptop.exec == DEPLOY_EXEC_AC && hpc.exec == DEPLOY_EXEC_PC,
              "the workstation runs adaptive; the HPC box runs parallel-coordinated");
        CHECK(laptop.power_posture == POWER_BATTERY && server.power_posture == POWER_MAINS,
              "the laptop conserves on battery; the server performs on mains");
    }

    /* ---- cells scale with real cores, within the class ceiling ---- */
    {
        deploy_profile_t small_srv = resolve(16, 65536, 1, POWER_MAINS, true);
        deploy_profile_t big_srv   = resolve(48, 262144, 1, POWER_MAINS, true);
        CHECK(big_srv.max_cells > small_srv.max_cells,
              "within the server class, a bigger server gets more cells");
        CHECK(small_srv.max_cells == 32,   /* 16 cores * 2, under the 64 ceiling */
              "16-core server -> 32 cells (2 per core)");
    }

    /* ---- absurd machine cannot exceed the hard ceiling ---- */
    {
        deploy_profile_t monster = resolve(100000, 1u<<30, 100000, POWER_MAINS, true);
        CHECK(monster.max_cells <= DEPLOY_MAX_CELLS,
              "even a preposterous machine is capped at DEPLOY_MAX_CELLS");
        CHECK(monster.klass == DEPLOY_HPC, "and is still classified HPC");
    }

    /* ---- no network: never a serving/peer role, never a constellation ---- */
    {
        deploy_profile_t offline_srv = resolve(32, 131072, 1, POWER_MAINS, false);
        CHECK(offline_srv.net_role == NETROLE_CLIENT,
              "a box with no network cannot be a server/peer role");
        deploy_profile_t offline_multi = resolve(64, 262144, 8, POWER_MAINS, false);
        CHECK(!offline_multi.enable_constellation,
              "no constellation without a network, even on many nodes");
    }

    /* ---- kernel reserve is bounded both ways ---- */
    {
        deploy_profile_t tiny = resolve(1, 256, 1, POWER_BATTERY, false);
        deploy_profile_t huge = resolve(128, 1048576, 1, POWER_MAINS, true);
        CHECK(tiny.kernel_reserve_mb >= 16, "a tiny box still reserves a floor for the kernel");
        CHECK(huge.kernel_reserve_mb <= 4096, "a huge box does not over-reserve");
    }

    /* ---- summary renders ---- */
    {
        char b[128];
        deploy_profile_t p = resolve(128, 524288, 64, POWER_MAINS, true);
        uint32_t n = deploy_summarise(&p, b, sizeof b);
        printf("       %s\n", b);
        CHECK(n > 0 && strstr(b, "supercomputer") && strstr(b, "constellation"),
              "the summary names the class and the constellation");
    }

    /* ---- defaults / null safety ---- */
    {
        deploy_profile_t p; deploy_resolve(0, &p);
        CHECK(p.max_cells >= 1 && p.target_concurrency >= 1,
              "a NULL caps resolves to a safe single-lane default, no crash");
        CHECK(deploy_classify(0) == DEPLOY_WORKSTATION, "NULL caps classifies as workstation");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
