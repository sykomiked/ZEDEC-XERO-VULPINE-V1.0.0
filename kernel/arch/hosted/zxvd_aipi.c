/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_aipi.c — AIPI inside zxvd (see zxvd_aipi.h, docs/AIPI.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_aipi.h"
#include "aipi.h"
#include "aipi_zwasm.h"
#include "web4_util.h" /* hex */
#include "zxv_api.h"

#define JOBS 4u
#define MEMO 64u
#define TAB  4096u /* blockstore index slots (a power of two) */
#define PINS 1024u

static struct {
    bool open;
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap;
    zxv_rmag *rm;
    uint32_t budget;
    aipi_zw *zw;
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t sops;
    ipfsn_bs_t bs;
    ipfsn_bs_entry_t *tab;
    ipfsn_pin_t *pins;
    ipfsn_node_t node;
    uint8_t *scratch, *net, *modbuf, *conbuf, *getbuf;
    aipi_ctx_t a;
    aipi_job_t jobs[JOBS];
    aipi_memo_t memo[MEMO];
    uint64_t puts, gets, publishes, calls, failed;
} Z;

static const char *ROUTE[] = {"?", "cache", "local", "fetch_local", "mesh", "unavailable"};

/* ----- output helpers ----- */

static void put_cid(zxvd_buf_t *out, const char *key, const ipfsn_cid_t *cid)
{
    char txt[AIPI_TEXT_MAX];
    zxvd_buf_printf(out, "\"%s\":", key);
    if (aipi_cid_to_text(cid, txt, sizeof txt) < 0) txt[0] = 0;
    zxvd_buf_json_str(out, txt);
}

static void put_tag(zxvd_buf_t *out, lpx_t t)
{
    zxvd_buf_raw(out, "\"tag\":{\"base\":", 14);
    zxvd_buf_json_str(out, lpx_base_name(lpx_base(t)));
    zxvd_buf_raw(out, ",\"polarity\":", 12);
    zxvd_buf_json_str(out, lpx_pol_name(lpx_pol(t)));
    zxvd_buf_raw(out, ",\"phase\":", 9);
    zxvd_buf_json_str(out, lpx_phase_name(lpx_phase(t)));
    zxvd_buf_raw(out, ",\"action\":", 10);
    zxvd_buf_json_str(out, lpx_action_name(lpx_action(t)));
    zxvd_buf_raw(out, "}", 1);
}

/* the status name, then what it means here */
static const char *aipi_why(int r)
{
    static const char *const MEANS[] = {
        "",
        "an argument is out of range",
        "the bytes do not parse",
        "not an AIPI function",
        "too large",
        "the bytes do not hash to their CID",
        "not in the local blockstore",
        "private work never leaves this node",
        "offline and the code is not in the local blockstore",
        "the job is not in a state that accepts this",
        "the same executor answered twice",
        "the job table or the blockstore is full",
        "the bounty could not be reserved",
        "the sandbox refused or stopped the run (validator, contract, trap or fuel)",
        "the function CID's codec does not match its intent",
        "the mesh refused the broadcast",
    };
    static char why[128];
    int k = -r;
    snprintf(why, sizeof why, "%s: %s", aipi_status_name(r),
             k > 0 && k < (int) (sizeof MEANS / sizeof MEANS[0]) ? MEANS[k] : "failed");
    return why;
}

static bool cid_in(const char *s, ipfsn_cid_t *cid)
{
    return aipi_cid_from_text(s, (uint32_t) strlen(s), cid) == AIPI_OK;
}

/* ----- methods ----- */

static const char *z_put(const uint8_t *data, uint32_t len, const char *path, bool priv,
                         zxvd_buf_t *out)
{
    ipfsn_cid_t cid;
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) return "the file could not be opened";
        size_t n = fread(Z.modbuf, 1, AIPI_MODULE_MAX, f);
        int more = fgetc(f);
        int bad = ferror(f);
        fclose(f);
        if (bad) return "the file could not be read";
        if (more != EOF) return "the file is larger than 1 MiB (AIPI_MODULE_MAX)";
        data = Z.modbuf;
        len = (uint32_t) n;
    }
    int r = aipi_put_blob(&Z.a, data, len, priv ? IPFSN_VIS_PRIVATE : IPFSN_VIS_PUBLIC, &cid);
    if (r != AIPI_OK) return aipi_why(r);
    Z.puts++;
    zxvd_buf_raw(out, "{", 1);
    put_cid(out, "cid", &cid);
    zxvd_buf_printf(out, ",\"bytes\":%u,\"private\":%s}", (unsigned) len, priv ? "true" : "false");
    return NULL;
}

