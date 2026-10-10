/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_host.c — the hosted ZXV swarm: runs the kernel's swarm modules as a
 * native program on macOS, Windows or Linux and serves its window.
 *
 *   zxv-host            desktop: open the window on this machine
 *   zxv-host --server   server: no window here; open it remotely through
 *                       an SSH tunnel (the address is printed at start-up)
 *   zxv-host --remote   use the remote-instance budget (up to 99%)
 *   zxv-host --port N   listen on port N (default 8722; 0 picks a free one)
 *   zxv-host --model F  use the GGUF model file F
 *   zxv-host --models-dir D   look for *.gguf in D (default: the per-user
 *                       data folder, see docs/MAC_APP.md)
 *   zxv-host --exit-with-parent   stop when stdin closes (the native shell
 *                       holds the other end, so the engine never outlives it)
 *   zxv-host --net lan|online   turn on the Vinea peer network (default OFF:
 *                       no socket, nothing leaves the machine; see
 *                       zxv_net_host.h). --net-port N, --net-bind IP and
 *                       --peer IP:PORT (repeatable) go with it.
 *   zxv-host --notify auto|print|off   where notifications go besides the
 *                       window (default auto: the OS notifier if present)
 *   zxv-host --update-gateway URL   trustless gateway for update checks
 *
 * The companion's model writes only what the swarm's tokens-per-cycle budget
 * allows (zxv_budget_gate.h): each answer is limited to the companion's
 * remaining allotment and charged for the tokens generated; with nothing
 * left the window is told the budget is exhausted until the next cycle.
 *
 * The window is served only on 127.0.0.1, never on the network. A remote
 * window is reached through `ssh -L`, so the SSH key is the only way in.
 * Every API call must carry this launch's random token, and the Host,
 * Origin and Sec-Fetch-Site headers must show a same-origin loopback
 * request (zxv_http_guard.h), so other web pages cannot drive the app.
 * The token is printed on the "ZXV-URL:" line at start-up; the native
 * shell can choose it instead through the ZXV_TOKEN environment variable.
 *
 * The platform layer (hardware scan, sockets, opening the window) is the
 * only OS-specific code; everything else is the freestanding kernel code
 * in kernel/src/swarm, unchanged. The agents are stand-ins until the model
 * packs exist: the swarm engine around them is real.
 */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <shellapi.h>
typedef SOCKET sock_t;
#    define CLOSESOCK closesocket
#    define BAD_SOCK  INVALID_SOCKET
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/time.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <unistd.h>
#    include <signal.h>
typedef int sock_t;
#    define CLOSESOCK close
#    define BAD_SOCK  (-1)
#    if defined(__APPLE__)
#        include <sys/sysctl.h>
#    endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "swarm_budget.h"
#include "swarm_emotion.h"
#include "swarm_market.h"
#include "swarm_ledger.h"
#include "swarm_reserve.h"
#include "swarm_harmonic.h"
#include "swarm_overlap.h"
#include "swarm_quality.h"
#include "swarm_hk.h"
#include "swarm_governor.h"
#include "swarm_dna.h"
#include "settle.h"
#include "zxv_http_guard.h"
#include "zxv_model_host.h"
#include "zxv_budget_gate.h"
#include "zxv_net_host.h"
#include "zxv_update_host.h"
#include "zx_notify.h"
#include "zx_upcheck.h"

extern const char zxv_ui_html[];

#define ZXV_VERSION     "0.1.0-preview"
#define AGENT_MB        512u /* stand-in agents: small memory each */
#define TOKENS_PER_CORE 1000u
#define TICKS_PER_STEP  252u /* 27720 / 110: about 1 fundamental per 11 s */
#define STEP_MS         100
#define NOTE_SLOTS      64u
#define MAX_PEERS_ARG   8

/* ===================== platform layer: hardware scan ===================== */

static uint32_t pal_cores(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t) n : 1;
#endif
}

static void pal_memory(uint64_t *total_mb, uint64_t *free_mb)
{
    *total_mb = 0;
    *free_mb = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        *total_mb = ms.ullTotalPhys >> 20;
        *free_mb = ms.ullAvailPhys >> 20;
    }
#elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t len = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0) *total_mb = mem >> 20;
    uint32_t free_pages = 0, page = 0;
    len = sizeof free_pages;
    sysctlbyname("vm.page_free_count", &free_pages, &len, NULL, 0);
    len = sizeof page;
    if (sysctlbyname("hw.pagesize", &page, &len, NULL, 0) != 0) page = 4096;
    *free_mb = ((uint64_t) free_pages * page) >> 20;
    /* macOS keeps memory busy as cache; count half of the rest as reclaimable. */
    if (*total_mb > *free_mb) *free_mb += (*total_mb - *free_mb) / 2;
#else
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        unsigned long long kb;
        while (fscanf(f, "%63s %llu kB\n", key, &kb) == 2) {
            if (!strcmp(key, "MemTotal:")) *total_mb = kb >> 10;
            if (!strcmp(key, "MemAvailable:")) *free_mb = kb >> 10;
        }
        fclose(f);
    }
#endif
}

/* Load from other programs, in cores x 1000. */
static uint32_t pal_other_load_milli(void)
{
#if defined(_WIN32)
    static ULONGLONG last_idle, last_total;
    FILETIME idle, kern, user;
    if (!GetSystemTimes(&idle, &kern, &user)) return 0;
    ULONGLONG i = ((ULONGLONG) idle.dwHighDateTime << 32) | idle.dwLowDateTime;
    ULONGLONG t = (((ULONGLONG) kern.dwHighDateTime << 32) | kern.dwLowDateTime) +
                  (((ULONGLONG) user.dwHighDateTime << 32) | user.dwLowDateTime);
    ULONGLONG di = i - last_idle, dt = t - last_total;
    last_idle = i;
    last_total = t;
    if (dt == 0) return 0;
    return (uint32_t) ((dt - di) * 1000ull * pal_cores() / dt);
#else
    double l[1];
    if (getloadavg(l, 1) < 1) return 0;
    return (uint32_t) (l[0] * 1000.0);
#endif
}

/* The window without the native shell: the user's default browser. The
 * native macOS app (macos/ZXVApp.m) shows it in its own WKWebView instead
 * and starts the engine with --no-window. */
