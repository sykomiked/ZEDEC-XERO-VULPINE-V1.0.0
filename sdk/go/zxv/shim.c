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
