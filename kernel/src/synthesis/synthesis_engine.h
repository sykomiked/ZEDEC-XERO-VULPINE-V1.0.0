/* synthesis_engine.h — ZEDEC Synthesis Engine
 *
 * The core OS-level service for mathematical software fabrication.
 * This is the engine that built the kernel, now integrated INTO the kernel
 * as a first-class OS service. Users can fabricate software and hardware
 * mathematically using deterministic models, M5 axiomatic coordinates,
 * second quantization field operators, and the nonlinear build method.
 *
 * Key capabilities:
 *   1. Mathematical code fabrication — generate C/HDL/Chisel from specs
 *   2. Hardware-as-code synthesis — RTL device → FPGA/ASIC artifacts
 *   3. Nonlinear compilation — deterministic models, unique mathematics
 *   4. Second quantization gates — creation/annihilation field operators
 *   5. M5 coverage verification — r × ℓ ≥ 1.8 for all synthesized output
 *   6. ISF surplus optimization — maximize interaction surplus f(u) = ln(1+(N-1)u)
 *   7. EDP risk assessment — evaluate synthesized code for collapse risk
 *   8. Anti-collapse verification — prevent axiomatic violations in output
 *   9. Multi-target output — C, SystemVerilog, VHDL, Chisel, Python
 *  10. Template-driven generation — axiom-verified code templates
 *
 * The synthesis engine is itself a hardware-as-code device:
 *   - Registers: spec input, output buffer, coverage, surplus, risk
 *   - DMA: streaming spec in, streaming fabricated code out
 *   - IRQ: synthesis_complete, coverage_violation, surplus_optimal
 *
 * Architecture:
 *
 *   User Input (spec)
 *        ↓
 *   [Parser] → AST → [Axiom Verifier] → [M5 Coordinate Mapper]
 *        ↓                                    ↓
 *   [Nonlinear Compiler] ← [ISF Surplus Optimizer]
 *        ↓                        ↓
 *   [Second Quantization Gates] → [Code Generator]
 *        ↓                              ↓
 *   [EDP Risk Check] ← [Coverage Verifier] → [HDL Synthesizer]
 *        ↓                                    ↓
 *   [Anti-Collapse Check] → Output (C/HDL/Chisel/Python)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */
#ifndef SYNTHESIS_ENGINE_H
#define SYNTHESIS_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"

/* ===== Constants ===== */
#define SYNTH_MAX_SPEC_LEN        8192
#define SYNTH_MAX_OUTPUT_LEN      65536
#define SYNTH_MAX_TEMPLATES       256
#define SYNTH_MAX_AST_NODES       1024
#define SYNTH_MAX_GATES           512
#define SYNTH_MAX_NAME_LEN        128
#define SYNTH_DMA_BUFFER_SIZE     16384
#define SYNTH_NUM_REGISTERS       16

/* ===== Synthesis target languages ===== */
typedef enum {
    SYNTH_TARGET_C = 0,          /* C source code */
    SYNTH_TARGET_HEADER,         /* C header file */
    SYNTH_TARGET_SYSTEMVERILOG,  /* SystemVerilog HDL */
    SYNTH_TARGET_VHDL,           /* VHDL HDL */
    SYNTH_TARGET_CHISEL,         /* Chisel HDL (Scala) */
    SYNTH_TARGET_PYTHON,         /* Python script */
    SYNTH_TARGET_MAKEFILE,       /* Makefile */
    SYNTH_TARGET_LINKER_SCRIPT,  /* Linker script */
    SYNTH_TARGET_BOOT_ASM,       /* Boot assembly */
} synth_target_t;

/* ===== Synthesis modes ===== */
typedef enum {
    SYNTH_MODE_KERNEL_MODULE = 0,   /* Generate a kernel module (.h + .c) */
    SYNTH_MODE_HW_DEVICE,           /* Generate a hardware-as-code device */
    SYNTH_MODE_DRIVER,              /* Generate a device driver */
    SYNTH_MODE_PROTOCOL,            /* Generate a network protocol */
    SYNTH_MODE_FINANCIAL,           /* Generate a financial instrument */
    SYNTH_MODE_CRYPTO,              /* Generate a cryptographic primitive */
    SYNTH_MODE_TEST,                /* Generate a test suite */
    SYNTH_MODE_APPLICATION,         /* Generate a userspace application */
    SYNTH_MODE_HDL_ARTIFACT,        /* Generate pure HDL for FPGA/ASIC */
    SYNTH_MODE_CUSTOM,              /* Custom template-driven */
} synth_mode_t;

