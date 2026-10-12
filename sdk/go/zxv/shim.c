/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "shim.h"
#include "_cgo_export.h"

static int32_t token_trampoline(void *user, int32_t token, const uint8_t *piece, size_t len)
{
    return zxvGoOnToken((uintptr_t) user, token, (uint8_t *) piece, len);
}

int32_t zxv_go_generate(const zxv_cap *cap, zxv_session *session, const uint8_t *prompt,
                        size_t prompt_len, uint32_t max_tokens, int32_t temperature_q16,
                        uint32_t top_k, int32_t top_p_q16, int32_t repeat_penalty_q16,
                        uint32_t repeat_last_n, uint64_t seed, uintptr_t handle,
                        uint32_t *out_n_prompt, uint32_t *out_n_generated, int32_t *out_stop)
{
    return zxv_generate(cap, session, prompt, prompt_len, max_tokens, temperature_q16, top_k,
                        top_p_q16, repeat_penalty_q16, repeat_last_n, seed, token_trampoline,
                        (void *) handle, out_n_prompt, out_n_generated, out_stop);
}

static void rmag_trampoline(void *user, uint32_t budget, int32_t event)
{
    zxvGoRmagEvent((uintptr_t) user, budget, event);
}

int32_t zxv_go_rmag_listen(zxv_rmag *rmag, uintptr_t handle)
{
    return zxv_rmag_listen(rmag, rmag_trampoline, (void *) handle);
}

int32_t zxv_go_rmag_unlisten(zxv_rmag *rmag)
{
    return zxv_rmag_listen(rmag, NULL, NULL);
}

/* ----- ABI 1.4 ----- */

static int32_t hw_call_trampoline(void *user, uint32_t op, const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len)
{
    return zxvGoHwCall((uintptr_t) user, op, (uint8_t *) in, in_len, out, out_cap, out_len);
}

int32_t zxv_go_hw_provider_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                               uint32_t res_id, int32_t kind, uint32_t cost, uintptr_t handle,
                               uint32_t *out_provider)
{
    return zxv_hw_provider_add(cap, mx, fn_id, res_id, kind, cost, hw_call_trampoline,
                               (void *) handle, out_provider);
}

static int32_t hw_load_trampoline(void *user, uint32_t slot, const uint8_t *payload, size_t len)
{
    return zxvGoHwLoad((uintptr_t) user, slot, (uint8_t *) payload, len);
}

static int32_t hw_readback_trampoline(void *user, uint32_t slot, uint8_t digest[32])
{
    return zxvGoHwReadback((uintptr_t) user, slot, digest);
}

static int32_t hw_activate_trampoline(void *user, uint32_t slot)
{
    return zxvGoHwActivate((uintptr_t) user, slot);
}

int32_t zxv_go_hw_region_create(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                uint32_t region_id, uint32_t target_class,
                                const uint8_t *target_digest, uintptr_t handle, uint32_t log_cap,
                                zxv_hw_region **out)
{
    return zxv_hw_region_create_cap(cap, mx, provider, region_id, target_class, target_digest,
                                    hw_load_trampoline, hw_readback_trampoline,
                                    hw_activate_trampoline, (void *) handle, log_cap, out);
}

static void hw_event_trampoline(void *user, uint32_t region_id, int32_t event, uint32_t reason,
                                uint32_t version)
{
    zxvGoHwEvent((uintptr_t) user, region_id, event, reason, version);
}

int32_t zxv_go_hw_region_events(const zxv_cap *cap, zxv_hw_region *rg, uintptr_t handle)
{
    return zxv_hw_region_events(cap, rg, hw_event_trampoline, (void *) handle);
}
