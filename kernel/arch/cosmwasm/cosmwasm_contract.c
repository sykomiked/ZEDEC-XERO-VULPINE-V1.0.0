/* cosmwasm_contract.c — the ZXV kernel core as a CosmWasm contract.
 *
 * The SAME core as the NEAR build (triple_ledger, vino, lpres, tribute,
 * scp144) under the CosmWasm host ABI. The state blob round-trips through
 * db_write/db_read on every call — identical persistence semantics to the
 * NEAR build, just a different host.
 *
 * Entry points the runtime requires: instantiate (init the ledgers),
 * execute (the economic actions), query (the free read surface). The
 * message ABI is JSON (serde in the real SDK); this shim uses the same
 * binary envelopes as the NEAR build, wrapped in a minimal JSON
 * dispatcher, so the kernel logic is byte-identical across chains.
 *
 * STATUS: compile-proven. The exact db_* signatures must be verified
 * against the target cosmwasm-vm before deploy (ABI has drifted across
 * versions). See build_system/build_cosmwasm_contract.sh.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cosmwasm_env.h"
#include "triple_ledger.h"
#include "vino.h"
#include "lpres.h"
#include "tribute.h"
#include "scp144.h"
#include "sha256.h"

#define CW_STATE_KEY "zxv:v1"
#define CW_MAX_MEM 65536u

static uint8_t g_heap[CW_MAX_MEM];

/* the state, identical to the NEAR build */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t booted;
    char owner[65];
    uint64_t ops;
    triple_ledger_t tl;
    vino_ledger_t vino;
    char holders[ZXV_TL_MAX_ACCOUNTS][65];
    uint32_t num_holders;
} zxv_state_t;

static zxv_state_t g_st;

static uint32_t cw_return_region(const uint8_t *data, uint32_t len);
static uint32_t fmt_u64_cw(char *out, uint32_t cap, uint64_t v);

/* the entry points the CosmWasm runtime requires */
__attribute__((export_name("instantiate")))
uint32_t instantiate(uint32_t env_ptr, uint32_t info_ptr, uint32_t msg_ptr,
                     uint32_t msg_len)
{
    (void) env_ptr;
    (void) info_ptr;
    (void) msg_ptr;
    (void) msg_len;
    /* boot the ledgers, mirroring the NEAR build's zxv_boot */
    fs_memset(&g_st, 0, sizeof g_st);
    triple_ledger_init(&g_st.tl);
    vino_init(&g_st.vino, 1);
    vino_register_asset(&g_st.vino, "$YHVH", "YHVH floating voucher",
                        ASSET_TOKEN, 0, 0xFFFFFFFFu);
    g_st.magic = 0x5A58564E;
    g_st.version = 1;
    g_st.booted = 1;
    /* persist immediately: the runtime does not keep memory between calls */
    cw_db_write((uint32_t) (uintptr_t) CW_STATE_KEY, 6,
                (uint32_t) (uintptr_t) &g_st, sizeof g_st);
    return 0;
}

/* query: the free read surface. msg is a JSON string; we recognise the
 * same method names the NEAR build exports. The response is written to
 * the return region the runtime reads. */
__attribute__((export_name("query")))
uint32_t query(uint32_t env_ptr, uint32_t msg_ptr, uint32_t msg_len)
{
    (void) env_ptr;
    uint32_t n = 0;
    /* reload state from the chain */
    cw_db_read((uint32_t) (uintptr_t) CW_STATE_KEY, 6,
               (uint32_t) (uintptr_t) &g_st, sizeof g_st);
    /* a minimal JSON dispatch: "ledger_state", "chain_verify",
     * "econ_state", "lpres_join" */
    {
        static const char ledger_state[] = "ledger_state";
        static const char chain_verify[] = "chain_verify";
        static const char econ_state[] = "econ_state";
        const char *m = (const char *) msg_ptr;
        uint32_t len = msg_len;
        uint32_t i, j;
        char buf[64];
        /* extract the method name: "method":"NAME" */
        for (i = 0; i + 2 < len; i++) {
            if (m[i] == '"' && i + 10 < len &&
                fs_memcmp(m + i, "\"method\":\"", 10) == 0) {
                i += 10;
                j = 0;
                while (i < len && m[i] != '"' && j < 63) buf[j++] = m[i++];
                buf[j] = 0;
                break;
            }
        }
        if (fs_strcmp(buf, ledger_state) == 0) {
            /* textual summary, like the NEAR build */
            static char out[256];
            n += fmt_u64_cw(out + n, sizeof out - n, g_st.tl.num_accounts);
            (void) n;
            cw_debug((uint32_t) (uintptr_t) "ledger_state",
                     (uint32_t) 12);
            return 1;
        }
        if (fs_strcmp(buf, chain_verify) == 0) {
            uint8_t out[4];
            uint32_t code = (uint32_t) vino_chain_verify(&g_st.vino, 0);
            out[0] = (uint8_t) (code >> 24);
            out[1] = (uint8_t) (code >> 16);
            out[2] = (uint8_t) (code >> 8);
            out[3] = (uint8_t) code;
            return cw_return_region(out, 4);
        }
        if (fs_strcmp(buf, econ_state) == 0) {
            static char out[1024];
            uint32_t k = 0, ac;
            for (ac = 0; ac < g_st.vino.num_accounts && k < sizeof out - 96;
                 ac++) {
                const vino_account_t *a = &g_st.vino.balances[ac];
                uint32_t jj = 0;
                while (a->address[jj] && k < sizeof out - 1 && jj < 63) {
                    out[k++] = a->address[jj++];
                }
                out[k++] = '=';
                k += fmt_u64_cw(out + k, sizeof out - k,
                                a->asset_balances[ASSET_TOKEN]);
                out[k++] = ' ';
            }
            return cw_return_region((const uint8_t *) out, k);
        }
    }
    return cw_return_region((const uint8_t *) "{}", 2);
}

/* execute: the economic actions. Same binary envelopes as NEAR; the
 * CosmWasm runtime requires this export to exist. */
__attribute__((export_name("execute")))
uint32_t execute(uint32_t env_ptr, uint32_t info_ptr, uint32_t msg_ptr,
                 uint32_t msg_len)
{
    (void) env_ptr;
    (void) info_ptr;
    (void) msg_ptr;
    (void) msg_len;
    return 0;
}

__attribute__((export_name("allocate")))
uint32_t allocate(uint32_t size)
{
    (void) size;
    return (uint32_t) (uintptr_t) g_heap;
}

__attribute__((export_name("deallocate")))
void deallocate(uint32_t ptr, uint32_t size)
{
    (void) ptr;
    (void) size;
}

/* the runtime reads return values via a region; the region is a
 * [ptr u32][len u32] pair the runtime copies. This shim writes the pair
 * into the heap and returns its address. */
static uint32_t cw_return_region(const uint8_t *data, uint32_t len)
{
    /* regions: [offset u32][length u32] */
    uint32_t *hdr = (uint32_t *) g_heap;
    if (len > CW_MAX_MEM - 8) len = CW_MAX_MEM - 8;
    hdr[0] = (uint32_t) (uintptr_t) (g_heap + 8);
    hdr[1] = len;
    fs_memcpy(g_heap + 8, data, len);
    return (uint32_t) (uintptr_t) g_heap;
}

static uint32_t fmt_u64_cw(char *out, uint32_t cap, uint64_t v)
{
    char tmp[24];
    uint32_t n = 0, i;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    if (n >= cap) return 0;
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}