static void pal_open_window(const char *url)
{
    /* The URL is built here from a port number and a hex token; refuse
     * anything else so no shell metacharacter can ever reach system(). */
    for (const char *p = url; *p; p++)
        if (!strchr("0123456789abcdefhknopt:/.#=", *p)) return;
#if defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[512];
#    if defined(__APPLE__)
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1", url);
#    else
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", url);
#    endif
    if (system(cmd) != 0) fprintf(stderr, "open the window at %s\n", url);
#endif
}

/* Where models live when no --models-dir is given. */
static void pal_models_dir(char *out, size_t cap)
{
    out[0] = 0;
#if defined(_WIN32)
    const char *app = getenv("APPDATA");
    if (app) snprintf(out, cap, "%s\\ZXV\\models", app);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home) snprintf(out, cap, "%s/Library/Application Support/ZXV/models", home);
#else
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/')
        snprintf(out, cap, "%s/zxv/models", xdg);
    else if (home)
        snprintf(out, cap, "%s/.local/share/zxv/models", home);
#endif
}

static uint64_t pal_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t) GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

/* ===================== the swarm ===================== */

typedef struct {
    uint32_t id;
    char name[32];
    swarm_dna_t dna;
    uint64_t gated_value; /* this fundamental, for fitness */
} agent_t;

static const char *SPECIALTY[] = {"Reasoner", "Coder",  "Writer", "Researcher", "Voice",
                                  "Vision",   "OCR",    "Music",  "Image",      "Video",
                                  "Planner",  "Memory", "Critic", "Translator"};
#define NUM_SPECIALTIES ((uint8_t) (sizeof SPECIALTY / sizeof SPECIALTY[0]))

static struct {
    bool remote;
    swarm_hw_scan_t hw;
    swarm_compute_budget_t gov;
    swarm_budget_t b;
    swarm_market_t m;
    swarm_emotion_state_t e;
    swarm_ledger_t l;
    settle_spine_t spine; /* the swarm's money on the ledger of record (settle.h) */
    swarm_overlap_t o;
    swarm_witness_t w[SWARM_MAX_MODELS];
    uint32_t nw;
    agent_t agent[SWARM_MAX_MODELS];
    uint32_t num_agents;
    uint64_t tick;
    uint64_t fundamentals;
    uint32_t due_mask;
    uint64_t rng;
    uint64_t instance_seed;
    bool quit;
    /* the companion's model answers under the budget (zxv_budget_gate.h) */
    zxv_gate_t last_gate;
    bool have_gate;
    uint64_t asks, exhausted_asks, task_seq;
    /* peers (zxv_net_host.h), updates, notifications */
    zxv_net_t *net;
    zxv_net_mode_t net_want; /* what the user chose */
    uint16_t net_port;
    const char *net_bind;
    zxn_bus_t bus;
    zxn_note_t notes[NOTE_SLOTS];
} S;

static uint64_t rnd(void)
{ /* xorshift64*, stand-in dynamics only */
    S.rng ^= S.rng >> 12;
    S.rng ^= S.rng << 25;
    S.rng ^= S.rng >> 27;
    return S.rng * 0x2545F4914F6CDD1Dull;
}

static agent_t *agent_by_id(uint32_t id)
{
    for (uint32_t i = 0; i < S.num_agents; i++)
        if (S.agent[i].id == id) return &S.agent[i];
    return NULL;
}

static void name_agent(agent_t *a, uint32_t level)
{
    if (level == 0) {
        snprintf(a->name, sizeof a->name, "Companion");
        return;
    }
    swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
    snprintf(a->name, sizeof a->name, "%s-%u", SPECIALTY[t.specialty], a->id);
}

static void scan_hardware(void)
{
    S.hw.remote = S.remote;
    S.hw.cores = pal_cores();
    S.hw.other_load_milli = S.remote ? 0 : pal_other_load_milli();
    pal_memory(&S.hw.mem_total_mb, &S.hw.mem_free_mb);
    S.hw.gpu_mem_mb = 0; /* GPU scan arrives with the tensor engine */
    S.gov = swarm_governor_budget(&S.hw, AGENT_MB, TOKENS_PER_CORE);
}

/* Build (or rebuild) the swarm to the governor's size. */
static void build_swarm(void)
{
    uint32_t levels = S.gov.num_levels ? S.gov.num_levels : 1;
    uint64_t T = S.gov.tokens_per_cycle;
    swarm_budget_init(&S.b, levels, T);
    swarm_market_init(&S.m);
    swarm_emotion_init(&S.e);
    swarm_overlap_init(&S.o);
    uint8_t seed[32];
    uint64_t x = S.instance_seed;
    for (int i = 0; i < 32; i++) { /* splitmix64 bytes: unique per instance */
        if ((i & 7) == 0) {
            x += 0x9E3779B97F4A7C15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            x = z ^ (z >> 31);
        }
        seed[i] = (uint8_t) (x >> (8 * (i & 7)));
    }
    settle_init(&S.spine, seed);
    S.num_agents = 0;
    uint32_t id = 1;
    for (uint32_t d = 0; d < levels; d++) {
        for (uint32_t k = 0; k < swarm_level_capacity(d); k++, id++) {
            agent_t *a = &S.agent[S.num_agents++];
            a->id = id;
            a->dna = swarm_dna_from_seed(S.instance_seed ^ ((uint64_t) id * 0x9E3779B97F4A7C15ull));
            a->gated_value = 0;
            name_agent(a, d);
            swarm_budget_register(&S.b, id, d);
            swarm_market_join(&S.m, id, 1000);
            settle_join(&S.spine, id, 1000, S.tick);
            swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
            swarm_emotion_set_feeling(&S.e, id, t.home);
        }
    }
}

/* The last cycle's allotments, kept after the cycle closes so the window can
 * show them (end_cycle zeroes the live ones). */
static uint64_t shown_re[SWARM_MAX_MODELS], shown_mk[SWARM_MAX_MODELS], shown_im[SWARM_MAX_MODELS];

static void keep_allotments(void)
{
    for (uint32_t i = 0; i < S.b.num_slots; i++) {
        const swarm_slot_t *s = &S.b.slots[i];
        shown_im[i] = s->allotted_im;
        shown_mk[i] = s->allotted_mk;
        shown_re[i] = s->allotted - s->allotted_im - s->allotted_mk;
    }
}

/* Close the open market cycle: the stand-in agents spend, the quality gate,
 * witness reward, ledger and frugality credit run, and the allotments expire.
 * The companion (agent 0) spends only what its model actually generated
 * through answer() while the cycle was open. */