/* ===== AST node types for spec parsing ===== */
typedef enum {
    AST_ROOT = 0,
    AST_MODULE_DECL,
    AST_FUNCTION_DECL,
    AST_STRUCT_DECL,
    AST_TYPEDEF,
    AST_REGISTER,
    AST_DMA_CHANNEL,
    AST_IRQ_LINE,
    AST_GATE_OP,        /* Second quantization gate */
    AST_M5_COORD,       /* M5 coordinate binding */
    AST_COVERAGE_CHECK, /* r × ℓ ≥ 1.8 verification */
    AST_SURPLUS_EXPR,   /* ISF surplus expression */
    AST_RISK_EXPR,      /* EDP risk expression */
    AST_TEMPLATE_REF,   /* Reference to a code template */
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_BLOCK,
} ast_node_type_t;

/* ===== AST node ===== */
typedef struct ast_node {
    ast_node_type_t type;
    char name[SYNTH_MAX_NAME_LEN];
    char value[256];
    struct ast_node *children[16];
    uint32_t num_children;
    struct ast_node *parent;
    /* M5 coordinates for this node */
    m5_coords_t m5;
} ast_node_t;

/* ===== Second quantization gate (field operator) ===== */
typedef enum {
    GATE_CREATE = 0,     /* Creation operator (adds a module/function) */
    GATE_ANNIHILATE,     /* Annihilation operator (removes dead code) */
    GATE_NUMBER,         /* Number operator (counts active elements) */
    GATE_DISPLACEMENT,   /* Displacement operator (shifts state) */
    GATE_SQUEEZE,        /* Squeeze operator (optimizes/compresses) */
    GATE_ROTATION,       /* Rotation operator (transforms coordinates) */
    GATE_ENTANGLE,       /* Entanglement operator (links modules) */
    GATE_MEASURE,        /* Measurement operator (collapses to output) */
} gate_op_t;

typedef struct {
    gate_op_t op;
    uint32_t source_node;
    uint32_t target_node;
    double amplitude;     /* Field amplitude */
    double phase;         /* Field phase (radians) */
    m5_coords_t m5;       /* M5 coordinates of this gate */
    bool applied;
} synth_gate_t;

/* ===== Code template ===== */
typedef struct {
    char name[SYNTH_MAX_NAME_LEN];
    synth_mode_t mode;
    synth_target_t target;
    char template_text[SYNTH_MAX_OUTPUT_LEN];
    uint32_t template_len;
    /* Variables that can be substituted */
    char var_names[32][64];
    uint32_t num_vars;
    /* M5 coverage requirement */
    double min_coverage_r;
    double min_coverage_l;
} synth_template_t;

/* ===== Synthesis register set (hardware-as-code) ===== */
typedef enum {
    REG_SPEC_INPUT = 0,      /* Write: spec text input */
    REG_SPEC_LENGTH,         /* Write: spec text length */
    REG_TARGET_LANG,         /* Write: target language */
    REG_MODE,                /* Write: synthesis mode */
    REG_OUTPUT_ADDR,         /* Read: output buffer address */
    REG_OUTPUT_LEN,          /* Read: output length */
    REG_STATUS,              /* Read: engine status */
    REG_COVERAGE_R,          /* Read: coverage r value */
    REG_COVERAGE_L,          /* Read: coverage l value */
    REG_SURPLUS,             /* Read: ISF surplus value */
    REG_RISK,                /* Read: EDP risk value */
    REG_GATE_COUNT,          /* Read: number of gates applied */
    REG_NODE_COUNT,          /* Read: AST node count */
    REG_TEMPLATE_ID,         /* Write: template to use */
    REG_OPTIMIZE,            /* Write: optimization level 0-3 */
    REG_CONTROL,             /* Write: start/abort/reset */
} synth_reg_t;

/* ===== Engine status bits ===== */
#define SYNTH_STATUS_IDLE       0x00
#define SYNTH_STATUS_PARSING    0x01
#define SYNTH_STATUS_VERIFYING  0x02
#define SYNTH_STATUS_COMPILING  0x04
#define SYNTH_STATUS_GENERATING 0x08
#define SYNTH_STATUS_DONE       0x10
#define SYNTH_STATUS_ERROR      0x20
#define SYNTH_STATUS_COVERAGE_FAIL 0x40
#define SYNTH_STATUS_RISK_HIGH  0x80

/* ===== Control bits ===== */
#define SYNTH_CTRL_START    0x01
#define SYNTH_CTRL_ABORT    0x02
#define SYNTH_CTRL_RESET    0x04
#define SYNTH_CTRL_VERIFY_ONLY 0x08

/* ===== Synthesis result ===== */
typedef struct {
    bool success;
    synth_target_t target;
    char output[SYNTH_MAX_OUTPUT_LEN];
    uint32_t output_len;
    double coverage_r;
    double coverage_l;
    double surplus;
    double risk;
    uint32_t gates_applied;
    uint32_t nodes_generated;
    char error_msg[512];
} synth_result_t;