static const char *z_get(const char *s, zxvd_buf_t *out)
{
    static char hex[2u * ZXVD_AIPI_GET_MAX + 1u];
    ipfsn_cid_t cid;
    uint32_t len = 0;
    lpx_t tag = 0;
    if (!cid_in(s, &cid)) return "not a CID";
    int r = aipi_get(&Z.a, &cid, Z.getbuf, ZXVD_AIPI_GET_MAX, &len, &tag);
    if (r == AIPI_ERR_SPACE) return "the block is larger than 65536 bytes";
    if (r != AIPI_OK) return aipi_why(r);
    if (w4_hex_encode(Z.getbuf, len, hex, sizeof hex) < 0) return aipi_why(AIPI_ERR_SPACE);
    Z.gets++;
    zxvd_buf_raw(out, "{", 1);
    put_cid(out, "cid", &cid);
    zxvd_buf_printf(out, ",\"bytes\":%u,\"hex\":", (unsigned) len);
    zxvd_buf_json_str(out, hex);
    zxvd_buf_raw(out, ",", 1);
    put_tag(out, tag);
    zxvd_buf_raw(out, "}", 1);
    return NULL;
}

static bool name_in(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n > AIPI_NAME_MAX) return false;
    memcpy(dst, src, n + 1);
    return true;
}

static const char *z_publish(const zxvd_aipi_fn_t *in, zxvd_buf_t *out)
{
    aipi_fn_t fn;
    ipfsn_cid_t cid;
    memset(&fn, 0, sizeof fn);
    fn.intent = AIPI_INTENT_FN;
    fn.verify = in->verify;
    fn.privacy = in->local_only ? AIPI_PRIV_LOCAL : AIPI_PRIV_PUBLIC;
    fn.determinism = in->deterministic ? AIPI_DET_DETERMINISTIC : AIPI_DET_NONDETERMINISTIC;
    fn.fuel_max = in->fuel_max;
    if (!cid_in(in->module, &fn.module)) return "\"module\" is not a CID";
    fn.has_contract = in->contract[0] != 0;
    if (fn.has_contract && !cid_in(in->contract, &fn.contract)) return "\"contract\" is not a CID";
    if (!name_in(fn.entry, in->entry) || !name_in(fn.checker, in->checker) ||
        !name_in(fn.certifier, in->certifier))
        return "export names are at most 63 bytes";
    int r = aipi_fn_check(&fn);
    if (r != AIPI_OK)
        return "the descriptor breaks the rules in docs/AIPI.md section 3 (AIPI_ERR_MALFORMED)";
    r = aipi_publish_fn(&Z.a, &fn, &cid);
    if (r != AIPI_OK) return aipi_why(r);
    Z.publishes++;
    zxvd_buf_raw(out, "{", 1);
    put_cid(out, "function", &cid);
    zxvd_buf_printf(out, ",\"local_only\":%s}", in->local_only ? "true" : "false");
    return NULL;
}

