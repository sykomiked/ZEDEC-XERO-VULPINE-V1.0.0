/* deploy.c — deployment profiles. See deploy.h. */
#include "deploy.h"

static uint32_t umin(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t umax(uint32_t a, uint32_t b) { return a > b ? a : b; }

deploy_class_t deploy_classify(const deploy_caps_t *caps) {
    if (!caps) return DEPLOY_WORKSTATION;
    uint32_t cores = caps->cpu_cores, mem = caps->mem_mb, nodes = caps->node_count;

    /* multi-node first: a supercomputer is many nodes AND many cores/node */
    if (nodes > 1u) {
        if (nodes >= 8u && cores >= 32u) return DEPLOY_HPC;
        return DEPLOY_CLUSTER;
    }
    /* single node */
    if (cores <= 2u && mem < 2048u)      return DEPLOY_EMBEDDED;
    if (cores >= 16u || mem >= 65536u)   return DEPLOY_SERVER;
    return DEPLOY_WORKSTATION;
}

void deploy_resolve(const deploy_caps_t *caps, deploy_profile_t *out) {
    if (!out) return;
    deploy_caps_t c = caps ? *caps : (deploy_caps_t){1, 1024, 1, POWER_MAINS, false};
    if (c.cpu_cores == 0) c.cpu_cores = 1;
    if (c.node_count == 0) c.node_count = 1;

    deploy_class_t k = deploy_classify(&c);
    out->klass = k;

    /* per-class shape: a ceiling on cells and the execution profile */
    uint32_t class_cap;   /* the class's cell ceiling */
    switch (k) {
    case DEPLOY_EMBEDDED:    class_cap = 2u;    out->exec = DEPLOY_EXEC_DC; break;
    case DEPLOY_WORKSTATION: class_cap = 8u;    out->exec = DEPLOY_EXEC_AC; break;
    case DEPLOY_SERVER:      class_cap = 64u;   out->exec = DEPLOY_EXEC_AC; break;
    case DEPLOY_CLUSTER:     class_cap = 256u;  out->exec = DEPLOY_EXEC_PC; break;
    case DEPLOY_HPC:         class_cap = DEPLOY_MAX_CELLS; out->exec = DEPLOY_EXEC_PC; break;
    default:                 class_cap = 8u;    out->exec = DEPLOY_EXEC_AC; break;
    }

    /* cells scale with the ACTUAL cores (2 per core is a reasonable
     * fault-isolation granularity), clamped by the class ceiling and the hard
     * ceiling — so a big machine uses its cores, a small one does not pretend. */
    uint32_t by_cores = umax(1u, c.cpu_cores * 2u);
    out->max_cells = umin(umin(by_cores, class_cap), DEPLOY_MAX_CELLS);

    /* concurrency tracks cores; the fabric runs at least one lane */
    out->target_concurrency = umax(1u, c.cpu_cores);

    /* constellation only when there is more than one node AND a network */
    out->enable_constellation = (c.node_count > 1u) && c.has_network;

    /* event budget grows with the class (bigger boxes drain more per cycle) */
    switch (k) {
    case DEPLOY_EMBEDDED:    out->event_budget = 16u;   break;
    case DEPLOY_WORKSTATION: out->event_budget = 64u;   break;
    case DEPLOY_SERVER:      out->event_budget = 256u;  break;
    case DEPLOY_CLUSTER:     out->event_budget = 512u;  break;
    case DEPLOY_HPC:         out->event_budget = 1024u; break;
    default:                 out->event_budget = 64u;   break;
    }

    /* hold back a slice of RAM for the kernel: more on bigger boxes, but never
     * a starving fraction on a tiny one */
    uint32_t reserve = c.mem_mb / 16u;               /* ~6% */
    out->kernel_reserve_mb = umax(16u, umin(reserve, 4096u));

    /* network role follows the class */
    if (!c.has_network)                    out->net_role = NETROLE_CLIENT;
    else if (k >= DEPLOY_CLUSTER)          out->net_role = NETROLE_PEER;
    else if (k == DEPLOY_SERVER)           out->net_role = NETROLE_SERVING;
    else                                   out->net_role = NETROLE_CLIENT;

    /* power posture: conserve on battery whatever the class */
    out->power_posture = c.power;
}

const char *deploy_class_name(deploy_class_t k) {
    switch (k) {
    case DEPLOY_EMBEDDED: return "embedded";
    case DEPLOY_WORKSTATION: return "workstation";
    case DEPLOY_SERVER: return "server";
    case DEPLOY_CLUSTER: return "cluster";
    case DEPLOY_HPC: return "supercomputer";
    default: return "?";
    }
}
const char *deploy_exec_name(deploy_exec_t e) {
    switch (e) { case DEPLOY_EXEC_DC: return "DC"; case DEPLOY_EXEC_AC: return "AC";
                 case DEPLOY_EXEC_PC: return "PC"; default: return "?"; }
}

/* a tiny bounded integer-to-decimal + string append, no libc */
static uint32_t put_str(char *b, uint32_t at, uint32_t cap, const char *s) {
    while (*s && at + 1u < cap) b[at++] = *s++;
    return at;
}
static uint32_t put_u32(char *b, uint32_t at, uint32_t cap, uint32_t v) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v && n < 12) { t[n++] = (char)('0' + v % 10u); v /= 10u; }
    while (n > 0 && at + 1u < cap) b[at++] = t[--n];
    return at;
}

uint32_t deploy_summarise(const deploy_profile_t *p, char *buf, uint32_t cap) {
    if (!p || !buf || cap == 0) return 0;
    uint32_t at = 0;
    at = put_str(buf, at, cap, deploy_class_name(p->klass));
    at = put_str(buf, at, cap, ": ");
    at = put_u32(buf, at, cap, p->max_cells);
    at = put_str(buf, at, cap, " cells, exec=");
    at = put_str(buf, at, cap, deploy_exec_name(p->exec));
    at = put_str(buf, at, cap, ", concur=");
    at = put_u32(buf, at, cap, p->target_concurrency);
    at = put_str(buf, at, cap, p->enable_constellation ? ", constellation" : ", single-node");
    at = put_str(buf, at, cap, p->power_posture == POWER_BATTERY ? ", conserve" : ", perform");
    buf[at < cap ? at : cap - 1] = 0;
    return at;
}
