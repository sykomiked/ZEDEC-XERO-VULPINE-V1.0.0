/* cellular_multikernel.h — ZXV Cellular Multikernel Fabric (CELL-001)
 *
 * Cell contract, control plane, data plane, and phase placement for
 * independently fault-contained kernel cells across homogeneous and
 * heterogeneous compute domains. Cells coordinate through the versioned
 * Event ABI; the fabric does not assume one cell per core, shared
 * memory consensus, or that cellularity alone provides security.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef CELLULAR_MULTIKERNEL_H
#define CELLULAR_MULTIKERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- 168-bit logical value (CRIT-168 transport profile) ----
 * Canonical 21-octet representation. Higher-level views (septets,
 * sextets) are derived by transformation, not by separate storage. */
#define CRIT168_BYTE_COUNT 21
#define CRIT168_BIT_COUNT  168

typedef struct {
    uint8_t b[CRIT168_BYTE_COUNT];
} crit168_value_t;

/* ---- Cell architecture / bitness profile ---- */
typedef enum {
    CELL_ARCH_ARM64,
    CELL_ARCH_X86_64,
    CELL_ARCH_RISCV64,
    CELL_ARCH_RISCV32,
    CELL_ARCH_ARM32,
    CELL_ARCH_I386,
    CELL_ARCH_DSP,
    CELL_ARCH_NPU_GPU,
    CELL_ARCH_FPGA_ASIC,
    CELL_ARCH_OTHER
} cell_architecture_t;

typedef enum {
    CELL_BITNESS_32,
    CELL_BITNESS_64,
    CELL_BITNESS_128_VECTOR,
    CELL_BITNESS_168_NATIVE
} cell_bitness_t;

typedef enum {
    CELL_TRUST_KERNEL,
    CELL_TRUST_DEVICE,
    CELL_TRUST_EXTERNAL,
    CELL_TRUST_QUARANTINE
} cell_trust_domain_t;

typedef enum {
    CELL_PRIVILEGE_EL1,
    CELL_PRIVILEGE_EL2,
    CELL_PRIVILEGE_EL0,
    CELL_PRIVILEGE_RING0,
    CELL_PRIVILEGE_RING3,
    CELL_PRIVILEGE_ACCELERATOR
} cell_privilege_level_t;

/* ---- Cell lifecycle ---- */
typedef enum {
    CELL_STATE_DISCOVERED,
    CELL_STATE_AUTHENTICATED,
    CELL_STATE_QUARANTINED,
    CELL_STATE_ADMITTED,
    CELL_STATE_ACTIVE,
    CELL_STATE_DEGRADED,
    CELL_STATE_FAILED,
    CELL_STATE_RECOVERING,
    CELL_STATE_REVOKED
} cell_state_t;

/* ---- Health state ---- */
typedef enum {
    CELL_HEALTH_OK,
    CELL_HEALTH_DEGRADED,
    CELL_HEALTH_FAIL_STOPPED,
    CELL_HEALTH_RECOVERING,
    CELL_HEALTH_REVOKED
} cell_health_t;

/* ---- Transport types ---- */
typedef enum {
    CELL_TRANSPORT_CACHED_RING,
    CELL_TRANSPORT_NONCOHERENT_QUEUE,
    CELL_TRANSPORT_MAILBOX,
    CELL_TRANSPORT_PCIE_CXL,
    CELL_TRANSPORT_NOC,
    CELL_TRANSPORT_NETWORK,
    CELL_TRANSPORT_COUNT
} cell_transport_type_t;

/* ---- Capabilities bit mask ---- */
#define CELL_CAP_OSEQ      (1u << 0)
#define CELL_CAP_RMAG      (1u << 1)
#define CELL_CAP_LPRES     (1u << 2)
#define CELL_CAP_IPHASE    (1u << 3)
#define CELL_CAP_CHOICE    (1u << 4)
#define CELL_CAP_PHASECOORD (1u << 5)
#define CELL_CAP_SCHEDULER  (1u << 6)
#define CELL_CAP_NETWORK    (1u << 7)
#define CELL_CAP_AUDIO      (1u << 8)
#define CELL_CAP_GRAPHICS   (1u << 9)
#define CELL_CAP_CRYPTO     (1u << 10)
#define CELL_CAP_VINO       (1u << 11)