static void close_cycle(void)
{
    if (!S.b.cycle_open) return;
    for (uint32_t i = 0; i < S.num_agents; i++) {
        agent_t *a = &S.agent[i];
        swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
        uint64_t g = 0;
        if (i > 0) /* stand-ins */
            swarm_budget_consume(&S.b, a->id, swarm_budget_remaining(&S.b, a->id) * 3u / 4u, &g);
        else
            g = S.b.slots[0].used;
        uint32_t r_milli = 1500u + (uint32_t) (rnd() % 600u);
        uint32_t l_milli = 600u + t.caution_permille * 4u / 10u;
        if (l_milli > 1000u) l_milli = 1000u;
        uint64_t value = g / 10u;
        if (swarm_quality_credit(&S.m, a->id, SWARM_CAP_INTELLECTUAL, value, r_milli, l_milli) ==
            SWARM_OK)
            a->gated_value += value;
    }
    swarm_witness_reward(&S.m, S.w, S.nw);
    swarm_ledger_post_cycle(&S.l, &S.b, &S.m, S.e.last_imag_pool, S.w, S.nw);
    swarm_market_credit_frugality(&S.m, &S.b);
    keep_allotments();
    swarm_budget_end_cycle(&S.b);
    swarm_market_settle(&S.m);
    /* Reconcile with the ledger of record. A failure halts the spine and,
     * with it, the market: no further cycle runs on books that disagree. */
    if (settle_sync(&S.spine, &S.m, S.tick) != SETTLE_OK)
        fprintf(stderr, "zxv: ledger halted (%d): market stopped\n", (int) S.spine.why);
}

/* Open the next market cycle and leave it open until the next reflex, so the
 * companion's answers can spend its allotment in it. */
static void open_cycle(void)
{
    /* Stand-in behaviour shaped by each agent's DNA: curious agents bid more,
     * cautious ones verify more and so pass the quality gate more often. */
    swarm_feeling_t mood = {(swarm_emotion_t) (rnd() % SWARM_EMO_COUNT), (uint8_t) (rnd() % 4)};
    swarm_emotion_set_mood(&S.e, mood);
    for (uint32_t i = 0; i < S.num_agents; i++) {
        agent_t *a = &S.agent[i];
        swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
        const swarm_trader_t *tr = swarm_market_trader(&S.m, a->id);
        uint64_t money = tr ? tr->cap[SWARM_CAP_FINANCIAL] : 0;
        swarm_market_bid(&S.m, a->id, money * t.curiosity_permille / 4000u);
        if (rnd() % 4 == 0) { /* feelings drift toward home */
            swarm_feeling_t f = t.home;
            f.intensity = (uint8_t) (rnd() % (SWARM_EMO_MAX_INTENSITY + 1u));
            swarm_emotion_set_feeling(&S.e, a->id, f);
        }
    }
    swarm_budget_t pre_b = S.b;
    swarm_market_t pre_m = S.m;
    swarm_emotion_state_t pre_e = S.e;
    swarm_market_begin_cycle(&S.b, &S.m, &S.e);
    S.nw = swarm_witness_cycle(&S.b, &pre_b, &pre_m, &pre_e, S.w);

    /* Agents that share a question share one job (the Venn rule). */
    uint64_t key = swarm_overlap_key(0, S.tick / SWARM_FUNDAMENTAL_TICKS, rnd() % 3);
    int32_t job = -1;
    for (uint32_t i = 1; i < S.num_agents; i++)
        if (rnd() % 3 == 0) job = swarm_overlap_request(&S.o, key, S.agent[i].id);
    if (job >= 0) swarm_overlap_settle(&S.o, &S.b, (uint32_t) job, 30);
}

static void market_cycle(void)
{
    if (S.spine.halted) return; /* fail closed (settle.h R3) */
    close_cycle();
    open_cycle();
}

static void evolve(void)
{
    /* Digital DNA: within each level of 3 or more, the fittest pair breeds
     * and the child takes the least fit agent's place. */
    for (uint32_t d = 1; d < S.b.num_levels; d++) {
        uint32_t idx[SWARM_MAX_MODELS], n = 0;
        uint64_t fit[SWARM_MAX_MODELS];
        for (uint32_t i = 0; i < S.num_agents; i++) {
            if (S.b.slots[i].level != d) continue;
            const swarm_trader_t *tr = swarm_market_trader(&S.m, S.agent[i].id);
            fit[n] = swarm_dna_fitness(S.agent[i].gated_value, tr ? tr->cap[SWARM_CAP_SOCIAL] : 0);
            idx[n++] = i;
        }
        uint32_t pa, pb, w;
        if (!swarm_dna_select(fit, n, &pa, &pb, &w)) continue;
        agent_t *child = &S.agent[idx[w]];
        child->dna =
            swarm_dna_combine(&S.agent[idx[pa]].dna, &S.agent[idx[pb]].dna, S.fundamentals);
        name_agent(child, d);
        swarm_emotion_set_feeling(&S.e, child->id,
                                  swarm_dna_express(&child->dna, NUM_SPECIALTIES).home);
    }
    for (uint32_t i = 0; i < S.num_agents; i++) S.agent[i].gated_value = 0;
}

static void step(void)
{
    for (uint32_t k = 0; k < TICKS_PER_STEP; k++) {
        S.tick++;
        uint32_t due = swarm_harmonics_due(S.tick);
        if (!due) continue;
        S.due_mask = due;
        if (due & (1u << 10)) market_cycle(); /* 11th harmonic: reflex */
        if (due & 1u) {                       /* fundamental: growth */
            S.fundamentals++;
            evolve();
            uint32_t old = S.gov.num_levels;
            scan_hardware();
            if (S.gov.num_levels != old) {
                close_cycle(); /* settle the open cycle before the swarm is rebuilt */
                build_swarm(); /* grow or shrink by levels */
                open_cycle();
            } else {
                swarm_budget_set_rate(&S.b, S.gov.tokens_per_cycle);
            }
        }
    }
}

/* ===================== the companion (stand-in) ===================== */

/* The budget gate around one generation (zxv_model_host.h M5). */
static int32_t run_under_budget(void *rctx, zxv_gen_fn gen, void *gctx)
{
    (void) rctx;
    uint32_t companion = S.num_agents ? S.agent[0].id : 0;
    int32_t r = zxv_budget_gate(&S.b, companion, ZXV_MODEL_MAX_NEW, gen, gctx, &S.last_gate);
    S.have_gate = true;
    return r;
}

