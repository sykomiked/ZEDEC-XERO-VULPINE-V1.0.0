/* m5_api.h — Complete M5 Axiomatic Kernel API for User Space Applications
 *
 * This is the single header that user applications include to access
 * all kernel services. It provides a stable ABI across kernel versions.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef M5_API_H
#define M5_API_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Core Types ===== */
typedef uint64_t m5_ordinal_t;
typedef struct { int64_t num, den; bool valid; } m5_rat_t;
typedef enum { M5_FALSE=0, M5_TRUE=1, M5_BOTH=2, M5_NEITHER=3 } m5_lpres_t;
typedef struct { double r, i; } m5_phase_t;
typedef struct { uint32_t bits[2]; } m5_collapse_t;
typedef struct { m5_ordinal_t omega; m5_rat_t r; m5_lpres_t ell; m5_phase_t iphi; m5_collapse_t chi; } m5_phase_tick_t;
typedef uint8_t m5_cid_t[32];
typedef struct { uint8_t bytes[21]; } m5_word168_t;

/* ===== 1. OSEQ — Causal Ordering ===== */
m5_ordinal_t m5_oseq_next(void);
bool m5_oseq_happens_before(m5_ordinal_t a, m5_ordinal_t b);

/* ===== 2. RMAG — Exact Rational Arithmetic ===== */
m5_rat_t m5_rat_zero(void);
m5_rat_t m5_rat_from_int(int64_t v);
m5_rat_t m5_rat_make(int64_t num, int64_t den);
m5_rat_t m5_rat_add(m5_rat_t a, m5_rat_t b);
m5_rat_t m5_rat_sub(m5_rat_t a, m5_rat_t b);
m5_rat_t m5_rat_mul(m5_rat_t a, m5_rat_t b);
m5_rat_t m5_rat_div(m5_rat_t a, m5_rat_t b);
m5_rat_t m5_rat_neg(m5_rat_t a);
bool m5_rat_eq(m5_rat_t a, m5_rat_t b);
int m5_rat_cmp(m5_rat_t a, m5_rat_t b);
bool m5_rat_is_zero(m5_rat_t a);
bool m5_rat_is_int(m5_rat_t a);
uint32_t m5_rat_to_string(m5_rat_t a, char *out, uint32_t max);
uint32_t m5_rat_to_fixed(m5_rat_t a, uint32_t places, char *out, uint32_t max, bool *exact);
bool m5_rat_split(m5_rat_t amount, uint32_t parts, m5_rat_t *out);

/* ===== 3. LPRES — Paraconsistent Logic ===== */
m5_lpres_t m5_lpres_negate(m5_lpres_t s);
m5_lpres_t m5_lpres_conjoin(m5_lpres_t a, m5_lpres_t b);
m5_lpres_t m5_lpres_disjoin(m5_lpres_t a, m5_lpres_t b);
bool m5_lpres_is_certain(m5_lpres_t s);
bool m5_lpres_is_contradictory(m5_lpres_t s);
bool m5_lpres_is_unknown(m5_lpres_t s);
bool m5_lpres_safety_gate_clear(m5_lpres_t s);  /* TRUE only */

/* ===== 4. IPHASE — Asymmetric Routing ===== */
m5_phase_t m5_iphase_add(m5_phase_t a, m5_phase_t b);
m5_phase_t m5_iphase_sub(m5_phase_t a, m5_phase_t b);
m5_phase_t m5_iphase_mul(m5_phase_t a, m5_phase_t b);
double m5_iphase_magnitude(m5_phase_t p);
double m5_iphase_argument(m5_phase_t p);
m5_phase_t m5_iphase_from_polar(double r, double phi);

/* ===== 5. CHOICE — Deterministic Collapse ===== */
m5_collapse_t m5_choice_get_state(void);
void m5_choice_set_state(const m5_collapse_t *state);
bool m5_choice_collapse(m5_collapse_t *state, uint32_t bit_idx);

/* ===== 6. Phase Coordinator — Admission Gates ===== */
typedef enum {
    M5_PC_ADMIT = 0,
    M5_PC_VETO = 1,
    M5_PC_DEFER = 2,
    M5_PC_RETRY = 3
} m5_pc_decision_t;

typedef struct {
    uint32_t phase_id;
    uint32_t step_id;
    bool requires_coverage;
    bool requires_health;
    bool is_hardware_action;
    bool is_s0_pending;
} m5_pc_step_request_t;

