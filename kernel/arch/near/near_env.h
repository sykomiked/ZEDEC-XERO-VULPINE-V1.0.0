/* near_env.h — the NEAR host ABI, declared for a freestanding wasm32 build.
 *
 * A NEAR contract is a wasm32 module that imports nothing but this "env"
 * module, exports its methods, and gets fresh memory on every call: nothing
 * survives between transactions except what is written through
 * env.storage_write. This header is the whole host surface the kernel sees
 * on this target — no WASI, no sockets, no clocks.
 *
 * The register model: env.input() / env.signer_account_id() /
 * env.current_account_id() / env.attached_deposit() place bytes in a
 * runtime register; env.register_len() reports its length and
 * env.read_register() copies it out. Storage keys and values are plain
 * pointers (the kernel's own buffers), with lengths as u64.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_NEAR_ENV_H
#define ZXV_NEAR_ENV_H

#include <stdint.h>

#define NEAR_REG_EMPTY 0xFFFFFFFFFFFFFFFFull

/* storage_read: length of the stored value, or NEAR_REG_EMPTY when the
 * key is absent. The value itself is copied into the given register. */
__attribute__((import_module("env"), import_name("storage_read")))
uint64_t env_storage_read(uint64_t key_len, uint64_t key_ptr, uint64_t reg);

/* storage_write: 0 when the key was new, 1 when it overwrote. */
__attribute__((import_module("env"), import_name("storage_write")))
uint64_t env_storage_write(uint64_t key_len, uint64_t key_ptr,
                           uint64_t val_len, uint64_t val_ptr, uint64_t reg);

__attribute__((import_module("env"), import_name("storage_has_key")))
uint64_t env_storage_has_key(uint64_t key_len, uint64_t key_ptr);

__attribute__((import_module("env"), import_name("register_len")))
uint64_t env_register_len(uint64_t reg);

__attribute__((import_module("env"), import_name("read_register")))
void env_read_register(uint64_t reg, uint64_t ptr);

__attribute__((import_module("env"), import_name("input")))
void env_input(uint64_t reg);

__attribute__((import_module("env"), import_name("signer_account_id")))
void env_signer_account_id(uint64_t reg);

__attribute__((import_module("env"), import_name("current_account_id")))
void env_current_account_id(uint64_t reg);

__attribute__((import_module("env"), import_name("attached_deposit")))
void env_attached_deposit(uint64_t reg);

__attribute__((import_module("env"), import_name("prepaid_gas")))
uint64_t env_prepaid_gas(void);

__attribute__((import_module("env"), import_name("block_index")))
uint64_t env_block_index(void);

__attribute__((import_module("env"), import_name("block_timestamp")))
uint64_t env_block_timestamp(void);

__attribute__((import_module("env"), import_name("value_return")))
void env_value_return(uint64_t len, uint64_t ptr);

__attribute__((import_module("env"), import_name("panic_utf8")))
void env_panic_utf8(uint64_t len, uint64_t ptr) __attribute__((noreturn));

__attribute__((import_module("env"), import_name("log_utf8")))
void env_log_utf8(uint64_t len, uint64_t ptr);

__attribute__((import_module("env"), import_name("log_utf8")))
void env_log_utf8(uint64_t len, uint64_t ptr);

#endif /* ZXV_NEAR_ENV_H */