static void note_task(bool ok, const char *summary)
{
    zxn_task_finished(&S.bus, "companion", ++S.task_seq, ok,
                      ok ? "The companion answered" : "The companion could not answer", summary,
                      "chat");
}

static void answer(const char *q, char *out, size_t cap)
{
    swarm_hk_ast_t ast;
    char reading[512];
    char model_note[400];
    S.asks++;
    int m = zxv_model_answer_ex(q, run_under_budget, NULL, out, cap);
    if (m == 1) { /* a real model answered, within the budget */
        char sum[96];
        snprintf(sum, sizeof sum, "%u tokens of the %llu left this cycle",
                 (unsigned) S.last_gate.generated,
                 (unsigned long long) S.last_gate.remaining_before);
        note_task(true, sum);
        return;
    }
    if (m == ZXV_MODEL_BUDGET_EXHAUSTED) {
        S.exhausted_asks++;
        snprintf(out, cap,
                 "Budget exhausted for this cycle: the companion has spent all %llu of its tokens "
                 "in market cycle %llu, so the model did not run. The next cycle (about a second) "
                 "refills it; ask again then.",
                 (unsigned long long) (S.num_agents ? S.b.slots[0].allotted : 0),
                 (unsigned long long) S.b.cycle);
        return;
    }
    if (zxv_model_info()->can_generate && out[0]) note_task(false, out);
    size_t nl = strlen(out);
    if (nl >= sizeof model_note) nl = sizeof model_note - 1;
    memcpy(model_note, out, nl);
    model_note[nl] = 0;
    if (swarm_hk_parse(q, &ast) == SWARM_HK_OK && ast.num_ops > 0 &&
        swarm_hk_canonical(&ast, reading, sizeof reading) >= 0) {
        snprintf(out, cap,
                 "Shorthand read as: %s\n%u operator(s)%s. This would be routed to the swarm as "
                 "one request. "
                 "The specialists are stand-ins, so there is no real answer yet.",
                 reading, (unsigned) ast.num_ops,
                 ast.num_ops > SWARM_HK_MAX_OPS
                     ? ", which is too many in one line (split it into steps)"
                     : "");
        return;
    }
    snprintf(
        out, cap,
        "I received your message (%u words). Routing plan: the companion splits it into steps, "
        "the market gives this cycle's %llu tokens to %u agents, a witness checks every allotment, "
        "and nothing is shown until it passes the r x l >= 1.8 quality gate.\n"
        "%s",
        (unsigned) swarm_hk_words(q), (unsigned long long) S.b.tokens_per_cycle,
        (unsigned) S.num_agents,
        model_note[0] ? model_note
                      : "No model is installed, so I can't write a real answer yet. Choose a GGUF "
                        "model file to install one.");
}

/* ===================== HTTP + JSON ===================== */

typedef struct {
    char *p;
    size_t len, cap;
} buf_t;

static void bput(buf_t *b, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if ((size_t) n < b->cap - b->len) {
            b->len += (size_t) n;
            return;
        }
        size_t nc = b->cap * 2 + (size_t) n + 64;
        char *np = realloc(b->p, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
}

static void json_str(buf_t *b, const char *s)
{
    bput(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            bput(b, "\\%c", *s);
        else if ((unsigned char) *s < 0x20)
            bput(b, "\\u%04x", (unsigned char) *s);
        else
            bput(b, "%c", *s);
    }
    bput(b, "\"");
}

static void utf8(char *out, uint32_t cp)
{
    out[0] = (char) (0xF0 | (cp >> 18));
    out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char) (0x80 | (cp & 0x3F));
    out[4] = 0;
}

