/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_host.c — the hosted ZXV swarm: runs the kernel's swarm modules as a
 * native program on macOS, Windows or Linux and serves its window.
 *
 *   zxv-host            desktop: open the window on this machine
 *   zxv-host --server   server: no window here; open it remotely through
 *                       an SSH tunnel (the address is printed at start-up)
 *   zxv-host --remote   use the remote-instance budget (up to 99%)
 *   zxv-host --port N   listen on port N (default 8722)
 *
 * The window is served only on 127.0.0.1, never on the network. A remote
 * window is reached through `ssh -L`, so the SSH key is the only way in.
 *
 * The platform layer (hardware scan, sockets, opening the window) is the
 * only OS-specific code; everything else is the freestanding kernel code
 * in kernel/src/swarm, unchanged. The agents are stand-ins until the model
 * packs exist: the swarm engine around them is real.
 */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <shellapi.h>
   typedef SOCKET sock_t;
#  define CLOSESOCK closesocket
#  define BAD_SOCK INVALID_SOCKET
#else
#  define _DEFAULT_SOURCE
#  define _DARWIN_C_SOURCE
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <sys/time.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <signal.h>
   typedef int sock_t;
#  define CLOSESOCK close
#  define BAD_SOCK (-1)
#  if defined(__APPLE__)
#    include <sys/sysctl.h>
#  endif
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

extern const char zxv_ui_html[];

#define ZXV_VERSION       "0.1.0-preview"
#define AGENT_MB          512u      /* stand-in agents: small memory each */
#define TOKENS_PER_CORE   1000u
#define TICKS_PER_STEP    252u      /* 27720 / 110: about 1 fundamental per 11 s */
#define STEP_MS           100

/* ===================== platform layer: hardware scan ===================== */

static uint32_t pal_cores(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
#endif
}

static void pal_memory(uint64_t *total_mb, uint64_t *free_mb) {
    *total_mb = 0;
    *free_mb = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        *total_mb = ms.ullTotalPhys >> 20;
        *free_mb  = ms.ullAvailPhys >> 20;
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
    *free_mb = ((uint64_t)free_pages * page) >> 20;
    /* macOS keeps memory busy as cache; count half of the rest as reclaimable. */
    if (*total_mb > *free_mb) *free_mb += (*total_mb - *free_mb) / 2;
#else
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        unsigned long long kb;
        while (fscanf(f, "%63s %llu kB\n", key, &kb) == 2) {
            if (!strcmp(key, "MemTotal:"))     *total_mb = kb >> 10;
            if (!strcmp(key, "MemAvailable:")) *free_mb  = kb >> 10;
        }
        fclose(f);
    }
#endif
}

