/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aml.h — an integer-only ACPI Machine Language interpreter for bare-metal ZXV.
 *
 * What it is for: the firmware describes power management as AML byte code in
 * the DSDT and SSDTs. Without running that code there is no battery, no lid,
 * no thermal zone, no sleep state and no clean power-off. This interpreter
 * loads the tables into a namespace and evaluates methods on demand.
 *
 * Shape
 *   - one aml_ctx_t holds everything: a caller-supplied arena (no malloc, no
 *     libc), the namespace, the call stack and the budgets;
 *   - hardware is reached only through aml_plat_t (memory map, port I/O, PCI
 *     configuration space, delay, log), so the same source runs in the kernel
 *     and on the host against fake devices (kernel/src/acpi/test);
 *   - AML is untrusted input: every read is bounds-checked, every loop and
 *     every recursion has a budget, and a bad table or a hostile method gives
 *     an error code, never a crash or a hang.
 *
 * Integer only. Values follow ACPI's value semantics: a Store copies, so every
 * value has exactly one owner and is freed by it.
 *
 * Verdicts: _STA maps onto the four-valued verdict of hwdrv.h (HW_T present
 * and working, HW_F present but failed, HW_N absent, HW_B the contradictory
 * "absent but functioning" combination the spec reserves for containers). */
#ifndef ZXV_AML_H
#define ZXV_AML_H

#include <stdint.h>

#include "hwdrv/hwdrv.h"

/* ---- status codes -------------------------------------------------------- */
#define AML_OK      0
#define AML_EPARSE  (-1)  /* malformed byte code (bad length, bad name, bad opcode) */
#define AML_ENOENT  (-2)  /* name not found */
#define AML_ETYPE   (-3)  /* operand of the wrong type */
#define AML_ENOMEM  (-4)  /* arena exhausted or object over its size limit */
#define AML_ELIMIT  (-5)  /* op, loop, sleep or depth budget spent */
#define AML_EBOUNDS (-6)  /* index or field outside its object or region */
#define AML_EDIV0   (-7)  /* Divide / Mod by zero */
#define AML_EFATAL  (-8)  /* Fatal opcode */
#define AML_EUNSUP  (-9)  /* address space or opcode this build does not do */
#define AML_EEXIST  (-10) /* name already defined with another type */
#define AML_EIO     (-11) /* region access failed (no handler, EC timeout) */
#define AML_EARG    (-12) /* bad API argument */
#define AML_EMUTEX  (-13) /* Release of a mutex that is not held */

/* ---- object types (the numbers ObjectType returns) ---------------------- */
#define AML_T_UNINIT   0
#define AML_T_INT      1
#define AML_T_STR      2
#define AML_T_BUF      3
#define AML_T_PKG      4
#define AML_T_FIELD    5
#define AML_T_DEVICE   6
#define AML_T_EVENT    7
#define AML_T_METHOD   8
#define AML_T_MUTEX    9
#define AML_T_REGION   10
#define AML_T_POWER    11
#define AML_T_PROC     12
#define AML_T_THERMAL  13
#define AML_T_BUFFIELD 14
#define AML_T_DDB      15
#define AML_T_DEBUG    16
/* internal, never visible as an ObjectType number */
#define AML_T_REF   20 /* object reference (RefOf, Index, a name in a package) */
#define AML_T_SCOPE 21 /* \_GPE, \_PR_, \_TZ_ and other pure scopes */
#define AML_T_ALIAS 22

struct aml_node;
struct aml_ref;
struct aml_ctx;

/* One value. str: len bytes plus a NUL; buf: len bytes; pkg: len elements. */
typedef struct aml_val {
    uint8_t type;
    uint8_t pad[3];
    uint32_t len;
    union {
        uint64_t i;
        uint8_t *bytes;
        struct aml_val *elems;
        struct aml_ref *ref;
    } u;
} aml_val_t;

/* ---- platform -------------------------------------------------------------- */
typedef struct aml_plat {
    void *ctx;
    /* Map physical memory (SystemMemory regions, tables); 0 = cannot. */
    volatile void *(*mmio_map)(void *ctx, uint64_t phys, uint64_t size);
    /* Port I/O, width 8/16/32. */
    uint32_t (*io_read)(void *ctx, uint32_t port, uint32_t width);
    void (*io_write)(void *ctx, uint32_t port, uint32_t width, uint32_t val);
    /* PCI configuration space; off is a byte offset, width 8/16/32. */
    uint32_t (*pci_read)(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn,
                         uint32_t off, uint32_t width);
    void (*pci_write)(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn,
                      uint32_t off, uint32_t width, uint32_t val);
    void (*delay_us)(void *ctx, uint32_t us);
    /* Optional: monotonic time in 100 ns units (Timer opcode). */
    uint64_t (*timer_100ns)(void *ctx);
    /* Optional: acquire (1) / release (0) the FACS global lock; 1 = held. */
    int (*global_lock)(void *ctx, int acquire);
    /* One console line, no trailing newline. May be 0. */
    void (*log)(const char *line);
} aml_plat_t;