static void state_json(buf_t *b)
{
    char host[128] = "localhost";
    gethostname(host, sizeof host - 1);
    const swarm_emotion_profile_t *mp = swarm_emotion_profile(S.e.mood.emotion);
    char mood[8];
    utf8(mood, mp ? mp->codepoint : 0x1F610u);
    bput(b, "{\"version\":\"%s\",\"host\":", ZXV_VERSION);
    json_str(b, host);
    bput(
        b,
        ",\"remote\":%s,\"cores\":%u,\"cores_milli\":%u,\"mem_total_mb\":%llu,\"mem_mb\":%llu,"
        "\"levels\":%u,\"tokens_per_cycle\":%llu,\"tick\":%llu,\"fundamental\":%llu,\"cycle\":%llu,"
        "\"due_mask\":%u,\"settled\":%llu,\"held\":%llu,\"money_supply\":%llu,\"pot\":%llu,"
        "\"imag\":%llu,\"saved\":%llu,\"mood\":\"%s %u\",\"ledger_postings\":%llu,"
        "\"ledger_synced\":%llu,\"ledger_halt\":%d,\"agents\":[",
        S.remote ? "true" : "false", S.hw.cores, S.gov.cores_milli,
        (unsigned long long) S.hw.mem_total_mb, (unsigned long long) S.gov.mem_mb, S.b.num_levels,
        (unsigned long long) S.b.tokens_per_cycle, (unsigned long long) S.tick,
        (unsigned long long) S.fundamentals,
        (unsigned long long) (S.b.cycle - (S.b.cycle_open ? 1u : 0u)), S.due_mask,
        (unsigned long long) S.l.settled_cycles, (unsigned long long) S.l.held_cycles,
        (unsigned long long) S.m.money_supply, (unsigned long long) S.m.pot,
        (unsigned long long) S.e.last_imag_pool, (unsigned long long) S.o.tokens_saved, mood,
        S.e.mood.intensity, (unsigned long long) S.spine.seq, (unsigned long long) S.spine.cycles,
        S.spine.halted ? (int) S.spine.why : 0);
    for (uint32_t i = 0; i < S.num_agents; i++) {
        const agent_t *a = &S.agent[i];
        const swarm_slot_t *s = &S.b.slots[i];
        const swarm_trader_t *tr = swarm_market_trader(&S.m, a->id);
        swarm_feeling_t f = {SWARM_EMO_NEUTRAL, 0};
        for (uint32_t k = 0; k < S.e.num; k++)
            if (S.e.model_id[k] == a->id) f = S.e.feeling[k];
        const swarm_emotion_profile_t *p = swarm_emotion_profile(f.emotion);
        char emoji[8];
        utf8(emoji, p ? p->codepoint : 0x1F610u);
        int verdict = 1;
        uint32_t by = 0;
        for (uint32_t k = 0; k < S.nw; k++)
            if (S.w[k].model_id == a->id) {
                verdict = (int) S.w[k].verdict;
                by = S.w[k].witness_id;
            }
        const agent_t *wa = agent_by_id(by);
        bput(b, "%s{\"id\":%u,\"name\":", i ? "," : "", a->id);
        json_str(b, a->name);
        bput(b,
             ",\"level\":%u,\"emoji\":\"%s\",\"intensity\":%u,\"re\":%llu,\"mk\":%llu,\"im\":%llu,"
             "\"money\":%llu,\"social\":%llu,\"gen\":%u,\"witness\":%d,\"witness_by\":",
             (unsigned) s->level, emoji, f.intensity, (unsigned long long) shown_re[i],
             (unsigned long long) shown_mk[i], (unsigned long long) shown_im[i],
             (unsigned long long) (tr ? tr->cap[SWARM_CAP_FINANCIAL] : 0),
             (unsigned long long) (tr ? tr->cap[SWARM_CAP_SOCIAL] : 0), a->dna.generation, verdict);
        json_str(b, wa ? wa->name : "kernel");
        bput(b, "}");
    }
    const zxv_model_info_t *mi = zxv_model_info();
    bput(b, "],\"model\":{\"loaded\":%s,\"tokenizer_ok\":%s,\"can_generate\":%s,\"name\":",
         mi->loaded ? "true" : "false", mi->tok_ok ? "true" : "false",
         mi->can_generate ? "true" : "false");
    json_str(b, mi->name);
    bput(b, ",\"arch\":");
    json_str(b, mi->arch);
    bput(b, ",\"bytes\":%llu,\"layers\":%lld,\"context\":%lld,\"vocab\":%u,\"status\":",
         (unsigned long long) mi->bytes, (long long) mi->n_layers, (long long) mi->n_ctx,
         (unsigned) mi->n_vocab);
    json_str(b, mi->status);
    /* the companion's budget in the open cycle (what the next answer may use) */
    const swarm_slot_t *cs = S.num_agents ? &S.b.slots[0] : NULL;
    uint64_t rem = cs ? swarm_budget_remaining(&S.b, cs->model_id) : 0;
    bput(b,
         "},\"companion\":{\"cycle_open\":%s,\"allotted\":%llu,\"used\":%llu,\"remaining\":%llu,"
         "\"exhausted\":%s,\"max_per_answer\":%u,\"asks\":%llu,\"exhausted_asks\":%llu",
         S.b.cycle_open ? "true" : "false", (unsigned long long) (cs ? cs->allotted : 0),
         (unsigned long long) (cs ? cs->used : 0), (unsigned long long) rem,
         rem == 0 ? "true" : "false", (unsigned) ZXV_MODEL_MAX_NEW, (unsigned long long) S.asks,
         (unsigned long long) S.exhausted_asks);
    if (S.have_gate)
        bput(b,
             ",\"last_answer\":{\"remaining_before\":%llu,\"allowed\":%u,\"generated\":%u,"
             "\"charged\":%llu,\"exhausted\":%s}",
             (unsigned long long) S.last_gate.remaining_before, (unsigned) S.last_gate.max_new,
             (unsigned) S.last_gate.generated, (unsigned long long) S.last_gate.charged,
             S.last_gate.exhausted ? "true" : "false");
    zxv_net_status_t ns;
    zxv_net_status(S.net, &ns);
    bput(b, "},\"net\":{\"mode\":\"%s\",\"bound\":%s,\"port\":%u,\"bind\":",
         zxv_net_mode_name(ns.mode), ns.bound ? "true" : "false", (unsigned) ns.port);
    json_str(b, ns.bind_ip);
    bput(b, ",\"node_id\":");
    json_str(b, ns.node_id);
    bput(b,
         ",\"peers\":%u,\"in\":%u,\"out\":%u,\"refused\":%u,\"policy_drops\":%u,"
         "\"error\":",
         ns.peers, ns.datagrams_in, ns.datagrams_out, ns.refused_in, ns.policy_drops);
    json_str(b, ns.error);
    zxv_update_status_t us;
    zxv_update_status(&us);
    bput(b, "},\"update\":{\"checked\":%s,\"installable\":%s,\"auto\":%s,\"status\":",
         us.checked ? "true" : "false", us.installable ? "true" : "false",
         us.auto_on ? "true" : "false");
    json_str(b, us.status);
    bput(b, ",\"title\":");
    json_str(b, us.title);
    bput(b, ",\"body\":");
    json_str(b, us.body);
    bput(b, ",\"gateway\":");
    json_str(b, us.gateway);
    bput(b, ",\"checks\":%u,\"requests\":%u,\"last_check\":%llu,\"error\":", us.checks, us.requests,
         (unsigned long long) us.last_check);
    json_str(b, us.error);
    bput(b, "},\"notes\":{\"unread\":%u,\"os\":", zxn_unread(&S.bus, ZXN_ALL_KINDS));
    json_str(b, zxn_host_backend());
    bput(b, "}}");
}

/* Newest notes first, for the window's notification panel. */
static void notes_json(buf_t *b)
{
    const zxn_note_t *l[20];
    uint32_t n = zxn_list(&S.bus, l, 20, false);
    bput(b, "{\"unread\":%u,\"notes\":[", zxn_unread(&S.bus, ZXN_ALL_KINDS));
    for (uint32_t i = 0; i < n; i++) {
        bput(b, "%s{\"seq\":%u,\"kind\":", i ? "," : "", (unsigned) l[i]->seq);
        json_str(b, zxn_kind_name((zxn_kind_t) l[i]->kind));
        bput(b, ",\"pri\":%u,\"read\":%s,\"count\":%u,\"source\":", (unsigned) l[i]->pri,
             l[i]->read ? "true" : "false", (unsigned) l[i]->count);
        json_str(b, l[i]->source);
        bput(b, ",\"title\":");
        json_str(b, l[i]->title);
        bput(b, ",\"body\":");
        json_str(b, l[i]->body);
        bput(b, ",\"action\":");
        json_str(b, l[i]->action);
        bput(b, "}");
    }
    bput(b, "]}");
}