/* Load from other programs, in cores x 1000. */
static uint32_t pal_other_load_milli(void) {
#if defined(_WIN32)
    static ULONGLONG last_idle, last_total;
    FILETIME idle, kern, user;
    if (!GetSystemTimes(&idle, &kern, &user)) return 0;
    ULONGLONG i = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
    ULONGLONG t = (((ULONGLONG)kern.dwHighDateTime << 32) | kern.dwLowDateTime) +
                  (((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime);
    ULONGLONG di = i - last_idle, dt = t - last_total;
    last_idle = i;
    last_total = t;
    if (dt == 0) return 0;
    return (uint32_t)((dt - di) * 1000ull * pal_cores() / dt);
#else
    double l[1];
    if (getloadavg(l, 1) < 1) return 0;
    return (uint32_t)(l[0] * 1000.0);
#endif
}

static void pal_open_window(const char *url) {
    char cmd[512];
#if defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    (void)cmd;
#elif defined(__APPLE__)
    /* App-style window in Chrome if present, else the default browser. */
    snprintf(cmd, sizeof cmd,
             "open -na 'Google Chrome' --args --app='%s' --new-window >/dev/null 2>&1 || open '%s'",
             url, url);
    if (system(cmd) != 0) fprintf(stderr, "open the window at %s\n", url);
#else
    snprintf(cmd, sizeof cmd,
             "(command -v chromium >/dev/null && chromium --app='%s') >/dev/null 2>&1 || "
             "xdg-open '%s' >/dev/null 2>&1 &", url, url);
    if (system(cmd) != 0) fprintf(stderr, "open the window at %s\n", url);
#endif
}

static uint64_t pal_now_ms(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

/* ===================== the swarm ===================== */

typedef struct {
    uint32_t    id;
    char        name[32];
    swarm_dna_t dna;
    uint64_t    gated_value;   /* this fundamental, for fitness */
} agent_t;

static const char *SPECIALTY[] = {
    "Reasoner", "Coder", "Writer", "Researcher", "Voice", "Vision", "OCR",
    "Music", "Image", "Video", "Planner", "Memory", "Critic", "Translator"
};
#define NUM_SPECIALTIES ((uint8_t)(sizeof SPECIALTY / sizeof SPECIALTY[0]))

static struct {
    bool                   remote;
    swarm_hw_scan_t        hw;
    swarm_compute_budget_t gov;
    swarm_budget_t         b;
    swarm_market_t         m;
    swarm_emotion_state_t  e;
    swarm_ledger_t         l;
    swarm_overlap_t        o;
    swarm_witness_t        w[SWARM_MAX_MODELS];
    uint32_t               nw;
    agent_t                agent[SWARM_MAX_MODELS];
    uint32_t               num_agents;
    uint64_t               tick;
    uint64_t               fundamentals;
    uint32_t               due_mask;
    uint64_t               rng;
    uint64_t               instance_seed;
    bool                   quit;
} S;

static uint64_t rnd(void) {                     /* xorshift64*, stand-in dynamics only */
    S.rng ^= S.rng >> 12;
    S.rng ^= S.rng << 25;
    S.rng ^= S.rng >> 27;
    return S.rng * 0x2545F4914F6CDD1Dull;
}

static agent_t *agent_by_id(uint32_t id) {
    for (uint32_t i = 0; i < S.num_agents; i++)
        if (S.agent[i].id == id) return &S.agent[i];
    return NULL;
}

static void name_agent(agent_t *a, uint32_t level) {
    if (level == 0) { snprintf(a->name, sizeof a->name, "Companion"); return; }
    swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
    snprintf(a->name, sizeof a->name, "%s-%u", SPECIALTY[t.specialty], a->id);
}

static void scan_hardware(void) {
    S.hw.remote = S.remote;
    S.hw.cores = pal_cores();
    S.hw.other_load_milli = S.remote ? 0 : pal_other_load_milli();
    pal_memory(&S.hw.mem_total_mb, &S.hw.mem_free_mb);
    S.hw.gpu_mem_mb = 0;                         /* GPU scan arrives with the tensor engine */
    S.gov = swarm_governor_budget(&S.hw, AGENT_MB, TOKENS_PER_CORE);
}

/* Build (or rebuild) the swarm to the governor's size. */
static void build_swarm(void) {
    uint32_t levels = S.gov.num_levels ? S.gov.num_levels : 1;
    uint64_t T = S.gov.tokens_per_cycle;
    swarm_budget_init(&S.b, levels, T);
    swarm_market_init(&S.m);
    swarm_emotion_init(&S.e);
    swarm_overlap_init(&S.o);
    S.num_agents = 0;
    uint32_t id = 1;
    for (uint32_t d = 0; d < levels; d++) {
        for (uint32_t k = 0; k < swarm_level_capacity(d); k++, id++) {
            agent_t *a = &S.agent[S.num_agents++];
            a->id = id;
            a->dna = swarm_dna_from_seed(S.instance_seed ^ ((uint64_t)id * 0x9E3779B97F4A7C15ull));
            a->gated_value = 0;
            name_agent(a, d);
            swarm_budget_register(&S.b, id, d);
            swarm_market_join(&S.m, id, 1000);
            swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
            swarm_emotion_set_feeling(&S.e, id, t.home);
        }
    }
}

/* The last cycle's allotments, kept after the cycle closes so the window can
 * show them (end_cycle zeroes the live ones). */
static uint64_t shown_re[SWARM_MAX_MODELS], shown_mk[SWARM_MAX_MODELS], shown_im[SWARM_MAX_MODELS];

static void keep_allotments(void) {
    for (uint32_t i = 0; i < S.b.num_slots; i++) {
        const swarm_slot_t *s = &S.b.slots[i];
        shown_im[i] = s->allotted_im;
        shown_mk[i] = s->allotted_mk;
        shown_re[i] = s->allotted - s->allotted_im - s->allotted_mk;
    }
}

static void market_cycle(void) {
    /* Stand-in behaviour shaped by each agent's DNA: curious agents bid more,
     * cautious ones verify more and so pass the quality gate more often. */
    swarm_feeling_t mood = { (swarm_emotion_t)(rnd() % SWARM_EMO_COUNT), (uint8_t)(rnd() % 4) };
    swarm_emotion_set_mood(&S.e, mood);
    for (uint32_t i = 0; i < S.num_agents; i++) {
        agent_t *a = &S.agent[i];
        swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
        const swarm_trader_t *tr = swarm_market_trader(&S.m, a->id);
        uint64_t money = tr ? tr->cap[SWARM_CAP_FINANCIAL] : 0;
        swarm_market_bid(&S.m, a->id, money * t.curiosity_permille / 4000u);
        if (rnd() % 4 == 0) {                        /* feelings drift toward home */
            swarm_feeling_t f = t.home;
            f.intensity = (uint8_t)(rnd() % (SWARM_EMO_MAX_INTENSITY + 1u));
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
    if (job >= 0) swarm_overlap_settle(&S.o, &S.b, (uint32_t)job, 30);

    for (uint32_t i = 0; i < S.num_agents; i++) {
        agent_t *a = &S.agent[i];
        swarm_traits_t t = swarm_dna_express(&a->dna, NUM_SPECIALTIES);
        uint64_t g;
        swarm_budget_consume(&S.b, a->id, swarm_budget_remaining(&S.b, a->id) * 3u / 4u, &g);
        uint32_t r_milli = 1500u + (uint32_t)(rnd() % 600u);
        uint32_t l_milli = 600u + t.caution_permille * 4u / 10u;
        if (l_milli > 1000u) l_milli = 1000u;
        uint64_t value = g / 10u;
        if (swarm_quality_credit(&S.m, a->id, SWARM_CAP_INTELLECTUAL, value, r_milli, l_milli) == SWARM_OK)
            a->gated_value += value;
    }
    swarm_witness_reward(&S.m, S.w, S.nw);
    swarm_ledger_post_cycle(&S.l, &S.b, &S.m, S.e.last_imag_pool, S.w, S.nw);
    swarm_market_credit_frugality(&S.m, &S.b);
    keep_allotments();
    swarm_budget_end_cycle(&S.b);
    swarm_market_settle(&S.m);
}

static void evolve(void) {
    /* Digital DNA: within each level of 3 or more, the fittest pair breeds
     * and the child takes the least fit agent's place. */
    for (uint32_t d = 1; d < S.b.num_levels; d++) {
        uint32_t idx[SWARM_MAX_MODELS], n = 0;
        uint64_t fit[SWARM_MAX_MODELS];
        for (uint32_t i = 0; i < S.num_agents; i++) {
            if (S.b.slots[i].level != d) continue;
            const swarm_trader_t *tr = swarm_market_trader(&S.m, S.agent[i].id);
            fit[n] = swarm_dna_fitness(S.agent[i].gated_value,
                                       tr ? tr->cap[SWARM_CAP_SOCIAL] : 0);
            idx[n++] = i;
        }
        uint32_t pa, pb, w;
        if (!swarm_dna_select(fit, n, &pa, &pb, &w)) continue;
        agent_t *child = &S.agent[idx[w]];
        child->dna = swarm_dna_combine(&S.agent[idx[pa]].dna, &S.agent[idx[pb]].dna, S.fundamentals);
        name_agent(child, d);
        swarm_emotion_set_feeling(&S.e, child->id, swarm_dna_express(&child->dna, NUM_SPECIALTIES).home);
    }
    for (uint32_t i = 0; i < S.num_agents; i++) S.agent[i].gated_value = 0;
}

static void step(void) {
    for (uint32_t k = 0; k < TICKS_PER_STEP; k++) {
        S.tick++;
        uint32_t due = swarm_harmonics_due(S.tick);
        if (!due) continue;
        S.due_mask = due;
        if (due & (1u << 10)) market_cycle();              /* 11th harmonic: reflex */
        if (due & 1u) {                                     /* fundamental: growth */
            S.fundamentals++;
            evolve();
            uint32_t old = S.gov.num_levels;
            scan_hardware();
            if (S.gov.num_levels != old) build_swarm();     /* grow or shrink by levels */
            else swarm_budget_set_rate(&S.b, S.gov.tokens_per_cycle);
        }
    }
}

/* ===================== the companion (stand-in) ===================== */

static void answer(const char *q, char *out, size_t cap) {
    swarm_hk_ast_t ast;
    char reading[512];
    if (swarm_hk_parse(q, &ast) == SWARM_HK_OK && ast.num_ops > 0 &&
        swarm_hk_canonical(&ast, reading, sizeof reading) >= 0) {
        snprintf(out, cap,
                 "Shorthand read as: %s\n%u operator(s)%s. This would be routed to the swarm as one request. "
                 "The specialists are stand-ins, so there is no real answer yet.",
                 reading, (unsigned)ast.num_ops,
                 ast.num_ops > SWARM_HK_MAX_OPS ? ", which is too many in one line (split it into steps)" : "");
        return;
    }
    snprintf(out, cap,
             "I received your message (%u words). Routing plan: the companion splits it into steps, "
             "the market gives this cycle's %llu tokens to %u agents, a witness checks every allotment, "
             "and nothing is shown until it passes the r x l >= 1.8 quality gate.\n"
             "The model packs aren't installed in this preview, so I can't write a real answer yet.",
             (unsigned)swarm_hk_words(q), (unsigned long long)S.b.tokens_per_cycle,
             (unsigned)S.num_agents);
}

/* ===================== HTTP + JSON ===================== */

typedef struct { char *p; size_t len, cap; } buf_t;

static void bput(buf_t *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if ((size_t)n < b->cap - b->len) { b->len += (size_t)n; return; }
        size_t nc = b->cap * 2 + (size_t)n + 64;
        char *np = realloc(b->p, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
}

static void json_str(buf_t *b, const char *s) {
    bput(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') bput(b, "\\%c", *s);
        else if ((unsigned char)*s < 0x20) bput(b, "\\u%04x", (unsigned char)*s);
        else bput(b, "%c", *s);
    }
    bput(b, "\"");
}

static void utf8(char *out, uint32_t cp) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    out[4] = 0;
}

static void state_json(buf_t *b) {
    char host[128] = "localhost";
    gethostname(host, sizeof host - 1);
    const swarm_emotion_profile_t *mp = swarm_emotion_profile(S.e.mood.emotion);
    char mood[8];
    utf8(mood, mp ? mp->codepoint : 0x1F610u);
    bput(b, "{\"version\":\"%s\",\"host\":", ZXV_VERSION);
    json_str(b, host);
    bput(b, ",\"remote\":%s,\"cores\":%u,\"cores_milli\":%u,\"mem_total_mb\":%llu,\"mem_mb\":%llu,"
            "\"levels\":%u,\"tokens_per_cycle\":%llu,\"tick\":%llu,\"fundamental\":%llu,\"cycle\":%llu,"
            "\"due_mask\":%u,\"settled\":%llu,\"held\":%llu,\"money_supply\":%llu,\"pot\":%llu,"
            "\"imag\":%llu,\"saved\":%llu,\"mood\":\"%s %u\",\"agents\":[",
         S.remote ? "true" : "false", S.hw.cores, S.gov.cores_milli,
         (unsigned long long)S.hw.mem_total_mb, (unsigned long long)S.gov.mem_mb,
         S.b.num_levels, (unsigned long long)S.b.tokens_per_cycle, (unsigned long long)S.tick,
         (unsigned long long)S.fundamentals, (unsigned long long)S.b.cycle, S.due_mask,
         (unsigned long long)S.l.settled_cycles, (unsigned long long)S.l.held_cycles,
         (unsigned long long)S.m.money_supply, (unsigned long long)S.m.pot,
         (unsigned long long)S.e.last_imag_pool, (unsigned long long)S.o.tokens_saved,
         mood, S.e.mood.intensity);
    for (uint32_t i = 0; i < S.num_agents; i++) {
        const agent_t *a = &S.agent[i];
        const swarm_slot_t *s = &S.b.slots[i];
        const swarm_trader_t *tr = swarm_market_trader(&S.m, a->id);
        swarm_feeling_t f = { SWARM_EMO_NEUTRAL, 0 };
        for (uint32_t k = 0; k < S.e.num; k++) if (S.e.model_id[k] == a->id) f = S.e.feeling[k];
        const swarm_emotion_profile_t *p = swarm_emotion_profile(f.emotion);
        char emoji[8];
        utf8(emoji, p ? p->codepoint : 0x1F610u);
        int verdict = 1;
        uint32_t by = 0;
        for (uint32_t k = 0; k < S.nw; k++) if (S.w[k].model_id == a->id) { verdict = (int)S.w[k].verdict; by = S.w[k].witness_id; }
        const agent_t *wa = agent_by_id(by);
        bput(b, "%s{\"id\":%u,\"name\":", i ? "," : "", a->id);
        json_str(b, a->name);
        bput(b, ",\"level\":%u,\"emoji\":\"%s\",\"intensity\":%u,\"re\":%llu,\"mk\":%llu,\"im\":%llu,"
                "\"money\":%llu,\"social\":%llu,\"gen\":%u,\"witness\":%d,\"witness_by\":",
             (unsigned)s->level, emoji, f.intensity,
             (unsigned long long)shown_re[i], (unsigned long long)shown_mk[i],
             (unsigned long long)shown_im[i],
             (unsigned long long)(tr ? tr->cap[SWARM_CAP_FINANCIAL] : 0),
             (unsigned long long)(tr ? tr->cap[SWARM_CAP_SOCIAL] : 0), a->dna.generation, verdict);
        json_str(b, wa ? wa->name : "kernel");
        bput(b, "}");
    }
    bput(b, "]}");
}

static void send_all(sock_t c, const char *p, size_t n) {
    while (n > 0) {
        int k = (int)send(c, p, (int)n, 0);
        if (k <= 0) return;
        p += k;
        n -= (size_t)k;
    }
}

static void reply(sock_t c, const char *status, const char *type, const char *body, size_t len) {
    char head[256];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len);
    send_all(c, head, (size_t)n);
    send_all(c, body, len);
}

static void handle(sock_t c) {
    char req[8192];
    int n = 0, k;
    while (n < (int)sizeof req - 1 && (k = (int)recv(c, req + n, (int)sizeof req - 1 - n, 0)) > 0) {
        n += k;
        req[n] = 0;
        char *hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end) {
            char *cl = strstr(req, "Content-Length:");
            long want = cl ? strtol(cl + 15, NULL, 10) : 0;
            if (n - (int)(hdr_end + 4 - req) >= want) break;
        }
    }
    req[n > 0 ? n : 0] = 0;
    char *body = strstr(req, "\r\n\r\n");
    body = body ? body + 4 : req + n;

    if (!strncmp(req, "GET / ", 6) || !strncmp(req, "GET /index.html", 15)) {
        reply(c, "200 OK", "text/html; charset=utf-8", zxv_ui_html, strlen(zxv_ui_html));
    } else if (!strncmp(req, "GET /api/state", 14)) {
        buf_t b = { malloc(4096), 0, 4096 };
        if (!b.p) return;
        state_json(&b);
        reply(c, "200 OK", "application/json", b.p, b.len);
        free(b.p);
    } else if (!strncmp(req, "POST /api/ask", 13)) {
        char ans[1400];
        answer(body, ans, sizeof ans);
        reply(c, "200 OK", "text/plain; charset=utf-8", ans, strlen(ans));
    } else if (!strncmp(req, "POST /api/quit", 14)) {
        reply(c, "200 OK", "text/plain", "bye", 3);
        S.quit = true;
    } else {
        reply(c, "404 Not Found", "text/plain", "not found", 9);
    }
}

int main(int argc, char **argv) {
    bool server = false, open_window = true;
    int port = 8722;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--server")) { server = true; open_window = false; }
        else if (!strcmp(argv[i], "--remote")) S.remote = true;
        else if (!strcmp(argv[i], "--no-window")) open_window = false;
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--version")) { printf("zxv-host %s\n", ZXV_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help")) {
            printf("zxv-host %s\n  --server     run headless; open the window remotely over SSH\n"
                   "  --remote     remote-instance budget (up to 99%%)\n"
                   "  --no-window  don't open a window\n  --port N     port (default 8722)\n", ZXV_VERSION);
            return 0;
        }
    }
    if (server && !S.remote) S.remote = true;   /* a server is a remote instance */

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "winsock failed\n"); return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    S.instance_seed = (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull ^ 0x36A9u;
    S.rng = S.instance_seed | 1u;
    swarm_ledger_init(&S.l);
    scan_hardware();
    build_swarm();

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) { fprintf(stderr, "socket failed\n"); return 1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          /* never on the network */
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(ls, 16) != 0) {
        fprintf(stderr, "port %d is busy (is ZXV already running?)\n", port);
        return 1;
    }
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    printf("ZXV swarm %s: %u cores, %llu MB; budget %.2f cores, %llu MB, %u levels, %u agents (%s)\n",
           ZXV_VERSION, S.hw.cores, (unsigned long long)S.hw.mem_total_mb, S.gov.cores_milli / 1000.0,
           (unsigned long long)S.gov.mem_mb, S.gov.num_levels, S.num_agents,
           S.remote ? "remote instance" : "local guest");
    if (server)
        printf("Server mode. From your own computer run:\n  ssh -N -L %d:127.0.0.1:%d <user>@<this-server>\n"
               "then open %s\n", port, port, url);
    else
        printf("Window: %s\n", url);
    fflush(stdout);
    if (open_window) pal_open_window(url);

    uint64_t next_step = pal_now_ms();
    while (!S.quit) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        struct timeval tv = { 0, STEP_MS * 1000 };
        int r = select((int)ls + 1, &rd, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(ls, &rd)) {
            sock_t c = accept(ls, NULL, NULL);
            if (c != BAD_SOCK) { handle(c); CLOSESOCK(c); }
        }
        /* the swarm advances on wall time, however busy the window is; after a
         * sleep it resumes rather than racing to catch up */
        uint64_t now = pal_now_ms();
        if (now > next_step + 1000u) next_step = now;
        for (; now >= next_step; next_step += STEP_MS) step();
    }
    CLOSESOCK(ls);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
