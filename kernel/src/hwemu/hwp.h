/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwp.h — ZHP-1, the ZXV hardware profile ("the hardware ROM").
 *
 * The OS image is one half of a ZXV stack; a hardware profile is the
 * other: a canonical, signable description of a machine as a tree of
 * abstract COMPONENTS (device > package > cluster > component > lane),
 * each with integer CAPABILITY descriptors, the architectural STATE needed
 * to take it over, and an opaque private EXTENSION blob. Engine nodes
 * name the CONTRACT (the versioned golden-vector set, hwe.h) they satisfy,
 * and a presented profile says which KIND of implementation (hwm.h)
 * serves each node. The emulation matrix (hwm.h) uses two profiles:
 *
 *   HOST    what is physically here (probed: libzxv zxv_hw_probe_host).
 *   TARGET  the machine the software is written for. By default the
 *           public VULPINE-class reference machine R1 (hwp_reference_r1).
 *
 * hwp_match pairs every TARGET component with a HOST component that
 * satisfies it. Paired components run natively; unpaired ones are served
 * by emulators through the matrix. A future chip that publishes a profile
 * satisfying R1 runs the same software with nothing emulated. So R1 is a
 * contract hardware can be built to, and it names classes, contracts and
 * minimum capabilities only, never how any chip implements them.
 * hwp_present fills R1 from a host: each node gets the kind that serves
 * it there (HARD when the host has it, SOFT otherwise). Emulation is
 * functional and engine-level; cycle accuracy is not a goal of ZHP-1.
 *
 * WIRE FORMAT (little-endian, see sdk/docs/HW_PROFILE.md for the spec)
 *   header  16 bytes: "ZHP1" | u16 format=1 | u16 flags=0 | u16 nodes |
 *           u16 reserved=0 | u32 total length
 *   node    u16 id | u16 parent (0xFFFF = root) | u32 class | u16 ncaps |
 *           u16 state_len | u16 ext_len | u16 reserved=0 |
 *           ncaps x (u16 key | u64 value) | state bytes | ext bytes
 *   The ext bytes are the implementer's private extension: opaque, never
 *   interpreted by ZXV, but covered by the encoding and the digest.
 *
 * CANONICAL RULES (hwp_decode refuses anything else, so one machine has
 * exactly one encoding and one digest):
 *   P1 node ids strictly increase; a parent's id is lower than its
 *      child's (or 0xFFFF), so the tree has no cycles;
 *   P2 cap keys strictly increase within a node;
 *   P3 reserved fields are zero and the total length is exact.
 *
 * CAP KEYS: bit 15 CRITICAL, bits 14-13 how a requirement compares
 * (MIN have >= need, EXACT, MASK all needed bits present, INFO never
 * compared), bits 12-0 the id. Ids 0x1000-0x1FFF and classes from
 * 0x80000000 are PRIVATE: an implementer's own, opaque to everyone else.
 * A requirement carrying a cap the host does not report is unmet, unless
 * it is INFO. A non-critical cap a reader does not know is kept and
 * ignored; a critical one it does not know cannot be satisfied.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef HWP_H
#define HWP_H

#include <stdbool.h>
#include <stdint.h>

#define HWP_MAX_NODES 64u
#define HWP_MAX_CAPS  16u
#define HWP_MAX_STATE 256u
#define HWP_MAX_EXT   256u
#define HWP_HEADER    16u
#define HWP_ROOT      0xFFFFu
#define HWP_NONE      0xFFFFu

/* ---- capability keys ---- */
#define HWP_KEY_CRITICAL   0x8000u
#define HWP_CMP_MIN        (0u << 13)
#define HWP_CMP_EXACT      (1u << 13)
#define HWP_CMP_MASK       (2u << 13)
#define HWP_CMP_INFO       (3u << 13)
#define HWP_KEY_CMP(k)     ((uint16_t) ((k) & 0x6000u))
#define HWP_KEY_ID(k)      ((uint16_t) ((k) & 0x1FFFu))
#define HWP_KEY_PRIVATE(k) (HWP_KEY_ID(k) >= 0x1000u)