/* ---- budgets ---------------------------------------------------------------- */
#define AML_MAX_CALL_DEPTH 24      /* nested method invocations */
#define AML_MAX_NEST       192     /* C recursion of the evaluator, all frames */
#define AML_MAX_LOOP       0x10000 /* iterations of one While */
#define AML_MAX_OPS        4000000 /* opcodes per top-level evaluation */
#define AML_MAX_SLEEP_US   3000000 /* Sleep + Stall per top-level evaluation */
#define AML_MAX_OBJ        0x40000 /* bytes in one string or buffer */
#define AML_MAX_PKG        4096    /* elements in one package */
#define AML_MAX_LOAD_SKIPS 64      /* unknown opcodes stepped over per table */
#define AML_MAX_TABLES     32
#define AML_MAX_NOTIFY     32
#define AML_MAX_EXTERNAL   64

/* ---- the context -------------------------------------------------------- */
typedef struct aml_loc {
    uint8_t kind; /* AML_LOC_* (aml_core.c) */
    uint8_t slot;
    uint16_t depth;
    uint32_t serial;
    struct aml_node *node;
} aml_loc_t;

typedef struct aml_frame {
    struct aml_node *method;
    struct aml_node *scope;
    struct aml_node *temps; /* nodes this invocation created (freed at exit) */
    uint32_t serial;
    aml_val_t args[7];
    aml_val_t locals[8];
} aml_frame_t;

typedef struct aml_notify {
    struct aml_node *node;
    uint32_t value;
} aml_notify_t;

typedef struct aml_external {
    struct aml_node *scope;
    const uint8_t *name; /* the NameString bytes in the table */
    uint8_t argc;
} aml_external_t;

typedef struct aml_ec {
    uint32_t data_port, cmd_port; /* 0 = no embedded controller */
    uint32_t gpe;
    uint32_t timeouts;
    struct aml_node *dev;
} aml_ec_t;

typedef struct aml_stats {
    uint32_t objects, methods, devices, regions, fields;
    uint32_t tables, errors;
    uint32_t heap_used, heap_peak, heap_size;
} aml_stats_t;

typedef struct aml_ctx {
    const aml_plat_t *plat;
    /* heap */
    uint8_t *arena;
    uint32_t arena_size, arena_top;
    void *freelist[40];
    uint32_t heap_used, heap_peak;
    /* namespace */
    struct aml_node *root;
    struct aml_node *node_free; /* recycled node slots (never returned to the heap) */
    uint32_t node_gen;
    uint8_t int64; /* 1 = 64-bit integers (DSDT revision >= 2) */
    uint8_t dsdt_rev;
    uint8_t loading;     /* 1 while a table's top level runs */
    uint32_t load_skips; /* unknown opcodes stepped over in this table */
    int load_err;        /* first error of a term skipped in this table */
    uint8_t pass1;
    /* execution */
    aml_frame_t frames[AML_MAX_CALL_DEPTH];
    uint32_t depth;    /* frames in use */
    uint32_t nest;     /* evaluator recursion */
    uint32_t serial;   /* frame serial counter */
    uint32_t ops;      /* ops spent in this top-level evaluation */
    uint32_t op_limit; /* 0 = AML_MAX_OPS; a caller may set a smaller budget */
    uint32_t sleep_us;
    uint32_t top; /* top-level evaluation nesting (budgets reset at 0) */
    uint64_t fake_time;
    uint32_t gl_count; /* global lock recursion */
    aml_val_t ret;     /* Return value in flight */
    /* bookkeeping */
    aml_notify_t notify[AML_MAX_NOTIFY];
    uint32_t notify_head, notify_count, notify_dropped;
    aml_external_t ext[AML_MAX_EXTERNAL];
    uint32_t n_ext;
    struct {
        const uint8_t *aml;
        uint32_t len;
        char sig[4];
        char oem_tid[8];
        char oem_id[6];
    } tables[AML_MAX_TABLES];
    uint32_t n_tables;
    aml_stats_t stats;
    aml_ec_t ec;
    int last_error;
    const char *last_error_where;
    uint32_t debug_lines; /* Store(x, Debug) lines logged */
} aml_ctx_t;