typedef struct {
    uint64_t token_id;
    uint32_t phase_id;
    uint32_t step_id;
    m5_pc_decision_t decision;
    uint64_t issued_at;
    uint64_t expires_at;
    bool revoked;
    bool requires_s0_resolution;
    char label[32];
} m5_pc_token_t;

m5_pc_token_t *m5_pc_admit(const m5_pc_step_request_t *req);
bool m5_pc_revoke_token(uint64_t token_id);
m5_pc_token_t *m5_pc_find_token(uint64_t token_id);
bool m5_pc_token_is_valid(uint64_t token_id);
uint64_t m5_pc_current_tick(void);

/* ===== 7. Triple Ledger (Vino) ===== */
typedef enum {
    M5_LEDGER_FINANCIAL = 0,    /* Rail 846 */
    M5_LEDGER_PROVENANCE = 1,   /* Rail 888 */
    M5_LEDGER_EXTERNALITY = 2   /* Rail 999 */
} m5_ledger_type_t;

typedef enum {
    M5_CAP_FINANCIAL = 0,
    M5_CAP_MATERIAL = 1,
    M5_CAP_KNOWLEDGE = 2,
    M5_CAP_SOCIAL = 3,
    M5_CAP_CULTURAL = 4,
    M5_CAP_SPIRITUAL = 5,
    M5_CAP_LIVING = 6,
    M5_CAP_BUILT = 7,
    M5_CAP_HUMAN = 8
} m5_capital_type_t;

typedef struct {
    uint64_t id;
    m5_rat_t amount;
    m5_capital_type_t capital;
    m5_ledger_type_t ledger;
    m5_cid_t cid;
    m5_lpres_t attestation;
    uint64_t tick;
} m5_ledger_entry_t;

typedef struct {
    uint64_t voucher_id;
    m5_capital_type_t capital;
    m5_rat_t merit_value;
    m5_rat_t coverage_ratio;
    m5_cid_t cid;
    uint64_t issued_tick;
    uint64_t expires_tick;
    bool transferable;
    bool redeemed;
} m5_voucher_t;

/* Ledger operations */
int32_t m5_ledger_post(m5_ledger_type_t ledger, const m5_rat_t *amount,
                       const m5_rat_t *ell, const m5_rat_t *phi,
                       const m5_rat_t *debit, const m5_rat_t *credit,
                       const m5_cid_t cid, const char *desc);

int32_t m5_ledger_transfer(m5_ledger_type_t ledger, uint32_t from_id, uint32_t to_id,
                           m5_capital_type_t cap, const m5_rat_t *amount,
                           const m5_rat_t *ell, const m5_rat_t *phi,
                           const char *desc);

uint64_t m5_voucher_issue(m5_capital_type_t cap, const m5_rat_t *merit_value,
                          const m5_rat_t *coverage, const char *purpose);

int32_t m5_voucher_transfer(uint64_t voucher_id, uint32_t new_holder);
int32_t m5_voucher_redeem(uint64_t voucher_id, uint32_t account_id);

bool m5_ledger_verify_coverage(uint32_t account_id);
m5_rat_t m5_ledger_account_coverage(uint32_t account_id);

/* ===== 8. Capital Forms (Nine Forms) ===== */
typedef enum {
    M5_FORM_SOCIAL = 0,
    M5_FORM_NATURAL = 1,
    M5_FORM_HERITAGE_INTELLECTUAL = 2,
    M5_FORM_GOVERNANCE_INSTITUTIONAL = 3,
    M5_FORM_FINANCIAL = 4,
    M5_FORM_MATERIAL = 5,
    M5_FORM_LIVING = 6,
    M5_FORM_KNOWLEDGE = 7,
    M5_FORM_BUILT = 8,
    M5_FORM_COUNT = 9
} m5_capital_form_t;

bool m5_capital_is_state_reserved(m5_capital_form_t form);
bool m5_capital_is_priceable(m5_capital_form_t form);
m5_capital_type_t m5_capital_form_to_vino(m5_capital_form_t form);

