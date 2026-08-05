/* zca.h — ZCA: the ZXV Card Activation language
 *
 * WHAT THIS IS
 * ------------
 * The activation section printed on a Glyph & Grid card is not prose about
 * what the card does. It IS what the card does — a short native ZXV program.
 * Scanning the card with OCR recovers that program, the kernel runs it, and
 * running it is what attaches the card's effect to your AI companion.
 *
 * A card is therefore a physical carrier of executable native code. Point a
 * camera at it and the companion changes.
 *
 * THE SECURITY PROBLEM THIS LANGUAGE EXISTS TO SOLVE
 * --------------------------------------------------
 * A card arrives as a PHOTOGRAPH. Anyone can print one. Anyone can hand you
 * one. So activation code is untrusted input in the strongest sense, and no
 * amount of "validate it carefully" is good enough.
 *
 * ZCA is built so that a hostile card cannot do damage even if it is
 * perfectly well-formed:
 *
 *   1. TOTAL. No jumps, no calls, no loops. A program is a straight list of
 *      at most ZCA_MAX_INS instructions, each executed once. It always
 *      terminates. There is no halting question to get wrong.
 *
 *   2. NO ADDRESSES. No opcode names a memory location. The entire writable
 *      surface is one card_effect_t supplied by the caller. A card cannot
 *      reach the kernel, another card, the filesystem, or the network,
 *      because the instruction set has no way to express any of them.
 *
 *   3. BOUNDED EFFECT. Axis operands are masked into range, magnitudes are
 *      clamped, and grants are masked to the four known capability bits. The
 *      worst a malicious card can do is be a legal card. That is the whole
 *      threat model, and it is closed by construction rather than by checks
 *      that someone must remember to write.
 *
 *   4. SEALED. Every program ends with SEAL, a CRC16 over everything before
 *      it plus the card index. OCR misreads are the normal case, not the
 *      exception, and a garbled scan must be REJECTED — never silently
 *      executed as a different but still valid program. The seal is what
 *      makes "I misread a digit" fail loudly.
 *
 * PRINTED CARDS THAT ALREADY EXIST
 * --------------------------------
 * The 52,095 cards are already printed and they are good as they are. They
 * do not show a ZCA listing — so the program is DERIVED deterministically
 * from the attributes the card does print (discipline, gematria, root, set).
 * zca_derive() and zca_assemble() are guaranteed to agree: derive -> emit ->
 * assemble is byte-identical, which test_zca.c asserts.
 *
 * So both paths converge:
 *   * existing card -> OCR reads its printed attributes -> derive -> program
 *   * future card   -> OCR reads its printed ZCA listing  -> assemble -> program
 * and they produce the same bytes. No reprint is needed to make the existing
 * deck executable.
 *
 * OPERATING INSTRUCTIONS
 * ----------------------
 * Lines beginning with ';' are operating instructions for the human holding
 * the card. The VM skips them. The activation section thus reads as both a
 * program and its own manual, which is the point: the card explains itself
 * to its owner in the same block of text the machine executes.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV card-activation slice)
 * License: SEL-3.3
 */
#ifndef ZXV_ZCA_H
#define ZXV_ZCA_H

#include <stdint.h>
#include <stdbool.h>
#include "../surplus/surplus.h"
#include "../chiglet/chiglet.h"

#define ZCA_MAX_INS      16u
#define ZCA_MAX_TEXT     512u
#define ZCA_MAG_MAX      1000u    /* magnitudes are milli-units, 0.000..1.000 */

/* Opcodes. Deliberately few: every one is a pure write into card_effect_t. */
typedef enum {
    ZCA_OP_NOP   = 0,
    ZCA_OP_AXIS  = 1,   /* a=axis, b=milli — the card's primary direction  */
    ZCA_OP_TILT  = 2,   /* a=axis, b=milli — a secondary component         */
    ZCA_OP_GRANT = 3,   /* b=capability mask                               */
    ZCA_OP_BIND  = 4,   /* a=field, b=value — an attribute the card carries*/
    ZCA_OP_SEAL  = 5,   /* b=crc16 — terminator; a program without it is   */
                        /*           not a program                         */
    ZCA_OP__MAX
} zca_op_t;

/* BIND fields */
#define ZCA_FIELD_GEM    1u
#define ZCA_FIELD_ROOT   2u
#define ZCA_FIELD_TAROT  3u
#define ZCA_FIELD_SET    4u
#define ZCA_FIELD__MAX   5u

typedef struct { uint8_t op, a; uint16_t b; } zca_ins_t;

typedef struct {
    uint32_t  card_index;
    uint8_t   n;
    zca_ins_t ins[ZCA_MAX_INS];
} zca_program_t;

/* The ENTIRE writable surface of a running card program. */
typedef struct {
    surplus_real_t evidence[CHG_DIM];
    uint32_t       grants;
    uint16_t       attr[ZCA_FIELD__MAX];
} card_effect_t;

typedef enum {
    ZCA_OK = 0,
    ZCA_ERR_ARG,
    ZCA_ERR_MAGIC,      /* not a ZCA1 listing */
    ZCA_ERR_OPCODE,     /* unknown mnemonic — refuse, never guess */
    ZCA_ERR_OPERAND,
    ZCA_ERR_TOO_LONG,
    ZCA_ERR_NO_SEAL,    /* ran off the end without a seal */
    ZCA_ERR_SEAL        /* seal present but wrong: the scan was garbled */
} zca_status_t;

/* Card attributes as printed. This is what OCR recovers from an existing
 * card, and it is sufficient to reconstruct the program exactly. */
typedef struct {
    uint32_t index;
    uint8_t  discipline;
    uint8_t  set;
    uint16_t gematria;
    uint8_t  root;
    uint8_t  tarot;
} zca_attrs_t;

/* Build the canonical program for a card from its printed attributes. */
zca_status_t zca_derive(const zca_attrs_t *a, zca_program_t *out);

/* Parse a printed ZCA listing (what OCR hands us). Rejects a bad seal. */
zca_status_t zca_assemble(const char *text, uint32_t len, zca_program_t *out);

/* Render a program as the listing that gets printed on the card. Returns
 * bytes written, 0 on failure. Emits no operating instructions — callers
 * add those as ';' lines. */
uint32_t zca_emit(const zca_program_t *p, char *out, uint32_t cap);

/* Run a program. Cannot fail on a well-formed program and cannot escape
 * `out`; every operand is clamped or masked on the way in. */
zca_status_t zca_exec(const zca_program_t *p, card_effect_t *out);

/* The seal a program should carry, given its instructions and card index. */
uint16_t zca_seal(const zca_program_t *p);

const char *zca_status_name(zca_status_t s);
const char *zca_discipline_name(uint8_t discipline);
/* Resolve a discipline name to its id; returns -1 if unknown. Case- and
 * space-insensitive, because OCR is neither. */
int32_t zca_discipline_id(const char *name, uint32_t len);
#define ZCA_DISCIPLINES  38u

#endif /* ZXV_ZCA_H */