/* POST /api/net: "off", "lan", "online" or "peer A.B.C.D:PORT". */
static const char *net_command(const char *body)
{
    zxv_net_mode_t m;
    uint64_t now = zxv_net_wall_ms();
    if (zxv_net_mode_parse(body, &m) == 0) {
        S.net_want = m;
        if (zxv_net_start(S.net, m, S.net_bind, S.net_port, now) != 0) return "could not start";
        return m == ZXV_NET_OFF ? "networking is off" : "networking is on";
    }
    if (!strncmp(body, "peer ", 5))
        return zxv_net_add_peer_str(S.net, body + 5, now) == 0 ? "pinged" : "refused";
    return NULL;
}

static void send_all(sock_t c, const char *p, size_t n)
{
    while (n > 0) {
        int k = (int) send(c, p, (int) n, 0);
        if (k <= 0) return;
        p += k;
        n -= (size_t) k;
    }
}

/* Headers on every reply: never cached, never framed, never sniffed. */
#define SAFE_HEADERS                                                                               \
    "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"                               \
    "X-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\n"                                    \
    "Cross-Origin-Resource-Policy: same-origin\r\nConnection: close\r\n"

static void reply(sock_t c, const char *status, const char *type, const char *body, size_t len)
{
    char head[512];
    int n =
        snprintf(head, sizeof head,
                 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n" SAFE_HEADERS "\r\n",
                 status, type, len);
    send_all(c, head, (size_t) n);
    send_all(c, body, len);
}

/* The window's token plumbing, put into the page's <head>: it takes the
 * token from the URL fragment (or from the native shell), drops it from
 * the address bar, and adds it to the page's own same-origin fetches. */
static const char TOKEN_SCRIPT[] =
    "<script>(function(){var t='';try{var m=/[#&]token=([0-9a-f]{64})/.exec(location.hash);"
    "if(m){t=m[1];sessionStorage.setItem('zxv-token',t);history.replaceState(null,'',"
    "location.pathname)}else{t=sessionStorage.getItem('zxv-token')||''}}catch(e){}"
    "if(typeof window.__ZXV_TOKEN==='string')t=window.__ZXV_TOKEN;var f=window.fetch.bind(window);"
    "window.fetch=function(u,o){o=Object.assign({},o||{});if(typeof u==='string'&&u.charAt(0)==='/'"
    "&&u.charAt(1)!=='/'){var h=new Headers(o.headers||{});h.set('" ZXV_TOKEN_HEADER "',t);"
    "o.headers=h}return f(u,o)}})();</script>";

static const char PAGE_CSP[] =
    "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; "
    "style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; "
    "frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n";

static void send_page(sock_t c)
{
    const char *html = zxv_ui_html;
    size_t total = strlen(html);
    const char *at = strstr(html, "<head>");
    size_t cut = at ? (size_t) (at - html) + 6 : 0;
    char head[768];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %zu\r\n%s" SAFE_HEADERS "\r\n",
                     total + sizeof TOKEN_SCRIPT - 1, PAGE_CSP);
    send_all(c, head, (size_t) n);
    send_all(c, html, cut);
    send_all(c, TOKEN_SCRIPT, sizeof TOKEN_SCRIPT - 1);
    send_all(c, html + cut, total - cut);
}

static zxv_guard_t G;

static void handle(sock_t c)
{
    char req[8192];
    int n = 0, k;
    char *hdr_end = NULL;
    long want = 0;
    while (n < (int) sizeof req - 1 &&
           (k = (int) recv(c, req + n, (int) sizeof req - 1 - n, 0)) > 0) {
        n += k;
        req[n] = 0;
        if (!hdr_end && (hdr_end = strstr(req, "\r\n\r\n")) != NULL) {
            const char *v;
            size_t vl;
            if (zxv_http_header(req, "Content-Length", &v, &vl) == 1) {
                char num[16];
                if (vl == 0 || vl >= sizeof num) {
                    reply(c, "400 Bad Request", "text/plain", "bad length", 10);
                    return;
                }
                memcpy(num, v, vl);
                num[vl] = 0;
                char *e;
                want = strtol(num, &e, 10);
                if (*e || want < 0) {
                    reply(c, "400 Bad Request", "text/plain", "bad length", 10);
                    return;
                }
            }
            if (want > (long) sizeof req - 1 - (long) (hdr_end + 4 - req)) {
                reply(c, "413 Content Too Large", "text/plain", "too large", 9);
                return;
            }
        }
        if (hdr_end && n - (int) (hdr_end + 4 - req) >= want) break;
    }
    /* timed out or closed before the headers or the body ended */
    if (n <= 0 || !hdr_end || n - (int) (hdr_end + 4 - req) < want) return;
    char *body = hdr_end + 4;
    body[want] = 0;

    /* the request target, without any query */
    const char *sp = strchr(req, ' ');
    if (!sp) return;
    const char *path = sp + 1;
    size_t plen = strcspn(path, " ?#\r\n");
    bool is_get = !strncmp(req, "GET ", 4);
#define PATH_IS(s) (plen == sizeof(s) - 1 && !memcmp(path, s, plen))
    bool api = plen >= 5 && !memcmp(path, "/api/", 5);

    zxv_guard_verdict_t v = zxv_guard_check(&G, req, api);
    if (v != ZXV_GUARD_OK) {
        const char *why = zxv_guard_reason(v);
        /* logged sparingly, so a hostile page looping requests cannot fill the disk */
        static uint64_t refused;
        if (++refused <= 20 || refused % 1000 == 0)
            fprintf(stderr, "zxv-host: refused %.*s: %s (%llu so far)\n",
                    (int) (plen < 64 ? plen : 64), path, why, (unsigned long long) refused);
        reply(c, v == ZXV_GUARD_BAD_METHOD ? "405 Method Not Allowed" : "403 Forbidden",
              "text/plain", why, strlen(why));
        return;
    }

    if (PATH_IS("/") || PATH_IS("/index.html")) {
        if (is_get)
            send_page(c);
        else
            reply(c, "405 Method Not Allowed", "text/plain", "use GET", 7);
    } else if (PATH_IS("/api/state") && is_get) {
        buf_t b = {malloc(4096), 0, 4096};
        if (!b.p) return;
        state_json(&b);
        reply(c, "200 OK", "application/json", b.p, b.len);
        free(b.p);
    } else if (PATH_IS("/api/ask") && !is_get) {
        char ans[1400];
        answer(body, ans, sizeof ans);
        reply(c, "200 OK", "text/plain; charset=utf-8", ans, strlen(ans));
    } else if (PATH_IS("/api/quit") && !is_get) {
        reply(c, "200 OK", "text/plain", "bye", 3);
        S.quit = true;
    } else if (PATH_IS("/api/notes") && is_get) {
        buf_t b = {malloc(4096), 0, 4096};
        if (!b.p) return;
        notes_json(&b);
        reply(c, "200 OK", "application/json", b.p, b.len);
        free(b.p);
    } else if (PATH_IS("/api/notes/read") && !is_get) {
        char t[32];
        int k = snprintf(t, sizeof t, "%u", (unsigned) zxn_mark_all_read(&S.bus, ZXN_ALL_KINDS));
        reply(c, "200 OK", "text/plain", t, (size_t) k);
    } else if (PATH_IS("/api/update") && !is_get) {
        /* the user asked: the only way a check runs while not ONLINE */
        zxv_update_check_now((uint64_t) time(NULL));
        zxv_update_status_t us;
        zxv_update_status(&us);
        reply(c, "200 OK", "text/plain", us.status, strlen(us.status));
    } else if (PATH_IS("/api/net") && !is_get) {
        const char *r = net_command(body);
        if (r)
            reply(c, "200 OK", "text/plain", r, strlen(r));
        else
            reply(c, "400 Bad Request", "text/plain", "off, lan, online or peer IP:PORT", 32);
    } else if (PATH_IS("/api/state") || PATH_IS("/api/ask") || PATH_IS("/api/quit") ||
               PATH_IS("/api/notes") || PATH_IS("/api/notes/read") || PATH_IS("/api/update") ||
               PATH_IS("/api/net")) {
        reply(c, "405 Method Not Allowed", "text/plain", "wrong method", 12);
    } else {
        reply(c, "404 Not Found", "text/plain", "not found", 9);
    }
#undef PATH_IS
}