static const char *z_execute(const char *s, const int64_t *args, uint32_t n, bool priv,
                             zxvd_buf_t *out)
{
    ipfsn_cid_t fcid;
    uint8_t in[8u * ZXVD_AIPI_ARGS_MAX], res[AIPI_IO_MAX];
    uint32_t in_len = 0, len = 0, job = JOBS;
    char hex[2u * AIPI_IO_MAX + 1u];
    if (!cid_in(s, &fcid)) return "\"function\" is not a CID";
    if (aipi_zw_pack(args, n, in, sizeof in, &in_len) != AIPI_OK) return aipi_why(AIPI_ERR_ARG);
    Z.calls++;
    int r = aipi_execute(&Z.a, &fcid, in, in_len, priv, 0, 0, 0, &job);
    const aipi_job_t *j = job < JOBS ? aipi_job(&Z.a, job) : NULL;
    if (r == AIPI_OK && j && j->status == AIPI_JOB_DONE)
        r = aipi_result(&Z.a, job, res, sizeof res, &len);
    else if (r == AIPI_OK)
        r = AIPI_ERR_STATE; /* Z1: never pending, there is no mesh */
    if (r != AIPI_OK) {
        Z.failed++;
        if (j && j->status != AIPI_JOB_PENDING) (void) aipi_release(&Z.a, job);
        return aipi_why(r);
    }
    zxvd_buf_raw(out, "{", 1);
    put_cid(out, "function", &fcid);
    zxvd_buf_raw(out, ",", 1);
    put_cid(out, "call", &j->call_cid);
    zxvd_buf_printf(out, ",\"route\":\"%s\",\"result\":", ROUTE[j->route < 6u ? j->route : 0]);
    int64_t v[ZXVD_AIPI_ARGS_MAX];
    uint32_t nv = 0;
    if (len == 8u && aipi_zw_unpack(res, len, v, &nv) == AIPI_OK && nv == 1u)
        zxvd_buf_printf(out, "\"%lld\"", (long long) v[0]);
    else
        zxvd_buf_raw(out, "null", 4);
    (void) w4_hex_encode(res, len, hex, sizeof hex);
    zxvd_buf_raw(out, ",\"output_hex\":", 14);
    zxvd_buf_json_str(out, len ? hex : "");
    zxvd_buf_raw(out, ",", 1);
    put_tag(out, j->tag);
    zxvd_buf_raw(out, "}", 1);
    (void) aipi_release(&Z.a, job);
    return NULL;
}

static void z_status(zxvd_buf_t *out)
{
    aipi_zw_stats_t st;
    aipi_zw_stats(Z.zw, &st);
    zxvd_buf_printf(out,
                    "{\"online\":false,\"blocks\":%u,\"store_bytes\":%llu,\"store_cap\":%llu,"
                    "\"puts\":%llu,\"gets\":%llu,\"publishes\":%llu,\"calls\":%llu,\"failed\":%llu,"
                    "\"forbidden\":%u,\"sandbox\":{\"runs\":%llu,\"checks\":%llu,\"trapped\":%llu,"
                    "\"refused_module\":%llu,\"refused_contract\":%llu,\"fuel_used\":\"%llu\"}}",
                    (unsigned) Z.bs.count, (unsigned long long) Z.ms.len,
                    (unsigned long long) Z.ms.cap, (unsigned long long) Z.puts,
                    (unsigned long long) Z.gets, (unsigned long long) Z.publishes,
                    (unsigned long long) Z.calls, (unsigned long long) Z.failed,
                    (unsigned) Z.a.forbidden, (unsigned long long) st.runs,
                    (unsigned long long) st.checks, (unsigned long long) st.trapped,
                    (unsigned long long) st.refused_module,
                    (unsigned long long) st.refused_contract, (unsigned long long) st.fuel_used);
}

static const zxvd_aipi_ops_t OPS = {z_put, z_get, z_publish, z_execute, z_status};

/* ----- lifetime ----- */

/* Z2: the key that seals PRIVATE blocks, from the OS */
static bool os_key(uint8_t key[IPFSN_KEY_LEN])
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t n = fread(key, 1, IPFSN_KEY_LEN, f);
    fclose(f);
    return n == IPFSN_KEY_LEN;
}