/* ===== 9. Derivatives ===== */
typedef enum {
    M5_DERIV_OK = 0,
    M5_DERIV_ERR_NO_BACKING = 1,
    M5_DERIV_ERR_BACKING_UNATTTESTED = 2,
    M5_DERIV_ERR_INSUFFICIENT_BACKING = 3,
    M5_DERIV_ERR_INVALID_JURISDICTION = 4,
    M5_DERIV_DEFER = 5,
    M5_DERIV_VETO = 6,
    M5_DERIV_ABSTAIN = 7,
    M5_DERIV_VETO_MAXIM = 8,
    M5_DERIV_VETO_STATE = 9
} m5_deriv_err_t;

typedef struct {
    m5_rat_t notional;
    m5_rat_t strike;
    uint64_t expiry_tick;
    m5_capital_form_t underlying_form;
    m5_cid_t backing_cid;
    m5_lpres_t backing_proof;
    uint64_t backing_verification_tick;
    m5_rat_t phase_curvature;
    m5_cid_t jurisdiction_cid;
    bool treaty_backed;
    m5_cid_t source_rail_cid;
    m5_cid_t target_rail_cid;
    uint8_t vouchers[3];
} m5_deriv_contract_t;

m5_deriv_err_t m5_deriv_verify_backing(const m5_deriv_contract_t *d);
m5_deriv_err_t m5_deriv_settle(m5_deriv_contract_t *d);
m5_deriv_err_t m5_deriv_create(m5_deriv_contract_t *d, m5_capital_form_t form,
                               const m5_rat_t *notional, const m5_rat_t *strike,
                               uint64_t expiry_tick, const m5_cid_t backing_cid,
                               const m5_lpres_t *backing_proof,
                               const m5_cid_t jurisdiction_cid, bool treaty_backed);

/* ===== 10. Assurance (Pay-It-Forward) ===== */
typedef enum {
    M5_ASSUR_OK = 0,
    M5_ASSUR_ERR_CONTRIBUTION = 1,
    M5_ASSUR_ERR_EFFICACY_UNVERIFIED = 2,
    M5_ASSUR_ERR_MAXIM_VIOLATION = 3,
    M5_ASSUR_ERR_INSUFFICIENT_GENERATION = 4,
    M5_ASSUR_ERR_FORWARD_ROUTE_INVALID = 5,
    M5_ASSUR_ERR_NO_GENERATION = 6,
    M5_ASSUR_DEFER = 7
} m5_assur_err_t;

typedef struct {
    m5_rat_t contribution;
    m5_capital_form_t target_form;
    m5_cid_t prevention_cid;
    m5_lpres_t efficacy_proof;
    m5_capital_form_t generated_form;
    m5_rat_t generation_ratio;
    uint64_t generation_tick;
    bool pay_it_forward;
    m5_cid_t forward_cid;
    m5_rat_t forward_phase;
} m5_assurance_contract_t;

m5_assur_err_t m5_assurance_admit(m5_assurance_contract_t *a);
m5_assur_err_t m5_assurance_verify_generation(m5_assurance_contract_t *a);
m5_assur_err_t m5_assurance_create(m5_assurance_contract_t *a,
                                   const m5_rat_t *contribution, m5_capital_form_t target_form,
                                   const m5_cid_t prevention_cid, const m5_lpres_t *efficacy_proof,
                                   m5_capital_form_t generated_form, const m5_rat_t *generation_ratio,
                                   bool pay_it_forward, const m5_cid_t forward_cid,
                                   const m5_rat_t *forward_phase);

/* ===== 11. Treaty Tokenization ===== */
typedef enum {
    M5_TREATY_OK = 0,
    M5_TREATY_ERR_NO_TREATY = 1,
    M5_TREATY_ERR_SOVEREIGNTY = 2,
    M5_TREATY_ERR_VALUATION = 3,
    M5_TREATY_ERR_CID_MISMATCH = 4,
    M5_TREATY_ERR_RAIL_FULL = 5
} m5_treaty_err_t;

typedef struct {
    m5_cid_t treaty_cid;
    m5_cid_t asset_cid;
    m5_capital_form_t form;
    m5_rat_t quantified_value;
    m5_lpres_t sovereignty_proof;
    m5_cid_t corridor_cid;
} m5_treaty_asset_t;

m5_treaty_err_t m5_treaty_asset_verify(const m5_treaty_asset_t *ta);
m5_treaty_err_t m5_treaty_asset_create(m5_treaty_asset_t *ta,
                                       const m5_cid_t treaty_cid, const m5_cid_t asset_cid,
                                       m5_capital_form_t form, const m5_rat_t *quantified_value,
                                       const m5_lpres_t *sovereignty_proof,
                                       const m5_cid_t corridor_cid);