int main(int argc, char **argv)
{
    bool server = false, open_window = true, exit_with_parent = false;
    int port = 8722;
    const char *model = NULL, *models_dir = NULL;
    const char *peers[MAX_PEERS_ARG], *gateway = NULL, *notify = "auto";
    int npeers = 0;
    S.net_port = ZXV_NET_DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--server")) {
            server = true;
            open_window = false;
        } else if (!strcmp(argv[i], "--remote"))
            S.remote = true;
        else if (!strcmp(argv[i], "--no-window"))
            open_window = false;
        else if (!strcmp(argv[i], "--exit-with-parent"))
            exit_with_parent = true;
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc)
            model = argv[++i];
        else if (!strcmp(argv[i], "--models-dir") && i + 1 < argc)
            models_dir = argv[++i];
        else if (!strcmp(argv[i], "--net") && i + 1 < argc) {
            if (zxv_net_mode_parse(argv[++i], &S.net_want) != 0) {
                fprintf(stderr, "zxv-host: --net takes off, lan or online\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--net-port") && i + 1 < argc) {
            int np = atoi(argv[++i]);
            if (np < 0 || np > 65535) {
                fprintf(stderr, "zxv-host: bad --net-port\n");
                return 2;
            }
            S.net_port = (uint16_t) np;
        } else if (!strcmp(argv[i], "--net-bind") && i + 1 < argc)
            S.net_bind = argv[++i];
        else if (!strcmp(argv[i], "--peer") && i + 1 < argc) {
            if (npeers < MAX_PEERS_ARG)
                peers[npeers++] = argv[++i];
            else
                ++i;
        } else if (!strcmp(argv[i], "--update-gateway") && i + 1 < argc)
            gateway = argv[++i];
        else if (!strcmp(argv[i], "--notify") && i + 1 < argc)
            notify = argv[++i];
        else if (!strcmp(argv[i], "--version")) {
            printf("zxv-host %s\n", ZXV_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--help")) {
            printf("zxv-host %s\n  --server            run headless; open the window remotely "
                   "over SSH\n"
                   "  --remote            remote-instance budget (up to 99%%)\n"
                   "  --no-window         don't open a window\n"
                   "  --port N            port (default 8722; 0 = any free port)\n"
                   "  --model FILE        GGUF model to use\n"
                   "  --models-dir DIR    where to look for *.gguf models\n"
                   "  --exit-with-parent  stop when stdin closes\n"
                   "  --net MODE          off (default), lan or online: the Vinea peer network\n"
                   "  --net-port N        its UDP port (default 8723; 0 = any free port)\n"
                   "  --net-bind IP       its address (default 0.0.0.0)\n"
                   "  --peer IP:PORT      ping this peer at start (repeatable)\n"
                   "  --notify MODE       auto (OS notifier if present), print or off\n"
                   "  --update-gateway U  trustless gateway for update checks\n",
                   ZXV_VERSION);
            return 0;
        } else {
            fprintf(stderr, "zxv-host: unknown option %s (try --help)\n", argv[i]);
            return 2;
        }
    }
    if (port < 0 || port > 65535) {
        fprintf(stderr, "zxv-host: bad port %d\n", port);
        return 2;
    }
    if (server && !S.remote) S.remote = true; /* a server is a remote instance */
#if defined(_WIN32)
    if (exit_with_parent) fprintf(stderr, "zxv-host: --exit-with-parent is not supported here\n");
#endif

    /* G1: this launch's token. The native shell may pick it (ZXV_TOKEN);
     * anything that is not a well-formed token is ignored, never trusted. */
    const char *env_tok = getenv("ZXV_TOKEN");
    if (zxv_guard_token_valid(env_tok))
        memcpy(G.token, env_tok, ZXV_TOKEN_HEX + 1);
    else if (zxv_guard_token_new(G.token) != 0) {
        fprintf(stderr, "zxv-host: no secure random source; refusing to start\n");
        return 1;
    }
    G.any_loopback_port = server;

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "winsock failed\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    S.instance_seed = (uint64_t) time(NULL) * 0x9E3779B97F4A7C15ull ^ 0x36A9u;
    S.rng = S.instance_seed | 1u;
    swarm_ledger_init(&S.l);
    scan_hardware();
    build_swarm();
    open_cycle(); /* the companion can answer from the start */

    /* notifications: the window reads the bus; the OS notifier gets the
     * important kinds (an update, a failed answer, a peer request) */
    zxn_init(&S.bus, S.notes, NOTE_SLOTS);
    if (!strcmp(notify, "off"))
        zxn_host_set_mode(ZXN_HOST_OFF);
    else if (!strcmp(notify, "print") || server)
        zxn_host_set_mode(ZXN_HOST_PRINT);
    else
        zxn_host_set_mode(ZXN_HOST_AUTO);
    zxn_subscribe(&S.bus, zxn_host_deliver, NULL,
                  ZXN_KIND_BIT(ZXN_UPDATE) | ZXN_KIND_BIT(ZXN_AI_TASK_FAILED) |
                      ZXN_KIND_BIT(ZXN_PEER_REQUEST),
                  ZXN_PRI_NORMAL);
    if (zxv_update_init(gateway, &S.bus, ZXU_VERSION(0, 1, 0)) != 0) {
        fprintf(stderr, "zxv-host: --update-gateway must be https://, or http:// to 127.0.0.1\n");
        return 2;
    }

    /* peers: nothing is opened unless the user asked (--net, or the window) */
    if (!(S.net = zxv_net_new())) {
        fprintf(stderr, "zxv-host: out of memory\n");
        return 1;
    }

    /* the model slot: an explicit file, else the first *.gguf in the
     * models folder, else none (the swarm runs without one) */
    zxv_model_close();
    char found[1100], dir[1024];
    if (!models_dir) {
        pal_models_dir(dir, sizeof dir);
        models_dir = dir;
    }
    if (!model && models_dir[0] && zxv_model_find_in_dir(models_dir, found, sizeof found) == 0)
        model = found;
    if (model && zxv_model_open(model) != 0)
        fprintf(stderr, "zxv-host: %s\n", zxv_model_info()->status);

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) {
        fprintf(stderr, "socket failed\n");
        return 1;
    }
    int yes = 1;
