/* mirror_ui.h — the SASOS mirror projection: kernel state, projected losslessly.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE MIRROR. In the megalithic (single-address-space) kernel there is no
 * user space to serialise for: every subsystem — the memory rings, the task
 * queues, the crypto trust boundaries, the tri-space valences, the lpres
 * four-valued states — lives in the SAME address space as the UI. So the UI
 * does not ASK the kernel what its state is; it points AT the kernel's state
 * registers and reads them directly, at the phase tick. This header defines
 * that binding: a bounded table of state registers (direct pointers), a
 * bounded lockfree change-notify ring, and a deterministic projection into a
 * render frame that is a function of (registers, tick) alone — identical
 * inputs and phase ordinals always produce identical visual frames.
 *
 * FAIL CLOSED. A register whose pointer is NULL, or whose kind is unknown,
 * projects as valence NEITHER (unknown, dimmed) — never as a guessed TRUE or
 * FALSE. A full notify ring is backpressure, not data loss: the push is
 * refused and the tick's frame carries a full flag the renderer must show.
 *
 * EXACT ONLY. No float anywhere: coverage, valence and depth are integers or
 * fixed-point (Q16.16). lpres states are lpres_state_t, never bools.
 */
#ifndef ZXV_MIRROR_UI_H
#define ZXV_MIRROR_UI_H

#include <stdint.h>
#include <stdbool.h>

#include "lpres.h"     /* lpres_state_t — the four-valued truth type      */
#include "trispace.h"  /* tri_role_t — S+ / S- / S0 valences              */
#include "zxv_fixed.h" /* Q16_ONE — fixed-point coverage                   */

/* --gc-sections KEEP. Same reasoning (and spelling) as tvl_rom.c's TVL_KEEP:
 * adding a file to KERNEL_SRCS is NECESSARY BUT NOT SUFFICIENT — the arch
 * links run -Wl,--gc-sections, so a module nothing references is DISCARDED
 * from the ELF. The mirror has no in-kernel caller until the boot path calls
 * mirror_project() (desktop/zui bring-up); until then its entry points are
 * marked retained. Drop the macro the moment kernel_main calls it. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define MIRROR_KEEP __attribute__((used, retain))
#else
#  define MIRROR_KEEP __attribute__((used))
#endif

/* ---- bounds (bounded, freestanding) -------------------------------------- */

#define MIRROR_MAX_REGS   64u  /* state registers the UI may bind         */
#define MIRROR_RING_CAP   256u /* change-notify ring slots                */
#define MIRROR_NAME_LEN   32u
#define MIRROR_PROJ_DEPTH 16u /* max tri-space depth per frame           */

/* ---- what a register IS --------------------------------------------------- */

typedef enum {
    MIRROR_KIND_RING = 0,  /* a bounded memory ring: head, tail, capacity   */
    MIRROR_KIND_QUEUE = 1, /* a task queue: depth, high-water               */
    MIRROR_KIND_TRUST = 2, /* a crypto trust boundary: sealed, verified     */
    MIRROR_KIND_TRIS = 3,  /* a tri-space valence: S+ / S- / S0 with depth  */
    MIRROR_KIND_LPRES = 4, /* a four-valued logic state                     */
    MIRROR_KIND_NET = 5,   /* network-space topology: peers up / total      */
    MIRROR_KIND_CID = 6,   /* content-addressed identity: verified, pinned  */
    MIRROR_KIND_PQ = 7,    /* post-quantum session: decapsulated, authed    */
    MIRROR_KIND_CLOCK = 8, /* distributed logical clock: local vs peer max  */
    MIRROR_KIND_COUNT
} mirror_kind_t;

/* One bound register: a DIRECT POINTER into kernel state. The UI reads
 * through this pointer at projection time — zero copy, zero serialisation.
 * `seed` is a per-register constant so the deterministic frame folds distinct
 * registers into distinct fields (two registers with identical values must
 * not collide in the frame digest). */
typedef struct {
    mirror_kind_t kind;
    char name[MIRROR_NAME_LEN];
    const void *ptr; /* NULL -> projects NEITHER, fail closed       */
    uint32_t seed;   /* per-register fold constant                  */
} mirror_reg_t;

/* The register payloads the projector understands. Each kind has exactly one
 * view; the projector refuses any other shape (fail closed). */
typedef struct {
    uint32_t head;
    uint32_t tail;
    uint32_t capacity; /* 0 -> unknown, projects NEITHER              */
} mirror_ring_view_t;

typedef struct {
    uint32_t depth;
    uint32_t high_water;
} mirror_queue_view_t;

typedef struct {
    uint32_t sealed;   /* 1 = sealed, 0 = open                        */
    uint32_t verified; /* 1 = verified, 0 = unverified                */
} mirror_trust_view_t;

typedef struct {
    tri_role_t role; /* S+ / S- / S0                                */
    uint32_t depth;  /* 0..MIRROR_PROJ_DEPTH                        */
} mirror_tris_view_t;