/* ===== 12. Vena Runtime (Smart Contracts) ===== */
typedef enum {
    M5_LANG_ENGLISH = 0,
    M5_LANG_CHINESE = 1,
    M5_LANG_JAPANESE = 2,
    M5_LANG_KOREAN = 3,
    M5_LANG_ARABIC = 4,
    M5_LANG_HEBREW = 5,
    M5_LANG_HINDI = 6,
    M5_LANG_SANSKRIT = 7,
    M5_LANG_RUSSIAN = 8,
    M5_LANG_FRENCH = 9,
    M5_LANG_GERMAN = 10,
    M5_LANG_SPANISH = 11,
    M5_LANG_PORTUGUESE = 12,
    M5_LANG_SWAHILI = 13,
    M5_LANG_AMHARIC = 14,
    M5_LANG_NAVAJO = 15,
    M5_LANG_INUKTITUT = 16,
    M5_LANG_BASQUE = 17,
    M5_LANG_WELSH = 18,
    M5_LANG_GAELIC = 19,
    M5_LANG_KLINGON = 20,
    M5_LANG_QUENYA = 21,
    M5_LANG_DOTHRAKI = 22,
    M5_LANG_ESPERANTO = 23,
    M5_LANG_LOJBAN = 24,
    M5_LANG_ITHKUIL = 25,
    M5_LANG_TOKI_PONA = 26,
    M5_LANG_SIGN_LANG = 27,
    M5_LANG_BRAILLE = 28,
    M5_LANG_MORSE = 29,
    M5_LANG_BINARY = 30,
    M5_LANG_M5_AXIOMATIC = 31
} m5_vena_language_t;

typedef enum {
    M5_APP_SHELL = 0,
    M5_APP_EDITOR = 1,
    M5_APP_FILE_MANAGER = 2,
    M5_APP_SYSMON = 3,
    M5_APP_NET_CONFIG = 4,
    M5_APP_WALLET = 5,
    M5_APP_BANK = 6,
    M5_APP_EXCHANGE = 7,
    M5_APP_MESSAGING = 8,
    M5_APP_BROWSER = 8,
    M5_APP_TERMINAL = 9,
    M5_APP_CALCULATOR = 10,
    M5_APP_CLOCK = 11,
    M5_APP_MEDIA_PLAYER = 12,
    M5_APP_CUSTOM = 13
} m5_app_type_t;

int32_t m5_vena_register_contract(const char *name, const char *code,
                                  m5_vena_language_t lang, const char *creator);
int32_t m5_vena_execute_contract(uint32_t contract_id, const char *args,
                                 char *result, uint32_t max_result);
int32_t m5_vena_load_app(const char *name, m5_app_type_t type,
                         const char *code, m5_vena_language_t lang);
int32_t m5_vena_start_app(uint32_t app_id);
int32_t m5_vena_stop_app(uint32_t app_id);
int32_t m5_vena_list_apps(uint32_t *ids, uint32_t max_ids);

/* ===== 13. ZAB VM (Smart Contract Execution) ===== */
typedef enum {
    M5_ZAB_OK = 0,
    M5_ZAB_ERR_INVALID = 1,
    M5_ZAB_ERR_CAPABILITY = 2,
    M5_ZAB_ERR_SEAL = 3,
    M5_ZAB_ERR_EXEC = 4
} m5_zab_err_t;

typedef enum {
    M5_ZAB_CAP_NONE = 0,
    M5_ZAB_CAP_OBSERVE = 0x00000001,
    M5_ZAB_CAP_READ_STATE = 0x00000002,
    M5_ZAB_CAP_WRITE_STATE = 0x00000004,
    M5_ZAB_CAP_FS_READ = 0x00000008,
    M5_ZAB_CAP_FS_WRITE = 0x00000010,
    M5_ZAB_CAP_LEDGER = 0x00000020,
    M5_ZAB_CAP_NET = 0x00000040,
    M5_ZAB_CAP_SPAWN = 0x00000080,
    M5_ZAB_CAP_EMIT = 0x00000100
} m5_zab_cap_t;

m5_zab_err_t m5_zab_derive_capabilities(const uint8_t *code, uint32_t len, uint32_t *out_caps);
m5_zab_err_t m5_zab_execute(const uint8_t *code, uint32_t len, uint32_t granted_caps,
                            void *host_ctx, void *host_read, void *host_write, void *host_observe);

