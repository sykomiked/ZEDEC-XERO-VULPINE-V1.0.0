/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aml_int.h — internals shared by the AML interpreter's files:
 *   aml_core.c    heap, values, namespace, parser and evaluator
 *   aml_region.c  operation regions, fields, the embedded controller
 *   aml_dev.c     device walk, _INI/_REG, typed helpers (battery, thermal...)
 * Nothing outside kernel/src/acpi includes this. */
#ifndef ZXV_AML_INT_H
#define ZXV_AML_INT_H

#include "aml.h"

/* location kinds (aml_loc_t.kind) */
#define AML_LOC_NONE  0
#define AML_LOC_NODE  1
#define AML_LOC_LOCAL 2
#define AML_LOC_ARG   3
#define AML_LOC_OWNED 4
#define AML_LOC_DEBUG 5

/* region spaces */
#define AML_SPACE_MEM   0
#define AML_SPACE_IO    1
#define AML_SPACE_PCI   2
#define AML_SPACE_EC    3
#define AML_SPACE_SMBUS 4
#define AML_SPACE_CMOS  5
#define AML_SPACE_TABLE 0xFE /* internal: DataRegion over a loaded table */

/* field flags */
#define AML_ACC_ANY   0
#define AML_ACC_BYTE  1
#define AML_ACC_WORD  2
#define AML_ACC_DWORD 3
#define AML_ACC_QWORD 4
#define AML_ACC_BUF   5
#define AML_UPD_PRESERVE 0
#define AML_UPD_ONES     1
#define AML_UPD_ZEROS    2

#define AML_FIELD_NORMAL 0
#define AML_FIELD_INDEX  1
#define AML_FIELD_BANK   2

typedef struct aml_node aml_node_t;
typedef int (*aml_native_fn)(aml_ctx_t *c, aml_node_t *m, aml_val_t *args, uint32_t argc,
                             aml_val_t *ret);

typedef struct aml_ref {
    aml_loc_t base;
    aml_val_t owned; /* base.kind == AML_LOC_OWNED */
    uint8_t indexed;
    uint32_t index;
} aml_ref_t;

struct aml_node {
    uint32_t name; /* four ASCII bytes, first byte lowest */
    uint32_t gen;
    aml_node_t *parent, *child, *next;
    aml_node_t *tnext; /* frame temporaries list */
    aml_val_t v;       /* data objects: the value; others: v.type only */
    uint8_t temp;
    union {
        struct {
            const uint8_t *code;
            uint32_t len;
            uint8_t flags; /* argc 0-2, serialized 3, sync 4-7 */
            aml_native_fn native;
        } m;
        struct {
            uint8_t space;
            uint8_t ready; /* base/len evaluated */
            uint8_t pci_ok;
            uint64_t base, len;
            volatile uint8_t *map;
            aml_node_t *dscope; /* scope of the deferred terms */
            uint8_t busy;    /* preparing (catches a region used by its own _ADR) */
            const uint8_t *defer; /* the two TermArgs, evaluated on first use */
            uint32_t defer_len;
            uint32_t seg, bus, dev, fn;
        } r;
        struct {
            uint8_t kind;
            uint8_t flags;
            uint8_t attrib;
            uint8_t acc_len;
            aml_node_t *region; /* region, or the index field for IndexField */
            aml_node_t *data;   /* IndexField data field, BankField bank field */
            uint64_t bank_val;
            uint32_t bitoff, bitlen;
        } f;
        struct {
            aml_loc_t target;
            aml_val_t owned;
            uint32_t bitoff, bitlen;
        } bf;
        struct {
            uint8_t sync;
            uint32_t depth;
        } mx;
        struct {
            uint32_t count;
        } ev;
        struct {
            uint32_t id, pblk, plen;
        } cpu;
        struct {
            uint32_t level, order;
        } pr;
        struct {
            aml_node_t *target;
        } alias;
        struct {
            uint32_t index;
        } ddb;
    } x;
};

/* ---- heap (aml_core.c) ---- */
void *aml_alloc(aml_ctx_t *c, uint32_t n);
void aml_free(aml_ctx_t *c, void *p);
void aml_bzero(void *p, uint32_t n);
void aml_bcopy(void *d, const void *s, uint32_t n);

/* ---- values ---- */
int aml_val_copy(aml_ctx_t *c, aml_val_t *dst, const aml_val_t *src);
int aml_mkbuf(aml_ctx_t *c, aml_val_t *v, uint32_t len);
int aml_mkstr(aml_ctx_t *c, aml_val_t *v, uint32_t len);
int aml_to_int(aml_ctx_t *c, const aml_val_t *v, uint64_t *out);
int aml_to_buf(aml_ctx_t *c, const aml_val_t *v, aml_val_t *out);
uint64_t aml_ones(const aml_ctx_t *c);

/* ---- namespace ---- */
aml_node_t *aml_ns_child(aml_node_t *scope, uint32_t name);
aml_node_t *aml_ns_resolve(aml_ctx_t *c, aml_node_t *n); /* follow aliases */
int aml_call(aml_ctx_t *c, aml_node_t *m, aml_val_t *args, uint32_t argc, aml_val_t *ret);
int aml_eval_node(aml_ctx_t *c, aml_node_t *n, const aml_val_t *args, uint32_t argc,
                  aml_val_t *ret);
void aml_logf(aml_ctx_t *c, const char *a, const char *b, uint64_t v, int hex);
void aml_budget_reset(aml_ctx_t *c);
int aml_delay(aml_ctx_t *c, uint64_t us);

/* ---- regions and fields (aml_region.c) ---- */
int aml_region_eval_deferred(aml_ctx_t *c, aml_node_t *r);
int aml_region_prepare(aml_ctx_t *c, aml_node_t *r);
int aml_field_read(aml_ctx_t *c, aml_node_t *f, aml_val_t *out);
int aml_field_write(aml_ctx_t *c, aml_node_t *f, const aml_val_t *v);
int aml_region_access(aml_ctx_t *c, aml_node_t *r, uint64_t off, uint32_t width_bytes, int write,
                      uint64_t *val);
uint32_t aml_rd_le(const uint8_t *p, uint32_t n);

#endif