#define HWP_CAP_WORD_BITS      (HWP_CMP_MIN | 1u) /* native integer width       */
#define HWP_CAP_COUNT          (HWP_CMP_MIN | 2u) /* cores, lanes, units        */
#define HWP_CAP_FREQ_KHZ       (HWP_CMP_INFO | 3u)
#define HWP_CAP_BYTES          (HWP_CMP_MIN | 4u) /* memory / cache / storage   */
#define HWP_CAP_LINE_BYTES     (HWP_CMP_INFO | 5u)
#define HWP_CAP_ISA            (HWP_CMP_EXACT | 6u) /* HWP_ISA_*                  */
#define HWP_CAP_INT_WIDTHS     (HWP_CMP_MASK | 7u)  /* HWP_INT_*                  */
#define HWP_CAP_RADIX          (HWP_CMP_EXACT | 8u) /* 2 binary, 3 ternary        */
#define HWP_CAP_MACS_PER_CYCLE (HWP_CMP_MIN | 9u)
#define HWP_CAP_SIMD           (HWP_CMP_MASK | 10u) /* HWP_SIMD_*                 */
#define HWP_CAP_TICK_NS        (HWP_CMP_INFO | 11u) /* deterministic phase tick   */
#define HWP_CAP_REGIONS        (HWP_CMP_MIN | 12u)  /* reconfigurable regions     */
#define HWP_CAP_BANDWIDTH_MBPS (HWP_CMP_MIN | 13u)
#define HWP_CAP_LATENCY_NS     (HWP_CMP_INFO | 14u)
#define HWP_CAP_PQ_ALGS        (HWP_CMP_MASK | 15u)  /* HWP_PQ_*                   */
#define HWP_CAP_HASH_ALGS      (HWP_CMP_MASK | 16u)  /* HWP_HASH_*                 */
#define HWP_CAP_WIRE_BITS      (HWP_CMP_EXACT | 17u) /* frame width, 168 for UBH  */
#define HWP_CAP_PEAK_OPS_MHZ   (HWP_CMP_INFO | 18u)  /* peak int ops per us, for   */
                                                     /* utilisation reports       */
#define HWP_CAP_CONTRACT_ID    (HWP_CMP_EXACT | 19u) /* hwe.h contract (function) id */
#define HWP_CAP_CONTRACT_VER   (HWP_CMP_MIN | 20u)   /* its golden-vector version   */
#define HWP_CAP_KIND           (HWP_CMP_INFO | 21u)  /* hwm_kind_t serving the node */
#define HWP_CAP_CAPACITY_CLASS (HWP_CMP_MIN | 22u)   /* coarse, 0..15               */
#define HWP_CAP_PERF_BUCKET    (HWP_CMP_MIN | 23u)   /* coarse, 0..15               */
#define HWP_CAP_POWER_BUCKET   (HWP_CMP_INFO | 24u)  /* hwm_power_class() scale     */

/* HWP_CAP_KIND values: the same numbers as hwm_kind_t. */
#define HWP_KIND_HARD   0u
#define HWP_KIND_FABRIC 1u
#define HWP_KIND_MICRO  2u
#define HWP_KIND_PEER   3u
#define HWP_KIND_SOFT   4u
#define HWP_KIND_REMOTE 5u

#define HWP_ISA_X86_64  1u
#define HWP_ISA_ARM64   2u
#define HWP_ISA_RISCV64 3u
#define HWP_ISA_WASM32  4u

#define HWP_INT_4    0x01u
#define HWP_INT_8    0x02u
#define HWP_INT_16   0x04u
#define HWP_INT_32   0x08u
#define HWP_INT_64   0x10u
#define HWP_INT_TRIT 0x20u

#define HWP_SIMD_SSE42   0x001u
#define HWP_SIMD_AVX2    0x002u
#define HWP_SIMD_AVX512  0x004u
#define HWP_SIMD_VNNI    0x008u
#define HWP_SIMD_NEON    0x010u
#define HWP_SIMD_DOTPROD 0x020u
#define HWP_SIMD_I8MM    0x040u
#define HWP_SIMD_SVE     0x080u
#define HWP_SIMD_RVV     0x100u

#define HWP_PQ_MLKEM    0x1u
#define HWP_PQ_MLDSA    0x2u
#define HWP_PQ_SLHDSA   0x4u
#define HWP_HASH_SHA256 0x1u
#define HWP_HASH_SHA3   0x2u

/* ---- component classes ---- */
/* Names (hwp_class_name) are dotted families: device, package, cluster,
 * core, mem.*, engine.*, fabric.*, io.*, secure.*, sensor, radio, ... */
typedef enum {
    HWP_CLASS_MACHINE = 1, /* "device": a whole machine; may nest (a machine
                              inside a region, a peer on the mesh) */
    HWP_CLASS_CPU_CLUSTER = 2,
    HWP_CLASS_CPU_CORE = 3,
    HWP_CLASS_CACHE = 4,
    HWP_CLASS_MEMORY = 5,
    HWP_CLASS_INTERCONNECT = 6,
    HWP_CLASS_MATRIX_UNIT = 7, /* integer multiply-accumulate arrays */
    HWP_CLASS_VECTOR_UNIT = 8,
    HWP_CLASS_TERNARY_UNIT = 9, /* trit logic */
    HWP_CLASS_HASH_UNIT = 10,
    HWP_CLASS_CRYPTO_UNIT = 11,
    HWP_CLASS_CODEC_UNIT = 12,
    HWP_CLASS_RECONF_FABRIC = 13,
    HWP_CLASS_STORAGE = 14,
    HWP_CLASS_NETWORK = 15,
    HWP_CLASS_RADIO = 16,
    HWP_CLASS_SENSOR = 17,
    HWP_CLASS_ACTUATOR = 18,
    HWP_CLASS_CLOCK = 19, /* a deterministic phase-tick source */
    HWP_CLASS_POWER = 20,
    HWP_CLASS_SECURE_ELEMENT = 21,
    HWP_CLASS_DISPLAY = 22,
    HWP_CLASS_AUDIO = 23,
    HWP_CLASS_WIRE = 24, /* a harmonic-wire (UBH-168) port */
    HWP_CLASS_GPU = 25,
    HWP_CLASS_PACKAGE = 26,
    HWP_CLASS_LANE = 27,
    HWP_CLASS_LOGIC_UNIT = 28,    /* engine.logic64: 64-lane four-valued logic */
    HWP_CLASS_NTT_UNIT = 29,      /* engine.ntt */
    HWP_CLASS_FRAME_UNIT = 30,    /* engine.frame.ubh168 */
    HWP_CLASS_GCD_UNIT = 31,      /* engine.gcd */
    HWP_CLASS_MEM_CHANNEL = 32,   /* mem.channel */
    HWP_CLASS_SECURE_ISLAND = 33, /* secure.island: attest, seal and
                                     update-verify only; nothing else of it
                                     is described */
    HWP_CLASS_IO = 34,
    HWP_CLASS_PUBLIC_MAX = 34
} hwp_class_t;
#define HWP_CLASS_PRIVATE 0x80000000u /* and above: implementer-defined */