#define CELL_MAX_SCHEMAS 8
#define CELL_MAX_ENDPOINTS 4
#define CELL_MAX_DIGEST 32
#define CELL_NAME_LEN 16
#define CELL_MAX_EVENTS 64

/* ---- Cell execution backend ----
 * Platform-agnostic hooks to create and terminate a separate execution
 * context (task/process) for a cell. The fabric itself does not contain
 * scheduler code; it delegates to whichever scheduler the platform uses. */
typedef struct cell_execution_backend {
    /* Create a new execution context. On success return >= 0 and set
     * *task_id; on failure return < 0. */
    int32_t (*create)(const char *name, void (*entry)(void *arg), void *arg,
                      uint32_t *task_id);
    /* Terminate an execution context created by this backend. */
    void (*terminate)(uint32_t task_id);
} cell_execution_backend_t;

/* ---- Event envelope (simplified fabric view) ----
 * The full event envelope is owned by EVT-001. This is the subset
 * the fabric needs for routing, sequence/replay checks, and tri-space
 * commit tracking. */
typedef struct {
    uint64_t event_id;
    uint64_t ordinal;
    uint64_t causal_parent;
    uint64_t triad_id;
    uint32_t phase_id;
    uint32_t phase_version;
    crit168_value_t evidence_digest;
    crit168_value_t s_plus_digest;
    crit168_value_t s_minus_digest;
    crit168_value_t s_zero_digest;
    uint32_t resource_charge;
    uint8_t lane;  /* 0=S+, 1=S-, 2=S0 */
    uint8_t decision; /* 0=defer, 1=commit, 2=reject, 3=compensate */
} fabric_event_t;

/* ---- Transport endpoint ---- */
typedef struct {
    cell_transport_type_t type;
    uint64_t local_addr;   /* local queue/ring base */
    uint64_t peer_addr;    /* peer queue/ring base when shared */
    uint32_t capacity;     /* bounded queue capacity */
    uint32_t head;
    uint32_t tail;
    uint32_t sequence;     /* for replay/stale checks */
    uint32_t incarnation;  /* cell incarnation at setup */
    bool authenticated;
    bool requires_rollback;
} cell_transport_endpoint_t;

/* ---- Resource budget ---- */
typedef struct {
    uint64_t max_events_per_second;
    uint64_t max_memory_bytes;
    uint64_t max_cpu_mhz;
    uint32_t max_priority;
    uint32_t min_priority;
} cell_resource_budget_t;

/* ---- Cell contract ---- */
typedef struct {
    char cell_id[CELL_NAME_LEN];
    cell_architecture_t arch;
    cell_bitness_t bitness;
    cell_trust_domain_t trust;
    cell_privilege_level_t privilege;
    uint32_t incarnation;
    uint32_t capabilities;          /* bitmask of CELL_CAP_* */
    uint32_t supported_schema_ids[CELL_MAX_SCHEMAS];
    uint8_t  supported_schema_count;
    uint64_t memory_ownership_base;
    uint64_t memory_ownership_size;
    cell_transport_endpoint_t transports[CELL_MAX_ENDPOINTS];
    uint8_t transport_count;
    cell_resource_budget_t budget;
    cell_health_t health;
    cell_state_t state;
    uint8_t firmware_or_kernel_digest[CELL_MAX_DIGEST];
    uint64_t admitted_at_ordinal;
    uint64_t revoked_at_ordinal;
    /* Execution context (separate protected cell) */
    void (*entry_point)(void *arg);
    void *entry_arg;
    uint32_t task_id;
    bool is_executing;
} cell_t;

/* ---- Routing table entry ---- */
typedef struct {
    uint32_t phase_id;
    uint32_t phase_version;
    char target_cell[CELL_NAME_LEN];
    uint32_t priority;
    bool requires_admission_token;
} cell_route_t;

/* ---- Control plane ---- */
#define CELL_MAX_CELLS 8
#define CELL_MAX_ROUTES 32