#if defined(_WIN32)
    /* no other program may bind the same port and steal requests */
    setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *) &yes, sizeof yes);
#else
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof yes);
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short) port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* never on the network */
    if (bind(ls, (struct sockaddr *) &addr, sizeof addr) != 0 || listen(ls, 16) != 0) {
        fprintf(stderr, "port %d is busy (is ZXV already running?)\n", port);
        return 1;
    }
    socklen_t alen = sizeof addr;
    if (getsockname(ls, (struct sockaddr *) &addr, &alen) == 0) port = ntohs(addr.sin_port);
    G.port = port;

    char url[64], url_tok[160];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    snprintf(url_tok, sizeof url_tok, "%s#token=%s", url, G.token);
    printf(
        "ZXV swarm %s: %u cores, %llu MB; budget %.2f cores, %llu MB, %u levels, %u agents (%s)\n",
        ZXV_VERSION, S.hw.cores, (unsigned long long) S.hw.mem_total_mb, S.gov.cores_milli / 1000.0,
        (unsigned long long) S.gov.mem_mb, S.gov.num_levels, S.num_agents,
        S.remote ? "remote instance" : "local guest");
    printf("Model: %s\n", zxv_model_info()->status);
    if (S.net_want != ZXV_NET_OFF) {
        uint64_t wnow = zxv_net_wall_ms();
        zxv_net_status_t ns;
        if (zxv_net_start(S.net, S.net_want, S.net_bind, S.net_port, wnow) == 0) {
            zxv_net_status(S.net, &ns);
            printf("Network: %s, UDP %s:%u, node %s\n", zxv_net_mode_name(ns.mode), ns.bind_ip,
                   (unsigned) ns.port, ns.node_id);
            for (int k = 0; k < npeers; k++)
                if (zxv_net_add_peer_str(S.net, peers[k], wnow) != 0)
                    fprintf(stderr, "zxv-host: peer %s refused\n", peers[k]);
        } else {
            zxv_net_status(S.net, &ns);
            fprintf(stderr, "zxv-host: network not started: %s\n", ns.error);
        }
    } else {
        printf("Network: off (nothing leaves this machine; --net lan|online turns it on)\n");
    }
    if (server)
        printf("Server mode. From your own computer run:\n  ssh -N -L %d:127.0.0.1:%d "
               "<user>@<this-server>\n"
               "then open the ZXV-URL below (keep the #token part private)\n",
               port, port);
    printf("ZXV-URL: %s\n", url_tok);
    fflush(stdout);
    if (open_window) pal_open_window(url_tok);

    uint64_t next_step = pal_now_ms();
    while (!S.quit) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        int maxfd = (int) ls;
        intptr_t nfd = zxv_net_fd(S.net);
        if (nfd >= 0) {
            FD_SET((sock_t) nfd, &rd);
            if ((int) nfd > maxfd) maxfd = (int) nfd;
        }
#if !defined(_WIN32)
        if (exit_with_parent) {
            FD_SET(0, &rd);
        }
#endif
        struct timeval tv = {0, STEP_MS * 1000};
        int r = select(maxfd + 1, &rd, NULL, NULL, &tv);
#if !defined(_WIN32)
        if (r > 0 && exit_with_parent && FD_ISSET(0, &rd)) {
            char tmp[64];
            if (read(0, tmp, sizeof tmp) <= 0) break; /* the shell is gone */
        }
#endif
        if (r > 0 && FD_ISSET(ls, &rd)) {
            sock_t c = accept(ls, NULL, NULL);
            if (c != BAD_SOCK) {
                /* a client that stalls must not freeze the swarm */
#if defined(_WIN32)
                DWORD to = 2000;
#else
                struct timeval to = {2, 0};
#endif
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *) &to, sizeof to);
                setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *) &to, sizeof to);
                handle(c);
                CLOSESOCK(c);
            }
        }
        if (r > 0 && nfd >= 0 && FD_ISSET((sock_t) nfd, &rd))
            zxv_net_on_readable(S.net, zxv_net_wall_ms());
        zxv_net_tick(S.net, zxv_net_wall_ms());
        zxv_update_tick((uint64_t) time(NULL),
                        zxv_net_fd(S.net) >= 0 && S.net_want == ZXV_NET_ONLINE);
        /* the swarm advances on wall time, however busy the window is; after a
         * sleep it resumes rather than racing to catch up */
        uint64_t now = pal_now_ms();
        if (now > next_step + 1000u) next_step = now;
        for (; now >= next_step; next_step += STEP_MS) step();
    }
    CLOSESOCK(ls);
    zxv_net_free(S.net);
    zxv_update_free();
    zxv_model_close();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