/* ===== Synthesis engine (hardware-as-code device) ===== */
typedef struct {
    /* Device identity */
    uint32_t device_id;
    char name[SYNTH_MAX_NAME_LEN];

    /* Register file */
    uint64_t registers[SYNTH_NUM_REGISTERS];

    /* DMA buffers */
    uint8_t tx_dma[SYNTH_DMA_BUFFER_SIZE];   /* Spec input stream */
    uint8_t rx_dma[SYNTH_DMA_BUFFER_SIZE];   /* Output stream */
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ lines */
    bool irq_synthesis_complete;
    bool irq_coverage_violation;
    bool irq_surplus_optimal;
    bool irq_risk_threshold;
    bool irq_error;

    /* Internal state */
    char spec_input[SYNTH_MAX_SPEC_LEN];
    uint32_t spec_len;
    synth_target_t target;
    synth_mode_t mode;
    uint8_t optimize_level;
    uint8_t status;

    /* AST */
    ast_node_t ast_nodes[SYNTH_MAX_AST_NODES];
    uint32_t num_ast_nodes;
    ast_node_t *root;

    /* Gates (second quantization operations) */
    synth_gate_t gates[SYNTH_MAX_GATES];
    uint32_t num_gates;

    /* Templates */
    synth_template_t templates[SYNTH_MAX_TEMPLATES];
    uint32_t num_templates;
    uint32_t active_template;

    /* Result */
    synth_result_t result;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;

    /* Statistics */
    uint64_t total_syntheses;
    uint64_t successful_syntheses;
    uint64_t failed_syntheses;
    uint64_t total_lines_generated;
    uint64_t total_gates_applied;
} synthesis_engine_t;

/* ===== API ===== */

/* Engine lifecycle */
void synth_engine_init(synthesis_engine_t *engine, uint32_t device_id, const char *name);
int synth_engine_reset(synthesis_engine_t *engine);

/* Register access (hardware-as-code) */
uint64_t synth_reg_read(synthesis_engine_t *engine, synth_reg_t reg);
void synth_reg_write(synthesis_engine_t *engine, synth_reg_t reg, uint64_t value);

/* DMA operations */
int synth_dma_write_spec(synthesis_engine_t *engine, const char *spec, uint32_t len);
int synth_dma_read_output(synthesis_engine_t *engine, char *output, uint32_t max_len);

/* Main synthesis operation */
int synth_engine_execute(synthesis_engine_t *engine, synth_result_t *result);

/* Spec parsing */
int synth_parse_spec(synthesis_engine_t *engine, const char *spec, uint32_t len);
ast_node_t *synth_ast_add_node(synthesis_engine_t *engine, ast_node_type_t type, const char *name);

/* Axiom verification */
bool synth_verify_axioms(synthesis_engine_t *engine);
bool synth_check_coverage(synthesis_engine_t *engine);

/* Nonlinear compilation */
int synth_nonlinear_compile(synthesis_engine_t *engine);

/* Second quantization gates */
int synth_gate_create(synthesis_engine_t *engine, uint32_t source, uint32_t target, double amplitude);
int synth_gate_annihilate(synthesis_engine_t *engine, uint32_t node);
int synth_gate_entangle(synthesis_engine_t *engine, uint32_t node1, uint32_t node2);
int synth_gate_measure(synthesis_engine_t *engine);
int synth_apply_gates(synthesis_engine_t *engine);

/* ISF surplus optimization */
double synth_optimize_surplus(synthesis_engine_t *engine);
double synth_compute_surplus(synthesis_engine_t *engine);

/* EDP risk assessment */
double synth_assess_risk(synthesis_engine_t *engine);

/* Code generation */
int synth_generate_code(synthesis_engine_t *engine, synth_target_t target, synth_result_t *result);
int synth_generate_module(synthesis_engine_t *engine, const char *module_name, synth_result_t *result);
int synth_generate_hw_device(synthesis_engine_t *engine, const char *device_name, synth_result_t *result);
int synth_generate_hdl(synthesis_engine_t *engine, synth_target_t target, synth_result_t *result);

/* Template management */
int synth_template_load(synthesis_engine_t *engine, const char *name, const char *template_text);
int synth_template_select(synthesis_engine_t *engine, uint32_t template_id);
synth_template_t *synth_template_find(synthesis_engine_t *engine, const char *name);

/* IRQ handling */
void synth_handle_irq(synthesis_engine_t *engine);

/* Diagnostics */
void synth_dump_state(synthesis_engine_t *engine);
const char *synth_status_string(uint8_t status);
const char *synth_target_string(synth_target_t target);
const char *synth_mode_string(synth_mode_t mode);
const char *synth_gate_string(gate_op_t op);

/* HDL generation for the synthesis engine itself (recursive hardware-as-code) */
const char *synth_engine_generate_hdl(synthesis_engine_t *engine, const char *language);

#endif /* SYNTHESIS_ENGINE_H */