/* ---- lifecycle ----------------------------------------------------------- */
/* Set up an empty namespace (\_SB_, \_GPE, \_PR_, \_TZ_, \_SI_, \_GL_, \_OSI,
 * \_OS_, \_REV) in arena (at least 64 KB). plat may have 0 hooks; regions
 * that need a missing hook fail with AML_EIO. */
int aml_init(aml_ctx_t *c, const aml_plat_t *plat, void *arena, uint32_t arena_size);

/* Load one definition block (a whole DSDT/SSDT, header included). The bytes
 * must stay valid for the life of the context (method bodies point into
 * them). Returns AML_OK or the first error; on error the objects already
 * created stay (as ACPICA does), so one bad SSDT does not lose the DSDT. */
int aml_load_table(aml_ctx_t *c, const void *table, uint32_t len);

/* ---- namespace ------------------------------------------------------------ */
/* Look up an ASCII path ("\_SB.PCI0.LPCB", "_STA", "^FOO") from scope (0 =
 * root). Like AcpiEvaluateObject, a relative name is NOT searched for in the
 * parent scopes: "_STA" under a device means that device's own _STA. */
struct aml_node *aml_lookup(aml_ctx_t *c, struct aml_node *scope, const char *path);
/* Object type of a node (AML_T_*). */
uint32_t aml_node_type(const struct aml_node *n);
/* The absolute path of n into out ("\_SB_.PCI0"); returns its length. */
uint32_t aml_node_path(const struct aml_node *n, char *out, uint32_t max);
/* Four-character name, NUL terminated into out[5]. */
void aml_node_name(const struct aml_node *n, char out[5]);
struct aml_node *aml_node_parent(const struct aml_node *n);

/* Depth-first walk under start (0 = root). cb returns 0 to continue, 1 to skip
 * the children of this node, 2 to stop. Returns nodes visited. */
typedef int (*aml_walk_fn)(aml_ctx_t *c, struct aml_node *n, uint32_t depth, void *user);
uint32_t aml_walk(aml_ctx_t *c, struct aml_node *start, aml_walk_fn cb, void *user);

/* 1 when device n has _HID or _CID equal to id ("PNP0C0A", "ACPI0007"). EISA
 * integers are decoded to their 7-character form first. */
int aml_dev_match_id(aml_ctx_t *c, struct aml_node *n, const char *id);
/* Up to max devices with that id, in namespace order; returns the count. */
uint32_t aml_find_devices(aml_ctx_t *c, const char *id, struct aml_node **out, uint32_t max);
/* The device's _HID as text into out[16] ("PNP0C0A", "ACPI0007", an EISA
 * integer decoded); 1 = it has one. Longer string ids are cut to 15. */
int aml_dev_hid(aml_ctx_t *c, struct aml_node *n, char out[16]);
/* Decode a compressed EISA id into out[8] ("PNP0C0A"). */
void aml_eisa_to_str(uint32_t eisa, char out[8]);

/* ---- evaluation ---------------------------------------------------------- */
/* Evaluate path relative to scope with argc arguments. A method is called; a
 * data object is copied; a field is read. *result (may be 0) receives a value
 * the caller owns and must aml_val_free. */
int aml_eval(aml_ctx_t *c, struct aml_node *scope, const char *path, const aml_val_t *args,
             uint32_t argc, aml_val_t *result);
/* Same, for an integer result (strings and buffers are converted). */
int aml_eval_int(aml_ctx_t *c, struct aml_node *scope, const char *path, uint64_t *out);
/* _STA of a device: 0x0F when there is no _STA (present by default). */
uint64_t aml_sta(aml_ctx_t *c, struct aml_node *dev);
hw_verdict_t aml_sta_verdict(uint64_t sta);

void aml_val_free(aml_ctx_t *c, aml_val_t *v);
aml_val_t aml_int(uint64_t v);
/* A string or buffer value built from bytes (0 on arena exhaustion: type 0). */
aml_val_t aml_str(aml_ctx_t *c, const char *s);
aml_val_t aml_buf(aml_ctx_t *c, const void *b, uint32_t len);
/* Package element i, or 0 when v is not a package or i is out of range. */
const aml_val_t *aml_pkg_elem(const aml_val_t *v, uint32_t i);
/* Integer value of v (int, or the first bytes of a buffer/hex string); 0 = ok. */
int aml_val_to_int(aml_ctx_t *c, const aml_val_t *v, uint64_t *out);