const zxvd_aipi_ops_t *zxvd_aipi_open(void)
{
    uint8_t key[IPFSN_KEY_LEN];
    if (Z.open) return &OPS;
    memset(&Z, 0, sizeof Z);
    Z.ms.cap = ZXVD_AIPI_STORE;
    Z.ms.buf = malloc(Z.ms.cap);
    Z.tab = calloc(TAB, sizeof *Z.tab);
    Z.pins = calloc(PINS, sizeof *Z.pins);
    Z.scratch = malloc(IPFSN_SCRATCH_MIN);
    Z.net = malloc(IPFSN_BLOCK_MAX + 256u);
    Z.modbuf = malloc(AIPI_MODULE_MAX);
    Z.conbuf = malloc(AIPI_CONTRACT_MAX);
    Z.getbuf = malloc(ZXVD_AIPI_GET_MAX);
    const char *why = NULL;
    if (!Z.ms.buf || !Z.tab || !Z.pins || !Z.scratch || !Z.net || !Z.modbuf || !Z.conbuf ||
        !Z.getbuf)
        why = "out of memory";
    else if (!os_key(key))
        why = "no random key from the OS";
    if (!why) {
        ipfsn_memstore_ops(&Z.ms, &Z.sops);
        if (ipfsn_bs_init(&Z.bs, &Z.sops, Z.tab, TAB, Z.scratch, IPFSN_SCRATCH_MIN) != IPFSN_OK ||
            ipfsn_node_init(&Z.node, &Z.bs, Z.pins, PINS, Z.net, IPFSN_BLOCK_MAX + 256u) !=
                IPFSN_OK)
            why = "the blockstore could not be set up";
    }
    if (!why) {
        ipfsn_bs_set_key(&Z.bs, key);
        if (aipi_init(&Z.a, &Z.node, Z.jobs, JOBS, Z.memo, MEMO, Z.modbuf, Z.conbuf) != AIPI_OK)
            why = "the AIPI context could not be set up";
    }
    memset(key, 0, sizeof key);
    /* Z3: the sandbox's compute budget, through a BUDGET-only capability */
    if (!why && (zxv_ctx_create(&Z.ctx) != ZXV_OK ||
                 zxv_cap_create(Z.ctx, 1, NULL, ZXV_CAP_BUDGET, &Z.root) != ZXV_OK ||
                 zxv_cap_attenuate(Z.root, ZXV_CAP_BUDGET, &Z.budget_cap) != ZXV_OK ||
                 zxv_rmag_create_cap(Z.root, &Z.rm) != ZXV_OK ||
                 zxv_rmag_budget(Z.root, Z.rm, "aipi", ZXV_RES_COMPUTE_CYCLES, ZXVD_AIPI_FUEL, 1, 1,
                                 &Z.budget) != ZXV_OK ||
                 aipi_zw_create(Z.budget_cap, Z.rm, Z.budget, &Z.zw) != AIPI_OK))
        why = "the Z-Wasm sandbox could not be set up";
    if (why) {
        fprintf(stderr, "zxvd: aipi: %s\n", why);
        Z.open = true; /* so close frees what was made */
        zxvd_aipi_close();
        return NULL;
    }
    aipi_zw_ops(Z.zw, &Z.a.exec);
    Z.a.online = false; /* Z1 */
    Z.open = true;
    return &OPS;
}

void zxvd_aipi_close(void)
{
    if (!Z.open) return;
    if (Z.zw) aipi_zw_destroy(Z.zw);
    if (Z.rm) zxv_rmag_destroy(Z.rm);
    if (Z.budget_cap) zxv_cap_destroy(Z.budget_cap);
    if (Z.root) zxv_cap_destroy(Z.root);
    if (Z.ctx) zxv_ctx_destroy(Z.ctx);
    if (Z.ms.buf) memset(Z.ms.buf, 0, Z.ms.len); /* sealed or not, it was the owner's */
    free(Z.ms.buf);
    free(Z.tab);
    free(Z.pins);
    free(Z.scratch);
    free(Z.net);
    free(Z.modbuf);
    free(Z.conbuf);
    free(Z.getbuf);
    memset(&Z, 0, sizeof Z);
}