typedef enum {
    HWP_OK = 0,
    HWP_E_ARG = -1,
    HWP_E_FULL = -2,
    HWP_E_FORMAT = -3,    /* not ZHP-1 */
    HWP_E_CANONICAL = -4, /* P1-P3 broken */
    HWP_E_BUFFER = -5,
    HWP_E_DUPLICATE = -6,
    HWP_E_NOT_FOUND = -7
} hwp_status_t;

typedef struct {
    uint16_t key;
    uint64_t val;
} hwp_cap_t;

typedef struct {
    uint16_t id, parent;
    uint32_t cls;
    hwp_cap_t cap[HWP_MAX_CAPS];
    uint16_t ncap;
    uint16_t state_len;
    uint8_t state[HWP_MAX_STATE];
    uint16_t ext_len;
    uint8_t ext[HWP_MAX_EXT]; /* private extension, opaque */
} hwp_node_t;

typedef struct {
    hwp_node_t node[HWP_MAX_NODES];
    uint16_t n;
} hwp_profile_t;

void hwp_init(hwp_profile_t *p);
/* Nodes must be added in increasing id order, parent first (P1). */
int hwp_node_add(hwp_profile_t *p, uint16_t id, uint16_t parent, uint32_t cls, uint16_t *out_index);
int hwp_node_find(const hwp_profile_t *p, uint16_t id, uint16_t *out_index);
/* Set (or replace) a cap; keeps keys sorted (P2). */
int hwp_cap_set(hwp_profile_t *p, uint16_t node_index, uint16_t key, uint64_t val);
bool hwp_cap_get(const hwp_node_t *n, uint16_t key, uint64_t *out);
int hwp_state_set(hwp_profile_t *p, uint16_t node_index, const uint8_t *state, uint16_t len);
int hwp_ext_set(hwp_profile_t *p, uint16_t node_index, const uint8_t *ext, uint16_t len);

uint32_t hwp_encoded_len(const hwp_profile_t *p);
int hwp_encode(const hwp_profile_t *p, uint8_t *out, uint32_t cap, uint32_t *out_len);
int hwp_decode(const uint8_t *buf, uint32_t len, hwp_profile_t *p);
/* SHA-256 of the canonical encoding: the profile's identity. */
void hwp_digest(const hwp_profile_t *p, uint8_t out[32]);

/* ---- matching (TARGET against HOST) ---- */
typedef enum {
    HWP_M_NATIVE = 0,  /* a host component satisfies it */
    HWP_M_EMULATE = 1, /* no host component does: the matrix emulates it */
    HWP_M_GROUP = 2    /* a device or package node: grouping, nothing to run */
} hwp_mkind_t;

typedef struct {
    uint16_t need_id;
    hwp_mkind_t kind;
    uint16_t have_id;   /* HWP_NONE unless NATIVE */
    uint16_t unmet_key; /* EMULATE: the first cap that failed on the
                           closest host component of that class, 0 if
                           the host has no component of that class */
} hwp_pair_t;

typedef struct {
    hwp_pair_t pair[HWP_MAX_NODES];
    uint16_t n;
    uint16_t native, emulated;
} hwp_match_t;

/* Does `have` satisfy `need` (same class, every needed cap met)? On false,
 * *out_unmet (optional) is the first failing key. */
bool hwp_node_satisfies(const hwp_node_t *have, const hwp_node_t *need, uint16_t *out_unmet);
/* Pair every target node in id order with the first unused host node that
 * satisfies it. Deterministic; a host node serves at most one target
 * node. */
int hwp_match(const hwp_profile_t *host, const hwp_profile_t *target, hwp_match_t *out);

/* The public VULPINE-class reference machine R1: the default TARGET. */
const hwp_profile_t *hwp_reference_r1(void);
/* The target as served on this host: a copy of `target` in which every
 * node that runs something carries HWP_CAP_KIND (HARD where a host node
 * satisfies it, SOFT where the matrix emulates it). */
int hwp_present(const hwp_profile_t *host, const hwp_profile_t *target, hwp_profile_t *out);

const char *hwp_class_name(uint32_t cls);

#endif /* HWP_H */