/* Pop one queued Notify(object, value); 1 = an entry was written. */
int aml_notify_pop(aml_ctx_t *c, aml_notify_t *out);

aml_stats_t aml_stats(aml_ctx_t *c);
const char *aml_strerror(int err);

/* ---- embedded controller ---------------------------------------------------
 * Ports from the ECDT or the PNP0C09 device's _CRS. aml_ec_init records them
 * and runs _REG(3, 1) so firmware methods start using the EC. */
int aml_ec_init(aml_ctx_t *c, uint32_t data_port, uint32_t cmd_port, struct aml_node *dev);
/* Find a PNP0C09 device and take its ports from _CRS; 1 = found. */
int aml_ec_probe(aml_ctx_t *c);
int aml_ec_read(aml_ctx_t *c, uint8_t addr, uint8_t *val);
int aml_ec_write(aml_ctx_t *c, uint8_t addr, uint8_t val);
/* If the EC raised an SCI event, query it and run _Qxx; returns xx or -1. */
int aml_ec_query(aml_ctx_t *c);

/* ---- namespace initialisation ------------------------------------------- */
/* _REG(space, 1) for every device holding a region of a space we serve, then
 * \_SB._INI and _INI of every present device (children of absent devices are
 * skipped unless the parent reports "functioning"). Returns _INI count. */
uint32_t aml_init_devices(aml_ctx_t *c);

/* ---- typed helpers over standard methods --------------------------------- */
/* \_Sx_ package: SLP_TYPa / SLP_TYPb. AML_ENOENT when the state is absent. */
int aml_sleep_state(aml_ctx_t *c, uint32_t state, uint8_t *typa, uint8_t *typb);

typedef struct aml_battery {
    uint32_t present;    /* _STA bit 4 */
    uint32_t state;      /* _BST[0]: bit0 discharging, bit1 charging, bit2 critical */
    uint32_t rate;       /* _BST[1] (mW or mA; 0xFFFFFFFF unknown) */
    uint32_t remaining;  /* _BST[2] */
    uint32_t voltage;    /* _BST[3] mV */
    uint32_t power_unit; /* 0 = mWh, 1 = mAh */
    uint32_t design_capacity, full_capacity, design_voltage;
    uint32_t cycle_count; /* _BIX only; 0xFFFFFFFF unknown */
    uint32_t percent;     /* remaining * 100 / full, 0..100; 0xFFFFFFFF unknown */
    uint32_t has_bix;
} aml_battery_t;
/* Read a PNP0C0A battery (_STA, then _BIX or _BIF, then _BST). */
int aml_battery_read(aml_ctx_t *c, struct aml_node *bat, aml_battery_t *out);

typedef struct aml_thermal {
    /* tenths of a kelvin; 0 = method absent */
    uint32_t tmp, psv, crt, hot;
} aml_thermal_t;
int aml_thermal_read(aml_ctx_t *c, struct aml_node *tz, aml_thermal_t *out);
/* tenths of a kelvin -> tenths of a degree Celsius */
int32_t aml_dk_to_dc(uint32_t dk);

/* _LID: 1 open, 0 closed. */
int aml_lid_read(aml_ctx_t *c, struct aml_node *lid, uint32_t *open);

typedef struct aml_pstate {
    uint32_t mhz, mw, latency, bm_latency, control, status;
} aml_pstate_t;
/* _PSS: fills up to max entries, returns the count (or an error < 0). */
int aml_cpu_pss(aml_ctx_t *c, struct aml_node *cpu, aml_pstate_t *out, uint32_t max);
/* _CPC: number of entries in the package (CPPC), or an error < 0. */
int aml_cpu_cpc(aml_ctx_t *c, struct aml_node *cpu, uint32_t *revision);
/* _PPC: highest allowed P-state index. */
int aml_cpu_ppc(aml_ctx_t *c, struct aml_node *cpu, uint32_t *ppc);
/* _PCT: 1 when both control and status register descriptors are present. */
int aml_cpu_pct(aml_ctx_t *c, struct aml_node *cpu, uint64_t *ctl_addr, uint64_t *sts_addr);

/* _BCL: levels (the first two entries are the AC and battery defaults and are
 * returned separately). Returns the number of levels written. */
int aml_bcl(aml_ctx_t *c, struct aml_node *out_dev, uint32_t *levels, uint32_t max, uint32_t *ac,
            uint32_t *dc);
int aml_bcm(aml_ctx_t *c, struct aml_node *out_dev, uint32_t level);
int aml_bqc(aml_ctx_t *c, struct aml_node *out_dev, uint32_t *level);

#endif