/* ===== 14. Apps Framework ===== */
typedef enum {
    M5_APP_STATE_IDLE = 0,
    M5_APP_STATE_RUNNING = 1,
    M5_APP_STATE_WAITING = 2,
    M5_APP_STATE_ERROR = 3
} m5_app_state_t;

typedef struct {
    m5_app_type_t type;
    m5_app_state_t state;
    uint32_t window_id;
    bool active;
    char name[32];
} m5_app_base_t;

int32_t m5_app_launcher_open(m5_app_type_t type);
int32_t m5_app_launcher_close(m5_app_type_t type);
void m5_app_launcher_handle_key(char ch, uint8_t scancode);
void m5_app_launcher_render(void);
void m5_app_launcher_tick(void);

/* ===== 15. System Calls (Direct Kernel Interface) ===== */
typedef enum {
    M5_SYS_EXIT = 0,
    M5_SYS_GETPID = 1,
    M5_SYS_YIELD = 2,
    M5_SYS_WRITE = 3,
    M5_SYS_READ = 4,
    M5_SYS_EXEC = 5,
    M5_SYS_SLEEP = 6,
    M5_SYS_SEND = 7,
    M5_SYS_RECV = 8,
    M5_SYS_OPEN = 9,
    M5_SYS_CLOSE = 10
} m5_syscall_t;

int64_t m5_syscall(m5_syscall_t num, uint64_t arg0, uint64_t arg1,
                   uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

/* ===== 16. IPC ===== */
typedef struct {
    uint32_t sender_pid;
    uint32_t length;
    uint8_t payload[256];
} m5_ipc_msg_t;

int32_t m5_ipc_send(uint32_t dest_pid, const uint8_t *payload, uint32_t length);
int32_t m5_ipc_recv(uint32_t sender_pid, uint8_t *buffer, uint32_t max_len);

/* ===== 17. VFS ===== */
typedef struct {
    uint32_t id;
    char name[64];
    bool is_dir;
    uint64_t size;
    uint64_t modified_tick;
} m5_vfs_node_t;

int32_t m5_vfs_list_dir(const char *path, m5_vfs_node_t *entries, uint32_t max);
int32_t m5_vfs_read_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *out_len);
int32_t m5_vfs_write_file(const char *path, const uint8_t *buf, uint32_t len);
int32_t m5_vfs_create_dir(const char *path);
int32_t m5_vfs_delete(const char *path);

/* ===== 18. Network / Mesh ===== */
typedef struct {
    uint32_t id;
    char name[32];
    bool up;
    uint32_t type;  /* 0=loopback, 1=ethernet, 2=mesh */
    m5_cid_t peer_cid;
} m5_net_iface_t;

int32_t m5_net_list_ifaces(m5_net_iface_t *ifaces, uint32_t max);
int32_t m5_mesh_route_price(const m5_cid_t dest_cid, m5_rat_t *out_price);

/* ===== 19. GUI ===== */
typedef enum {
    M5_ATTR_NORMAL = 0,
    M5_ATTR_BRIGHT = 1,
    M5_ATTR_DIM = 2,
    M5_ATTR_RED = 3,
    M5_ATTR_GREEN = 4,
    M5_ATTR_YELLOW = 5,
    M5_ATTR_BLUE = 6,
    M5_ATTR_CYAN = 7,
    M5_ATTR_MAGENTA = 8,
    M5_ATTR_WHITE = 9
} m5_attr_t;

void m5_gui_write(uint32_t window, const char *text);
void m5_gui_write_attr(uint32_t window, const char *text, m5_attr_t attr);
void m5_gui_newline(uint32_t window);
void m5_gui_clear(uint32_t window);

/* ===== 20. Self-Audit ===== */
typedef enum {
    M5_AUDIT_PASS = 0,
    M5_AUDIT_WARN = 1,
    M5_AUDIT_FAIL = 2,
    M5_AUDIT_VETO = 3
} m5_audit_result_t;

m5_audit_result_t m5_self_audit(void);

/* ===== 21. Assertions ===== */
#define M5_ASSERT(cond, msg) do { if (!(cond)) { m5_assert_fail(msg, __FILE__, __LINE__); } } while(0)
void m5_assert_fail(const char *msg, const char *file, int line);

#endif /* M5_API_H */