typedef struct {
    cell_t cells[CELL_MAX_CELLS];
    uint8_t cell_count;
    cell_route_t routes[CELL_MAX_ROUTES];
    uint8_t route_count;
    uint64_t next_ordinal;
    uint32_t control_plane_incarnation;
    bool has_replica;          /* for server profile */
    bool has_local_replica;    /* for consumer profile */
    const cell_execution_backend_t *execution; /* optional execution backend */
} cell_fabric_t;

/* ---- Tri-space commit state ---- */
typedef struct {
    uint64_t triad_id;
    bool s_plus_ok;
    bool s_minus_ok;
    bool s_zero_ok;
    bool s_zero_resolved;
    bool lane_received[3]; /* 0=S+, 1=S-, 2=S0 */
    uint64_t veto_deadline_ordinal;
    uint8_t commit_decision; /* 0=undecided, 1=commit, 2=reject, 3=compensate */
} triad_commit_state_t;

/* ---- Fabric API ---- */

/* Initialize the fabric (control plane) */
void cell_fabric_init(cell_fabric_t *fabric);

/* Reset a cell contract to zero */
void cell_contract_zero(cell_t *cell);

/* Set the firmware/kernel digest for a cell */
bool cell_set_digest(cell_t *cell, const uint8_t digest[CELL_MAX_DIGEST]);

/* Control plane: topology discovery / admission / recovery replacement */
bool cell_fabric_discover(cell_fabric_t *fabric, const cell_t *candidate);
bool cell_fabric_authenticate(cell_fabric_t *fabric, const char *cell_id,
                              const uint8_t expected_digest[CELL_MAX_DIGEST]);
cell_state_t cell_fabric_admit(cell_fabric_t *fabric, const char *cell_id);
bool cell_fabric_revoke(cell_fabric_t *fabric, const char *cell_id);

/* Control plane: admission lifecycle */
bool cell_fabric_activate(cell_fabric_t *fabric, const char *cell_id);

/* Bind an execution backend to the fabric. */
void cell_fabric_set_backend(cell_fabric_t *fabric,
                             const cell_execution_backend_t *backend);

/* Set the entry point for a cell before execution. */
bool cell_fabric_set_entry(cell_t *cell, void (*entry)(void *arg), void *arg);

/* Execute an admitted cell as a separately protected execution context.
 * Requires an execution backend to be registered, or a caller-supplied
 * entry point. Returns the resulting state. */
cell_state_t cell_fabric_execute(cell_fabric_t *fabric, const char *cell_id,
                                 void (*entry)(void *arg), void *arg);

/* Terminate a cell's execution context and revoke it. */
bool cell_fabric_terminate(cell_fabric_t *fabric, const char *cell_id);

/* Control plane: routing */
bool cell_fabric_add_route(cell_fabric_t *fabric, const cell_route_t *route);
const char *cell_fabric_route_lookup(cell_fabric_t *fabric, uint32_t phase_id,
                                      uint32_t phase_version);

/* Data plane: send/receive an event through a transport endpoint */
bool cell_transport_send(cell_transport_endpoint_t *ep, const fabric_event_t *ev);
bool cell_transport_receive(cell_transport_endpoint_t *ep, fabric_event_t *ev);

/* Tri-space commit protocol */
void triad_commit_reset(triad_commit_state_t *st, uint64_t triad_id,
                        uint64_t veto_deadline_ordinal);
bool triad_commit_update(triad_commit_state_t *st, const fabric_event_t *ev);

/* CRIT-168 helpers */
void crit168_zero(crit168_value_t *v);
bool crit168_eq(const crit168_value_t *a, const crit168_value_t *b);
void crit168_from_digest(crit168_value_t *v, const uint8_t *data, size_t len);

/* Fault model: mark a cell failed and revoke its capabilities */
bool cell_fabric_handle_fault(cell_fabric_t *fabric, const char *cell_id,
                              cell_health_t new_health);

/* Diagnostic: health summary string */
void cell_fabric_health_summary(const cell_fabric_t *fabric, char *out, size_t out_len);

#endif /* CELLULAR_MULTIKERNEL_H */
