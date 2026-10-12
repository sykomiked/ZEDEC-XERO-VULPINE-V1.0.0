/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* shim.h — C trampolines between libzxv callbacks and exported Go
 * functions. Go passes a runtime/cgo.Handle as a uintptr_t; the casts to
 * void * happen here, in C. */
#ifndef ZXV_GO_SHIM_H
#define ZXV_GO_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include "zxv_api.h"

int32_t zxv_go_generate(const zxv_cap *cap, zxv_session *session, const uint8_t *prompt,
                        size_t prompt_len, uint32_t max_tokens, int32_t temperature_q16,
                        uint32_t top_k, int32_t top_p_q16, int32_t repeat_penalty_q16,
                        uint32_t repeat_last_n, uint64_t seed, uintptr_t handle,
                        uint32_t *out_n_prompt, uint32_t *out_n_generated, int32_t *out_stop);

int32_t zxv_go_rmag_listen(zxv_rmag *rmag, uintptr_t handle);
int32_t zxv_go_rmag_unlisten(zxv_rmag *rmag);

/* ABI 1.4: provider, device and event callbacks reach Go through the
 * exported zxvGoHw* functions; handle is a cgo.Handle. */
int32_t zxv_go_hw_provider_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                               uint32_t res_id, int32_t kind, uint32_t cost, uintptr_t handle,
                               uint32_t *out_provider);
int32_t zxv_go_hw_region_create(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                uint32_t region_id, uint32_t target_class,
                                const uint8_t *target_digest, uintptr_t handle, uint32_t log_cap,
                                zxv_hw_region **out);
int32_t zxv_go_hw_region_events(const zxv_cap *cap, zxv_hw_region *rg, uintptr_t handle);

#endif