typedef struct {
    lpres_state_t state; /* NEITHER / TRUE / FALSE / BOTH / phases      */
} mirror_lpres_view_t;

/* ---- network-space views ---------------------------------------------------
 * The triadic mirror's third space: the mesh's topology, identity, sessions
 * and clocks project with the SAME valences as the kernel's own rings — the
 * network is the system topology across physical distance, not a transport
 * layer. These views are INTEGER-only: the PQ/oseq code that owns the real
 * state fills them (the binder at the trust stratum casts through these
 * shapes, so mirror_ui itself never includes pqsec or oseq headers). */

typedef struct {
    uint32_t peers_up;    /* peers currently reachable                   */
    uint32_t peers_total; /* peers in the baked topology (0 = unknown)   */
} mirror_net_view_t;

typedef struct {
    uint32_t verified; /* 1 = this CID verified against its digest    */
    uint32_t pinned;   /* 1 = pinned on this node                     */
} mirror_cid_view_t;

typedef struct {
    uint32_t decapsulated;  /* 1 = ML-KEM decapsulation succeeded          */
    uint32_t authenticated; /* 1 = ML-DSA peer authentication succeeded    */
} mirror_pq_view_t;

typedef struct {
    uint32_t local_tick;     /* this node's phase ordinal                   */
    uint32_t peer_max_tick;  /* highest peer ordinal observed (logical clock) */
    uint32_t skew_tolerance; /* accepted skew; 0 = no skew permitted        */
} mirror_clock_view_t;

/* ---- the projected valence -------------------------------------------------
 *
 * Every register projects to a VISUAL VALENCE the renderer must honour —
 * this is the "uncertainty handler": a NEITHER or BOTH state renders as
 * ambiguity (dimming / structural shading), never as a forced binary. */
typedef enum {
    MIRROR_VALENCE_CERTAIN = 0, /* TRUE, sealed+verified, S+ at depth     */
    MIRROR_VALENCE_ABSENT = 1,  /* FALSE, open, S-                        */
    MIRROR_VALENCE_NEITHER = 2, /* unknown, unverified, unbound, S0       */
    MIRROR_VALENCE_BOTH = 3,    /* contradictory evidence, full ring       */
    MIRROR_VALENCE_COUNT
} mirror_valence_t;

/* One projected register, as the renderer sees it. */
typedef struct {
    mirror_kind_t kind;
    char name[MIRROR_NAME_LEN];
    mirror_valence_t valence;
    uint32_t value; /* the register's exact integer value     */
    uint32_t depth; /* tri-space depth (0 for non-tris)       */
} mirror_proj_t;

/* ---- the mirror ----------------------------------------------------------- */

typedef struct {
    mirror_reg_t regs[MIRROR_MAX_REGS];
    uint32_t reg_count;

    /* lockfree change-notify ring: producers push (head), the projector
     * drains (tail). Full ring = backpressure: push refused, full flag set. */
    uint32_t ring_head;
    uint32_t ring_tail;
    uint32_t ring[MIRROR_RING_CAP];
    bool ring_full;

    uint64_t last_tick; /* the tick the frame was last projected at    */
} mirror_ui_t;

/* The deterministic frame: a projection of (registers, tick). Two projections
 * with the same registers and the same tick are byte-identical. */
typedef struct {
    uint64_t tick;
    mirror_proj_t proj[MIRROR_MAX_REGS];
    uint32_t proj_count;
    bool ring_full;  /* backpressure surfaced to the renderer  */
    uint32_t digest; /* fold over the whole frame, exact ints  */
} mirror_frame_t;

/* ---- API ------------------------------------------------------------------ */

void mirror_ui_init(mirror_ui_t *m);

/* Bind one kernel state register. `ptr` is borrowed, not copied — it must
 * outlive the mirror and stay valid for the mirror's whole life (kernel
 * static state satisfies this; a stack frame does not). Returns the register
 * index, or -1 if the table is full or the name/kind is invalid. */
int mirror_bind(mirror_ui_t *m, mirror_kind_t kind, const char *name, const void *ptr,
                uint32_t seed);

/* Notify the mirror that register `idx` changed. Bounded: if the ring is
 * full the push is refused and ring_full is set (backpressure, never lossy
 * overwrite). idx out of range is refused. */
bool mirror_notify(mirror_ui_t *m, uint32_t idx);

/* Project the mirror into `out` at tick `tick`. Deterministic: same
 * registers + same tick -> same bytes. Drains the notify ring. */
void mirror_project(const mirror_ui_t *m, uint64_t tick, mirror_frame_t *out);

/* The valence of one projected register — exposed so the renderer and the
 * Chiglet can agree on the SAME mapping (dual-space symmetry). */
mirror_valence_t mirror_valence_of(const mirror_ui_t *m, uint32_t idx);

const char *mirror_valence_name(mirror_valence_t v);
const char *mirror_kind_name(mirror_kind_t k);

#endif /* ZXV_MIRROR_UI_H */