/* cosmwasm_env.h — the CosmWasm host ABI, for the same kernel core that
 * boots on NEAR. A CosmWasm contract is wasm32 importing module "env":
 * storage through db_read/db_write (no registers — the value is copied to
 * a caller region), plus the vm helpers. This header is the whole host
 * surface the kernel sees on Cosmos chains.
 *
 * ABI (cosmwasm-vm 1.x/2.x, wasmtime):
 *   db_read(key_ptr, key_len, value_ptr, value_len) -> u32 (0/1 found)
 *     db_read copies the stored value into the caller's region.
 *   db_write(key_ptr, key_len, value_ptr, value_len)
 *   db_remove(key_ptr, key_len)
 *   db_scan(start_ptr,start_len,end_ptr,end_len,order) -> u32 handle
 *   db_next(handle, key_ptr, key_len, val_ptr, val_len) -> u32
 *   addr_validate / addr_canonicalize / addr_humanize (regions)
 *   debug(ptr, len)
 *   query_chain(ptr, len, result_ptr, result_len) -> u32
 *   secp256k1_verify / ed25519_verify (regions) -> u32
 * Exports required by the runtime: instantiate, execute, query (the entry
 * points), plus allocate/deallocate for the region ABI.
 *
 * NOTE: this shim's exact signatures must be verified against the target
 * cosmwasm-vm version before a real deploy (the ABI has drifted across
 * 0.x -> 1.x -> 2.x). It is compile-proven here; chain-verified next.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_COSMWASM_ENV_H
#define ZXV_COSMWASM_ENV_H

#include <stdint.h>

#define CW_REG_EMPTY 0u

/* storage */
__attribute__((import_module("env"), import_name("db_read")))
uint32_t cw_db_read(uint32_t key_ptr, uint32_t key_len, uint32_t val_ptr,
                    uint32_t val_len);

__attribute__((import_module("env"), import_name("db_write")))
void cw_db_write(uint32_t key_ptr, uint32_t key_len, uint32_t val_ptr,
                 uint32_t val_len);

__attribute__((import_module("env"), import_name("db_remove")))
void cw_db_remove(uint32_t key_ptr, uint32_t key_len);

/* regions: the value the runtime returns lives in the caller's memory; the
 * runtime returns its length via a u32 region handle. */
__attribute__((import_module("env"), import_name("db_scan")))
uint32_t cw_db_scan(uint32_t start_ptr, uint32_t start_len, uint32_t end_ptr,
                    uint32_t end_len, uint32_t order);

__attribute__((import_module("env"), import_name("db_next")))
uint32_t cw_db_next(uint32_t handle, uint32_t key_ptr, uint32_t key_len,
                    uint32_t val_ptr, uint32_t val_len);

__attribute__((import_module("env"), import_name("debug")))
void cw_debug(uint32_t ptr, uint32_t len);

__attribute__((import_module("env"), import_name("addr_validate")))
uint32_t cw_addr_validate(uint32_t ptr, uint32_t len, uint32_t out_ptr,
                          uint32_t out_len);

#endif /* ZXV_COSMWASM_ENV_H */