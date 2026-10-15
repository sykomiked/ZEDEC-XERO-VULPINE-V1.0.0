/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_proj.c — projections out of the shared IR: a verified program written
 * as freestanding C, as x86-64 assembly (GNU syntax) or as Forth
 * (docs/spec/ALPHABET.md).
 *
 * C and x86-64 are exact: the projected function runs on zxp_rt and prints,
 * spends fuel and traps exactly as zl_run does, down to the trapping
 * instruction. Forth is exact in what it prints and in whether it ends or
 * stops: the projection runs as a Forth program (through Lingua's own Forth
 * front-end, or gforth), so its fuel is Forth's, a trap is a stop rather
 * than the same status, and its helpers use up to four extra stack cells.
 *
 * Each target takes the integer core of the IR (32- and 64-bit words);
 * anything else is refused as UNSUPPORTED, never approximated. Lifting
 * source back into the IR is the front-ends' job and covers only what each
 * front-end accepts, so the projections are not a general bijection. */
#include "zl_int.h"
#include "zxp_rt.h"

#define S(t) zl_puts(o, t)
#define I(v) zl_puti(o, (int64_t) (v))

static int bad(zl_diag_t *d, int st, uint32_t line, const char *msg)
{
    if (d) {
        d->status = st;
        d->line = line;
        d->col = 0;
        d->msg = msg;
    }
    return st;
}

/* ---- which ops each target takes ---- */

static int core_op(int op)
{
    switch (op) {
    case ZL_HALT:
    case ZL_PUSH:
    case ZL_LOAD:
    case ZL_STORE:
    case ZL_LOADX:
    case ZL_STOREX:
    case ZL_LOADA:
    case ZL_STOREA:
    case ZL_ADD:
    case ZL_SUB:
    case ZL_MUL:
    case ZL_DIVT:
    case ZL_MODT:
    case ZL_DIVF:
    case ZL_MODF:
    case ZL_DIVX:
    case ZL_NEG:
    case ZL_ABS:
    case ZL_SGN:
    case ZL_MIN:
    case ZL_MAX:
    case ZL_EQ:
    case ZL_NE:
    case ZL_LT:
    case ZL_LE:
    case ZL_GT:
    case ZL_GE:
    case ZL_AND:
    case ZL_OR:
    case ZL_XOR:
    case ZL_INVERT:
    case ZL_NOT:
    case ZL_DUP:
    case ZL_DROP:
    case ZL_SWAP:
    case ZL_OVER:
    case ZL_ROT:
    case ZL_JMP:
    case ZL_JZ:
    case ZL_JNZ:
    case ZL_CALL:
    case ZL_RET:
    case ZL_OUTI:
    case ZL_OUTS:
    case ZL_OUTC:
    case ZL_NL:
    case ZL_TRAP:
        return 1;
    default:
        return 0;
    }
}

/* the ops C and x86-64 take beyond the core */
static int machine_op(int op)
{
    switch (op) {
    case ZL_POW:
    case ZL_TAB:
    case ZL_IN:
    case ZL_INEOF:
    case ZL_RANGE:
    case ZL_SHL:
    case ZL_SHR:
    case ZL_WRAP:
    case ZL_OUTD:
        return 1;
    default:
        return 0;
    }
}

int zl_project_op(int target, int op)
{
    if (target == ZL_PROJ_FORTH) return core_op(op);
    if (target == ZL_PROJ_C || target == ZL_PROJ_X86_64) return core_op(op) || machine_op(op);
    return 0;
}

static int name_ok(const char *n)
{
    size_t i = 0;
    if (!n || !((n[0] >= 'a' && n[0] <= 'z') || n[0] == '_')) return 0;
    for (; n[i]; i++)
        if (!((n[i] >= 'a' && n[i] <= 'z') || zl_isdigit(n[i]) || n[i] == '_') || i >= 40) return 0;
    return 1;
}

/* ---- C ---- */

static void c_bin(zl_out_t *o, const char *expr)
{
    S("NEED(2); y = st[--sp]; x = st[sp - 1]; ");
    S(expr);
    S(" CHK(z); st[sp - 1] = z;");
}

static void c_call(zl_out_t *o, const char *call)
{
    S("if ((s = ");
    S(call);
    S(") != ZL_OK) goto trap;");
}

static void project_c(zl_out_t *o, const zl_prog_t *p, const char *name)
{
    S("/* projected from Lingua IR by zl_project; runs on zxp_rt */\n");
    S("int ");
    S(name);
    S("(zxp_io_t *io)\n{\n");
    S("    int64_t *st = io->stack, *mem = io->mem, x = 0, y = 0, z = 0;\n");
    S("    uint32_t *rs = io->rstack, sp = 0, rp = 0, pc = 0, ret = 0;\n");
    S("    int s = ZL_OK;\n");
    S("    (void) st, (void) sp, (void) mem, (void) rs, (void) rp, (void) ret, (void) x, (void) y, "
      "(void) z;\n");
    S("    zxp_begin(io, ");
    I(p->nslots);
    S(");\n");
    S("#define ZF(k) do { pc = (k); if (io->used >= io->fuel) { s = ZL_E_FUEL; goto trap; } "
      "io->used++; } while (0)\n");
    S("#define ZT(t) do { s = (t); goto trap; } while (0)\n");
    S("#define NEED(n) do { if (sp < (n)) ZT(ZL_E_STACK); } while (0)\n");
    S("#define ROOM(n) do { if (sp + (n) > ZL_MAX_STACK) ZT(ZL_E_STACK); } while (0)\n");
    S("#define CHK(v) do { if (!zxp_fits(");
    I(p->word_bits);
    S(", (v))) ZT(ZL_E_OVERFLOW); } while (0)\n");
    for (uint32_t k = 0; k < p->ncode; k++) {
        const zl_ins_t *in = &p->code[k];
        S("L");
        I(k);
        S(": ZF(");
        I(k);
        S("); ");
        switch (in->op) {
        case ZL_HALT:
            S("return ZL_OK;");
            break;
        case ZL_PUSH:
            S("ROOM(1); st[sp++] = ");
            if (in->a == INT64_MIN)
                S("INT64_MIN");
            else {
                I(in->a);
                S("LL");
            }
            S(";");
            break;
        case ZL_LOAD:
            S("ROOM(1); st[sp++] = mem[");
            I(in->a);
            S("];");
            break;
        case ZL_STORE:
            S("NEED(1); mem[");
            I(in->a);
            S("] = st[--sp];");
            break;
        case ZL_LOADX:
            S("NEED(1); x = st[sp - 1]; if (x < 0 || x >= ");
            I(in->b);
            S(") ZT(ZL_E_BOUNDS); st[sp - 1] = mem[");
            I(in->a);
            S(" + x];");
            break;
        case ZL_STOREX:
            S("NEED(2); y = st[--sp]; x = st[--sp]; if (x < 0 || x >= ");
            I(in->b);
            S(") ZT(ZL_E_BOUNDS); mem[");
            I(in->a);
            S(" + x] = y;");
            break;
        case ZL_LOADA:
            S("NEED(1); x = st[sp - 1]; if (x < 0 || x >= ");
            I(p->nslots);
            S(") ZT(ZL_E_BOUNDS); st[sp - 1] = mem[x];");
            break;
        case ZL_STOREA:
            S("NEED(2); x = st[sp - 1]; if (x < 0 || x >= ");
            I(p->nslots);
            S(") ZT(ZL_E_BOUNDS); mem[x] = st[sp - 2]; sp -= 2;");
            break;
        case ZL_ADD:
            c_bin(o, "if (__builtin_add_overflow(x, y, &z)) ZT(ZL_E_OVERFLOW);");
            break;
        case ZL_SUB:
            c_bin(o, "if (__builtin_sub_overflow(x, y, &z)) ZT(ZL_E_OVERFLOW);");
            break;
        case ZL_MUL:
            c_bin(o, "if (__builtin_mul_overflow(x, y, &z)) ZT(ZL_E_OVERFLOW);");
            break;
        case ZL_DIVT:
        case ZL_MODT:
        case ZL_DIVF:
        case ZL_MODF:
        case ZL_DIVX:
            S("NEED(2); y = st[--sp]; x = st[sp - 1]; ");
            S("if ((s = zxp_div(");
            I(in->op);
            S(", x, y, ");
            I(p->word_bits);
            S(", &z)) != ZL_OK) goto trap; st[sp - 1] = z;");
            break;
        case ZL_POW:
            S("NEED(2); y = st[--sp]; x = st[sp - 1]; ");
            S("if ((s = zxp_pow(x, y, ");
            I(in->a);
            S(", ");
            I(p->word_bits);
            S(", &z)) != ZL_OK) goto trap; st[sp - 1] = z;");
            break;
        case ZL_SHL:
        case ZL_SHR:
            S("NEED(2); y = st[--sp]; x = st[sp - 1]; ");
            S("if ((s = zxp_shift(");
            I(in->op);
            S(", x, y, ");
            I(p->word_bits);
            S(", &z)) != ZL_OK) goto trap; st[sp - 1] = z;");
            break;
        case ZL_MIN:
            c_bin(o, "z = x < y ? x : y;");
            break;
        case ZL_MAX:
            c_bin(o, "z = x > y ? x : y;");
            break;
        case ZL_EQ:
            c_bin(o, "z = x == y;");
            break;
        case ZL_NE:
            c_bin(o, "z = x != y;");
            break;
        case ZL_LT:
            c_bin(o, "z = x < y;");
            break;
        case ZL_LE:
            c_bin(o, "z = x <= y;");
            break;
        case ZL_GT:
            c_bin(o, "z = x > y;");
            break;
        case ZL_GE:
            c_bin(o, "z = x >= y;");
            break;
        case ZL_AND:
            c_bin(o, "z = x & y;");
            break;
        case ZL_OR:
            c_bin(o, "z = x | y;");
            break;
        case ZL_XOR:
            c_bin(o, "z = x ^ y;");
            break;
        case ZL_NEG:
            S("NEED(1); x = st[sp - 1]; if (x == INT64_MIN) ZT(ZL_E_OVERFLOW); CHK(-x); "
              "st[sp - 1] = -x;");
            break;
        case ZL_ABS:
            S("NEED(1); x = st[sp - 1]; if (x == INT64_MIN) ZT(ZL_E_OVERFLOW); x = x < 0 ? -x : "
              "x; CHK(x); st[sp - 1] = x;");
            break;
        case ZL_SGN:
            S("NEED(1); x = st[sp - 1]; st[sp - 1] = (x > 0) - (x < 0);");
            break;
        case ZL_INVERT:
            S("NEED(1); st[sp - 1] = ~st[sp - 1];");
            break;
        case ZL_NOT:
            S("NEED(1); st[sp - 1] = st[sp - 1] == 0;");
            break;
        case ZL_DUP:
            S("NEED(1); ROOM(1); st[sp] = st[sp - 1]; sp++;");
            break;
        case ZL_DROP:
            S("NEED(1); sp--;");
            break;
        case ZL_SWAP:
            S("NEED(2); x = st[sp - 1]; st[sp - 1] = st[sp - 2]; st[sp - 2] = x;");
            break;
        case ZL_OVER:
            S("NEED(2); ROOM(1); st[sp] = st[sp - 2]; sp++;");
            break;
        case ZL_ROT:
            S("NEED(3); x = st[sp - 3]; st[sp - 3] = st[sp - 2]; st[sp - 2] = st[sp - 1]; "
              "st[sp - 1] = x;");
            break;
        case ZL_JMP:
            S("goto L");
            I(in->a);
            S(";");
            break;
        case ZL_JZ:
        case ZL_JNZ:
            S(in->op == ZL_JZ ? "NEED(1); if (st[--sp] == 0) goto L"
                              : "NEED(1); if (st[--sp] != 0) goto L");
            I(in->a);
            S(";");
            break;
        case ZL_CALL:
            S("if (rp >= ZL_MAX_CALLS) ZT(ZL_E_RSTACK); rs[rp++] = ");
            I(k + 1);
            S("; goto L");
            I(in->a);
            S(";");
            break;
        case ZL_RET:
            S("if (rp == 0) ZT(ZL_E_RSTACK); ret = rs[--rp]; goto back;");
            break;
        case ZL_OUTI:
            S("NEED(1); x = st[--sp]; ");
            S("if ((s = zxp_outi(io, x, ");
            I(in->a);
            S(", ");
            I(in->b);
            S(")) != ZL_OK) goto trap;");
            break;
        case ZL_OUTS:
            S("if ((s = zxp_outs(io, \"");
            for (uint32_t i = 0; i < p->str_len[in->a]; i++) {
                unsigned char ch = (unsigned char) p->str[p->str_off[in->a] + i];
                char e[5] = {'\\', (char) ('0' + (ch >> 6)), (char) ('0' + ((ch >> 3) & 7)),
                             (char) ('0' + (ch & 7)), 0};
                if (ch >= 0x20 && ch < 0x7F && ch != '"' && ch != '\\' && ch != '?')
                    zl_putc(o, (char) ch);
                else
                    S(e); /* three octal digits: the escape never runs on */
            }
            S("\", ");
            I(p->str_len[in->a]);
            S(")) != ZL_OK) goto trap;");
            break;
        case ZL_OUTC:
            S("NEED(1); x = st[--sp]; ");
            c_call(o, "zxp_outc(io, x)");
            break;
        case ZL_NL:
            c_call(o, "zxp_nl(io)");
            break;
        case ZL_TAB:
            S("if ((s = zxp_tab(io, ");
            I(in->a);
            S(")) != ZL_OK) goto trap;");
            break;
        case ZL_OUTD:
            S("NEED(1); x = st[--sp]; if ((s = zxp_outd(io, x, ");
            I(in->a);
            S(", ");
            I(in->b);
            S(")) != ZL_OK) goto trap;");
            break;
        case ZL_IN:
            S("ROOM(1); ");
            S("if ((s = zxp_in(io, ");
            I(p->word_bits);
            S(", &x)) != ZL_OK) goto trap; st[sp++] = x;");
            break;
        case ZL_INEOF:
            S("ROOM(1); st[sp++] = io->inpos >= io->nin;");
            break;
        case ZL_RANGE:
            S("NEED(1); if ((s = zxp_range(st[sp - 1], ");
            I(in->a);
            S(", ");
            I(in->b);
            S(")) != ZL_OK) goto trap;");
            break;
        case ZL_WRAP:
            S("NEED(1); if ((s = zxp_wrap(st[sp - 1], ");
            I(in->a);
            S(", ");
            I(in->b);
            S(", ");
            I(p->word_bits);
            S(", &z)) != ZL_OK) goto trap; st[sp - 1] = z;");
            break;
        default: /* TRAP */
            S("ZT(");
            I(in->a);
            S(");");
            break;
        }
        S("\n");
    }
    S("L");
    I(p->ncode);
    S(": pc = ");
    I(p->ncode);
    S("; ZT(ZL_E_VERIFY);\nback:\n    switch (ret) {\n");
    for (uint32_t k = 0; k < p->ncode; k++)
        if (p->code[k].op == ZL_CALL) {
            S("    case ");
            I(k + 1);
            S(": goto L");
            I(k + 1);
            S(";\n");
        }
    S("    default: pc = ret; ZT(ZL_E_VERIFY);\n    }\n");
    S("trap:\n    io->trap_pc = pc;\n    return s;\n");
    S("#undef ZF\n#undef ZT\n#undef NEED\n#undef ROOM\n#undef CHK\n}\n");
}

/* ---- x86-64 (System V, GNU as) ----
 * rbx io, r12 the data stack, r13 its depth, r14 the slots, r15 the return
 * stack, rbp its depth, (%rsp) a scratch cell for the runtime's answers. */

static const char *const TOP = "-8(%r12,%r13,8)", *const NEXT = "-16(%r12,%r13,8)";
static const char *xfn; /* the function being written: its labels are .L<fn>_... */

/* a local label: ".L" fn "_" kind k */
static void x_lbl(zl_out_t *o, const char *kind, uint32_t k)
{
    S(".L");
    S(xfn);
    S("_");
    S(kind);
    I(k);
}

/* a jump to this instruction's trap with a fixed status */
static void x_trap(zl_out_t *o, const char *cc, uint32_t k, int st)
{
    S("\t");
    S(cc);
    S(" ");
    x_lbl(o, "t", k);
    S("_");
    I(-st);
    S("\n");
}

static void x_need(zl_out_t *o, uint32_t k, int n)
{
    S("\tcmp $");
    I(n);
    S(", %r13\n");
    x_trap(o, "jb", k, ZL_E_STACK);
}

static void x_room(zl_out_t *o, uint32_t k, int n)
{
    S("\tcmp $");
    I(ZL_MAX_STACK - n);
    S(", %r13\n");
    x_trap(o, "ja", k, ZL_E_STACK);
}

static void x_chk(zl_out_t *o, const zl_prog_t *p, uint32_t k, const char *reg, const char *reg32)
{
    if (p->word_bits == 64) return;
    S("\tmovslq ");
    S(reg32);
    S(", %rcx\n\tcmp %rcx, ");
    S(reg);
    S("\n");
    x_trap(o, "jne", k, ZL_E_OVERFLOW);
}

/* call the runtime; a non-zero answer is this instruction's trap */
static void x_rtcall(zl_out_t *o, const char *fn, uint32_t k)
{
    S("\tcall ");
    S(fn);
    S("\n\ttest %eax, %eax\n\tjnz ");
    x_lbl(o, "d", k);
    S("\n");
}

static void x_bin_args(zl_out_t *o)
{
    S("\tmov ");
    S(NEXT);
    S(", %rax\n\tmov ");
    S(TOP);
    S(", %rdx\n");
}

static void x_mov(zl_out_t *o, const char *src, const char *dst)
{
    S("\tmov ");
    S(src);
    S(", ");
    S(dst);
    S("\n");
}

static void x_imm(zl_out_t *o, const char *reg, int64_t v)
{
    S("\tmovabs $");
    I(v);
    S(", ");
    S(reg);
    S("\n");
}

static void project_x86(zl_out_t *o, const zl_prog_t *p, const char *name)
{
    uint32_t n = p->ncode;
    xfn = name;
    S("# projected from Lingua IR by zl_project; runs on zxp_rt\n");
    S("\t.text\n\t.globl ");
    S(name);
    S("\n\t.type ");
    S(name);
    S(", @function\n");
    S(name);
    S(":\n\tpush %rbx\n\tpush %rbp\n\tpush %r12\n\tpush %r13\n\tpush %r14\n\tpush %r15\n");
    S("\tsub $8, %rsp\n\tmov %rdi, %rbx\n\tmov $");
    I(p->nslots);
    S(", %esi\n\tcall zxp_begin\n");
    S("\tlea ");
    I(offsetof(zxp_io_t, stack));
    S("(%rbx), %r12\n\txor %r13d, %r13d\n\tlea ");
    I(offsetof(zxp_io_t, mem));
    S("(%rbx), %r14\n\tlea ");
    I(offsetof(zxp_io_t, rstack));
    S("(%rbx), %r15\n\txor %ebp, %ebp\n");
    for (uint32_t k = 0; k < n; k++) {
        const zl_ins_t *in = &p->code[k];
        x_lbl(o, "p", k);
        S(":\n\tmov ");
        I(offsetof(zxp_io_t, used));
        S("(%rbx), %rax\n\tcmp ");
        I(offsetof(zxp_io_t, fuel));
        S("(%rbx), %rax\n");
        x_trap(o, "jae", k, ZL_E_FUEL);
        S("\tincq ");
        I(offsetof(zxp_io_t, used));
        S("(%rbx)\n");
        switch (in->op) {
        case ZL_HALT:
            S("\txor %eax, %eax\n\tjmp ");
            x_lbl(o, "ret", 0);
            S("\n");
            break;
        case ZL_PUSH:
            x_room(o, k, 1);
            x_imm(o, "%rax", in->a);
            S("\tmov %rax, (%r12,%r13,8)\n\tinc %r13\n");
            break;
        case ZL_LOAD:
            x_room(o, k, 1);
            S("\tmov ");
            I(in->a * 8);
            S("(%r14), %rax\n\tmov %rax, (%r12,%r13,8)\n\tinc %r13\n");
            break;
        case ZL_STORE:
            x_need(o, k, 1);
            S("\tdec %r13\n\tmov (%r12,%r13,8), %rax\n\tmov %rax, ");
            I(in->a * 8);
            S("(%r14)\n");
            break;
        case ZL_LOADX:
        case ZL_LOADA:
            x_need(o, k, 1);
            x_mov(o, TOP, "%rax");
            S("\tmov $");
            I(in->op == ZL_LOADX ? in->b : (int64_t) p->nslots);
            S(", %rcx\n\tcmp %rcx, %rax\n");
            x_trap(o, "jae", k, ZL_E_BOUNDS);
            S("\tmov ");
            I(in->op == ZL_LOADX ? in->a * 8 : 0);
            S("(%r14,%rax,8), %rax\n");
            x_mov(o, "%rax", TOP);
            break;
        case ZL_STOREX:
            x_need(o, k, 2);
            x_mov(o, TOP, "%rdx");
            x_mov(o, NEXT, "%rax");
            S("\tsub $2, %r13\n\tmov $");
            I(in->b);
            S(", %rcx\n\tcmp %rcx, %rax\n");
            x_trap(o, "jae", k, ZL_E_BOUNDS);
            S("\tmov %rdx, ");
            I(in->a * 8);
            S("(%r14,%rax,8)\n");
            break;
        case ZL_STOREA:
            x_need(o, k, 2);
            x_mov(o, TOP, "%rax");
            S("\tmov $");
            I(p->nslots);
            S(", %rcx\n\tcmp %rcx, %rax\n");
            x_trap(o, "jae", k, ZL_E_BOUNDS);
            x_mov(o, NEXT, "%rdx");
            S("\tmov %rdx, (%r14,%rax,8)\n\tsub $2, %r13\n");
            break;
        case ZL_ADD:
        case ZL_SUB:
        case ZL_MUL:
        case ZL_AND:
        case ZL_OR:
        case ZL_XOR:
            x_need(o, k, 2);
            x_bin_args(o);
            S(in->op == ZL_ADD   ? "\tadd %rdx, %rax\n"
              : in->op == ZL_SUB ? "\tsub %rdx, %rax\n"
              : in->op == ZL_MUL ? "\timul %rdx, %rax\n"
              : in->op == ZL_AND ? "\tand %rdx, %rax\n"
              : in->op == ZL_OR  ? "\tor %rdx, %rax\n"
                                 : "\txor %rdx, %rax\n");
            if (in->op == ZL_ADD || in->op == ZL_SUB || in->op == ZL_MUL)
                x_trap(o, "jo", k, ZL_E_OVERFLOW);
            x_chk(o, p, k, "%rax", "%eax");
            x_mov(o, "%rax", NEXT);
            S("\tdec %r13\n");
            break;
        case ZL_MIN:
        case ZL_MAX:
            x_need(o, k, 2);
            x_bin_args(o);
            S("\tcmp %rdx, %rax\n");
            S(in->op == ZL_MIN ? "\tcmovg %rdx, %rax\n" : "\tcmovl %rdx, %rax\n");
            x_mov(o, "%rax", NEXT);
            S("\tdec %r13\n");
            break;
        case ZL_EQ:
        case ZL_NE:
        case ZL_LT:
        case ZL_LE:
        case ZL_GT:
        case ZL_GE:
            x_need(o, k, 2);
            x_bin_args(o);
            S("\tcmp %rdx, %rax\n\t");
            S(in->op == ZL_EQ   ? "sete"
              : in->op == ZL_NE ? "setne"
              : in->op == ZL_LT ? "setl"
              : in->op == ZL_LE ? "setle"
              : in->op == ZL_GT ? "setg"
                                : "setge");
            S(" %al\n\tmovzbl %al, %eax\n");
            x_mov(o, "%rax", NEXT);
            S("\tdec %r13\n");
            break;
        case ZL_DIVT:
        case ZL_MODT:
        case ZL_DIVF:
        case ZL_MODF:
        case ZL_DIVX:
        case ZL_SHL:
        case ZL_SHR:
        case ZL_POW:
            x_need(o, k, 2);
            if (in->op == ZL_POW) {
                x_mov(o, NEXT, "%rdi");
                x_mov(o, TOP, "%rsi");
                S("\tmov $");
                I(in->a);
                S(", %rdx\n\tmov $");
                I(p->word_bits);
                S(", %ecx\n\tmov %rsp, %r8\n");
            } else {
                S("\tmov $");
                I(in->op);
                S(", %edi\n");
                x_mov(o, NEXT, "%rsi");
                x_mov(o, TOP, "%rdx");
                S("\tmov $");
                I(p->word_bits);
                S(", %ecx\n\tmov %rsp, %r8\n");
            }
            S("\tdec %r13\n");
            x_rtcall(o,
                     in->op == ZL_POW                       ? "zxp_pow"
                     : in->op == ZL_SHL || in->op == ZL_SHR ? "zxp_shift"
                                                            : "zxp_div",
                     k);
            S("\tmov (%rsp), %rax\n");
            x_mov(o, "%rax", TOP);
            break;
        case ZL_NEG:
        case ZL_ABS:
            x_need(o, k, 1);
            x_mov(o, TOP, "%rax");
            S("\tmov %rax, %rdx\n\tneg %rdx\n");
            x_trap(o, "jo", k, ZL_E_OVERFLOW);
            if (in->op == ZL_NEG)
                S("\tmov %rdx, %rax\n");
            else
                S("\ttest %rax, %rax\n\tcmovs %rdx, %rax\n");
            x_chk(o, p, k, "%rax", "%eax");
            x_mov(o, "%rax", TOP);
            break;
        case ZL_SGN:
            x_need(o, k, 1);
            x_mov(o, TOP, "%rax");
            S("\txor %ecx, %ecx\n\txor %edx, %edx\n\ttest %rax, %rax\n\tsetg %cl\n\tsetl "
              "%dl\n\tsub %rdx, %rcx\n");
            x_mov(o, "%rcx", TOP);
            break;
        case ZL_INVERT:
            x_need(o, k, 1);
            S("\tnotq ");
            S(TOP);
            S("\n");
            break;
        case ZL_NOT:
            x_need(o, k, 1);
            S("\tcmpq $0, ");
            S(TOP);
            S("\n\tsete %al\n\tmovzbl %al, %eax\n");
            x_mov(o, "%rax", TOP);
            break;
        case ZL_DUP:
            x_need(o, k, 1);
            x_room(o, k, 1);
            x_mov(o, TOP, "%rax");
            S("\tmov %rax, (%r12,%r13,8)\n\tinc %r13\n");
            break;
        case ZL_DROP:
            x_need(o, k, 1);
            S("\tdec %r13\n");
            break;
        case ZL_SWAP:
            x_need(o, k, 2);
            x_bin_args(o);
            x_mov(o, "%rdx", NEXT);
            x_mov(o, "%rax", TOP);
            break;
        case ZL_OVER:
            x_need(o, k, 2);
            x_room(o, k, 1);
            x_mov(o, NEXT, "%rax");
            S("\tmov %rax, (%r12,%r13,8)\n\tinc %r13\n");
            break;
        case ZL_ROT:
            x_need(o, k, 3);
            S("\tmov -24(%r12,%r13,8), %rax\n");
            x_mov(o, NEXT, "%rdx");
            S("\tmov %rdx, -24(%r12,%r13,8)\n");
            x_mov(o, TOP, "%rdx");
            x_mov(o, "%rdx", NEXT);
            x_mov(o, "%rax", TOP);
            break;
        case ZL_JMP:
            S("\tjmp ");
            x_lbl(o, "p", (uint32_t) in->a);
            S("\n");
            break;
        case ZL_JZ:
        case ZL_JNZ:
            x_need(o, k, 1);
            S("\tdec %r13\n\tcmpq $0, (%r12,%r13,8)\n\t");
            S(in->op == ZL_JZ ? "je " : "jne ");
            x_lbl(o, "p", (uint32_t) in->a);
            S("\n");
            break;
        case ZL_CALL:
            S("\tcmp $");
            I(ZL_MAX_CALLS);
            S(", %rbp\n");
            x_trap(o, "jae", k, ZL_E_RSTACK);
            S("\tmovl $");
            I(k + 1);
            S(", (%r15,%rbp,4)\n\tinc %rbp\n\tjmp ");
            x_lbl(o, "p", (uint32_t) in->a);
            S("\n");
            break;
        case ZL_RET:
            S("\ttest %rbp, %rbp\n");
            x_trap(o, "jz", k, ZL_E_RSTACK);
            S("\tdec %rbp\n\tmov (%r15,%rbp,4), %eax\n\tlea ");
            x_lbl(o, "tab", 0);
            S("(%rip), %rcx\n\tmovslq (%rcx,%rax,4), %rdx\n\tadd %rcx, %rdx\n\tjmp *%rdx\n");
            break;
        case ZL_OUTI:
        case ZL_OUTD:
            x_need(o, k, 1);
            S("\tmov %rbx, %rdi\n\tdec %r13\n\tmov (%r12,%r13,8), %rsi\n");
            x_imm(o, "%rdx", in->a);
            x_imm(o, "%rcx", in->b);
            x_rtcall(o, in->op == ZL_OUTI ? "zxp_outi" : "zxp_outd", k);
            break;
        case ZL_OUTS:
            S("\tmov %rbx, %rdi\n\tlea ");
            x_lbl(o, "s", k);
            S("(%rip), %rsi\n\tmov $");
            I(p->str_len[in->a]);
            S(", %edx\n");
            x_rtcall(o, "zxp_outs", k);
            break;
        case ZL_OUTC:
            x_need(o, k, 1);
            S("\tmov %rbx, %rdi\n\tdec %r13\n\tmov (%r12,%r13,8), %rsi\n");
            x_rtcall(o, "zxp_outc", k);
            break;
        case ZL_NL:
            S("\tmov %rbx, %rdi\n");
            x_rtcall(o, "zxp_nl", k);
            break;
        case ZL_TAB:
            S("\tmov %rbx, %rdi\n\tmov $");
            I(in->a);
            S(", %esi\n");
            x_rtcall(o, "zxp_tab", k);
            break;
        case ZL_IN:
            x_room(o, k, 1);
            S("\tmov %rbx, %rdi\n\tmov $");
            I(p->word_bits);
            S(", %esi\n\tmov %rsp, %rdx\n");
            x_rtcall(o, "zxp_in", k);
            S("\tmov (%rsp), %rax\n\tmov %rax, (%r12,%r13,8)\n\tinc %r13\n");
            break;
        case ZL_INEOF:
            x_room(o, k, 1);
            S("\tmov ");
            I(offsetof(zxp_io_t, inpos));
            S("(%rbx), %rax\n\tcmp ");
            I(offsetof(zxp_io_t, nin));
            S("(%rbx), %rax\n\tsetae %al\n\tmovzbl %al, %eax\n\tmov %rax, (%r12,%r13,8)\n\tinc "
              "%r13\n");
            break;
        case ZL_RANGE:
        case ZL_WRAP:
            x_need(o, k, 1);
            x_mov(o, TOP, "%rdi");
            x_imm(o, "%rsi", in->a);
            x_imm(o, "%rdx", in->b);
            if (in->op == ZL_RANGE)
                x_rtcall(o, "zxp_range", k);
            else {
                S("\tmov $");
                I(p->word_bits);
                S(", %ecx\n\tmov %rsp, %r8\n");
                x_rtcall(o, "zxp_wrap", k);
                S("\tmov (%rsp), %rax\n");
                x_mov(o, "%rax", TOP);
            }
            break;
        default: /* TRAP */
            S("\tmov $");
            I(in->a);
            S(", %eax\n\tjmp ");
            x_lbl(o, "d", k);
            S("\n");
            break;
        }
        /* this instruction's traps, jumped over on the way to the next */
        S("\tjmp ");
        x_lbl(o, "p", k + 1);
        S("\n");
        x_lbl(o, "d", k);
        S(":\n\tmovl $");
        I(k);
        S(", ");
        I(offsetof(zxp_io_t, trap_pc));
        S("(%rbx)\n\tjmp ");
        x_lbl(o, "ret", 0);
        S("\n");
        static const int sts[] = {ZL_E_FUEL, ZL_E_STACK, ZL_E_OVERFLOW, ZL_E_BOUNDS, ZL_E_RSTACK};
        for (uint32_t i = 0; i < sizeof sts / sizeof sts[0]; i++) {
            x_lbl(o, "t", k);
            S("_");
            I(-sts[i]);
            S(":\n\tmov $");
            I(sts[i]);
            S(", %eax\n\tjmp ");
            x_lbl(o, "d", k);
            S("\n");
        }
    }
    x_lbl(o, "p", n);
    S(":\n\tmovl $");
    I(n);
    S(", ");
    I(offsetof(zxp_io_t, trap_pc));
    S("(%rbx)\n\tmov $");
    I(ZL_E_VERIFY);
    S(", %eax\n");
    x_lbl(o, "ret", 0);
    S(":\n\tadd $8, %rsp\n\tpop %r15\n\tpop %r14\n\tpop %r13\n\tpop %r12\n\tpop "
      "%rbp\n\tpop %rbx\n\tret\n\t.size ");
    S(name);
    S(", .-");
    S(name);
    S("\n\t.section .rodata\n\t.balign 4\n");
    x_lbl(o, "tab", 0);
    S(":\n");
    for (uint32_t k = 0; k <= n; k++) {
        S("\t.long ");
        x_lbl(o, "p", k);
        S(" - ");
        x_lbl(o, "tab", 0);
        S("\n");
    }
    for (uint32_t k = 0; k < n; k++)
        if (p->code[k].op == ZL_OUTS) {
            x_lbl(o, "s", k);
            S(":");
            for (uint32_t i = 0; i < p->str_len[p->code[k].a]; i++) {
                S(i % 16 ? ", " : "\n\t.byte ");
                I((unsigned char) p->str[p->str_off[p->code[k].a] + i]);
            }
            S("\n");
        }
    S("\t.text\n");
}

/* ---- Forth ----
 * The IR's basic blocks become cases of one loop on a PC variable; the
 * slots are the cells of M, the return stack the cells of RS. Every check
 * the IR makes is made, and a failed check stops the program (ZTRAP reads
 * cell -1); Forth's own arithmetic stops on overflow and on division by
 * zero. Truth values are 1 and 0, as in the IR. */

static const char FORTH_PRELUDE[] =
    "\\ projected from Lingua IR by zl_project\n"
    "VARIABLE PC\nVARIABLE RP\nVARIABLE ZA\nVARIABLE ZB\nVARIABLE ZC\n"
    "CREATE RS 1024 CELLS ALLOT\n"
    ": ZTRAP -1 @ ;\n"
    ": ZU DUP 9 > IF DUP 10 / RECURSE THEN 10 MOD 48 + EMIT ;\n"
    ": ZW 1 SWAP BEGIN DUP 9 > WHILE 10 / SWAP 1+ SWAP REPEAT DROP ;\n"
    ": ZSP BEGIN DUP 0> WHILE SPACE 1- REPEAT DROP ;\n"
    ": ZI ZC ! ZB ! ZA ! ZA @ -9223372036854775807 1- = IF ZC @ 20 - ZSP "
    ".\" -9223372036854775808\" ELSE ZA @ ABS ZW ZA @ 0< IF 1+ ELSE ZB @ 1 AND IF 1+ THEN "
    "THEN ZC @ SWAP - ZSP ZA @ 0< IF 45 EMIT ELSE ZB @ 1 AND IF SPACE THEN THEN ZA @ ABS ZU "
    "THEN ZB @ 2 AND IF SPACE THEN ;\n"
    ": ZDIVT ZB ! ZA ! ZA @ ZB @ / ZA @ ZB @ MOD 0<> ZA @ 0< ZB @ 0< <> AND IF 1+ THEN ;\n"
    ": ZMODT ZB ! ZA ! ZA @ ZB @ MOD DUP 0<> ZA @ 0< ZB @ 0< <> AND IF ZB @ - THEN ;\n"
    ": ZDIVX 2DUP MOD 0<> IF ZTRAP THEN / ;\n"
    ": Z32 DUP -2147483648 < OVER 2147483647 > OR IF ZTRAP THEN ;\n";

/* the first instruction of each basic block */
static void leaders(const zl_prog_t *p, uint8_t *lead)
{
    for (uint32_t k = 0; k < p->ncode; k++) lead[k] = k == 0;
    for (uint32_t k = 0; k < p->ncode; k++) {
        int op = p->code[k].op;
        if (op == ZL_JMP || op == ZL_JZ || op == ZL_JNZ || op == ZL_CALL) lead[p->code[k].a] = 1;
        if ((op == ZL_JMP || op == ZL_JZ || op == ZL_JNZ || op == ZL_CALL || op == ZL_RET ||
             op == ZL_HALT || op == ZL_TRAP) &&
            k + 1 < p->ncode)
            lead[k + 1] = 1;
    }
}

static void f_check(zl_out_t *o, int64_t limit)
{
    S(" DUP 0< OVER ");
    I(limit);
    S(" < 0= OR IF ZTRAP THEN");
}

static void f_slot(zl_out_t *o, int64_t a)
{
    S(" M ");
    I(a);
    S(" CELLS +");
}

static void project_forth(zl_out_t *o, const zl_prog_t *p, uint8_t *lead)
{
    int w32 = p->word_bits == 32;
    S(FORTH_PRELUDE);
    S("CREATE M ");
    I(p->nslots ? p->nslots : 1);
    S(" CELLS ALLOT\n: ZCLR ");
    I(p->nslots ? p->nslots : 1);
    S(" 0 DO 0 M I CELLS + ! LOOP ;\n: ZMAIN ZCLR 0 PC ! 0 RP ! BEGIN\n");
    for (uint32_t k = 0; k < p->ncode; k++) {
        const zl_ins_t *in = &p->code[k];
        if (lead[k]) {
            S("PC @ ");
            I(k);
            S(" = IF");
        }
        switch (in->op) {
        case ZL_HALT:
            S(" -1 PC !");
            break;
        case ZL_PUSH:
            S(" ");
            if (in->a == INT64_MIN)
                S("-9223372036854775807 1-");
            else
                I(in->a);
            break;
        case ZL_LOAD:
            f_slot(o, in->a);
            S(" @");
            break;
        case ZL_STORE:
            f_slot(o, in->a);
            S(" !");
            break;
        case ZL_LOADX:
            f_check(o, in->b);
            S(" CELLS");
            f_slot(o, in->a);
            S(" + @");
            break;
        case ZL_STOREX:
            S(" SWAP");
            f_check(o, in->b);
            S(" CELLS");
            f_slot(o, in->a);
            S(" + !");
            break;
        case ZL_LOADA:
            f_check(o, p->nslots);
            S(" CELLS M + @");
            break;
        case ZL_STOREA:
            f_check(o, p->nslots);
            S(" CELLS M + !");
            break;
        case ZL_ADD:
            S(" +");
            break;
        case ZL_SUB:
            S(" -");
            break;
        case ZL_MUL:
            S(" *");
            break;
        case ZL_DIVT:
            S(" ZDIVT");
            break;
        case ZL_MODT:
            S(" ZMODT");
            break;
        case ZL_DIVF:
            S(" /");
            break;
        case ZL_MODF:
            S(" MOD");
            break;
        case ZL_DIVX:
            S(" ZDIVX");
            break;
        case ZL_NEG:
            S(" NEGATE");
            break;
        case ZL_ABS:
            S(" ABS");
            break;
        case ZL_SGN:
            S(" DUP 0> NEGATE SWAP 0< NEGATE -");
            break;
        case ZL_MIN:
            S(" MIN");
            break;
        case ZL_MAX:
            S(" MAX");
            break;
        case ZL_EQ:
            S(" = NEGATE");
            break;
        case ZL_NE:
            S(" <> NEGATE");
            break;
        case ZL_LT:
            S(" < NEGATE");
            break;
        case ZL_LE:
            S(" > 0= NEGATE");
            break;
        case ZL_GT:
            S(" > NEGATE");
            break;
        case ZL_GE:
            S(" < 0= NEGATE");
            break;
        case ZL_AND:
            S(" AND");
            break;
        case ZL_OR:
            S(" OR");
            break;
        case ZL_XOR:
            S(" XOR");
            break;
        case ZL_INVERT:
            S(" INVERT");
            break;
        case ZL_NOT:
            S(" 0= NEGATE");
            break;
        case ZL_DUP:
            S(" DUP");
            break;
        case ZL_DROP:
            S(" DROP");
            break;
        case ZL_SWAP:
            S(" SWAP");
            break;
        case ZL_OVER:
            S(" OVER");
            break;
        case ZL_ROT:
            S(" ROT");
            break;
        case ZL_JMP:
            S(" ");
            I(in->a);
            S(" PC !");
            break;
        case ZL_JZ:
        case ZL_JNZ:
            S(" IF ");
            I(in->op == ZL_JZ ? (int64_t) k + 1 : in->a);
            S(" ELSE ");
            I(in->op == ZL_JZ ? in->a : (int64_t) k + 1);
            S(" THEN PC !");
            break;
        case ZL_CALL:
            S(" RP @ 1024 = IF ZTRAP THEN ");
            I(k + 1);
            S(" RS RP @ CELLS + ! 1 RP +! ");
            I(in->a);
            S(" PC !");
            break;
        case ZL_RET:
            S(" RP @ 0= IF ZTRAP THEN -1 RP +! RS RP @ CELLS + @ PC !");
            break;
        case ZL_OUTI:
            S(" ");
            I(in->a);
            S(" ");
            I(in->b);
            S(" ZI");
            break;
        case ZL_OUTS: {
            const char *t = p->str + p->str_off[in->a];
            uint32_t len = p->str_len[in->a], plain = len > 0;
            for (uint32_t i = 0; i < len; i++)
                if (t[i] < 0x20 || t[i] > 0x7E || t[i] == '"') plain = 0;
            if (plain) {
                S(" .\" ");
                zl_put(o, t, len);
                S("\"");
            } else
                for (uint32_t i = 0; i < len; i++) {
                    S(" ");
                    I((unsigned char) t[i]);
                    S(" EMIT");
                }
            break;
        }
        case ZL_OUTC:
            S(" DUP 0< OVER 255 > OR IF ZTRAP THEN EMIT");
            break;
        case ZL_NL:
            S(" CR");
            break;
        default: /* TRAP */
            S(" ZTRAP");
            break;
        }
        if (w32 && (in->op == ZL_ADD || in->op == ZL_SUB || in->op == ZL_MUL || in->op == ZL_DIVT ||
                    in->op == ZL_DIVF || in->op == ZL_DIVX || in->op == ZL_NEG || in->op == ZL_ABS))
            S(" Z32");
        int ends = in->op == ZL_HALT || in->op == ZL_JMP || in->op == ZL_JZ || in->op == ZL_JNZ ||
                   in->op == ZL_CALL || in->op == ZL_RET || in->op == ZL_TRAP;
        if (!ends && (k + 1 == p->ncode || lead[k + 1])) {
            S(" ");
            I(k + 1);
            S(" PC !");
            ends = 1;
        }
        if (ends) S(" THEN\n");
    }
    S("PC @ ");
    I(p->ncode);
    S(" = IF ZTRAP THEN\nPC @ -1 = UNTIL ;\nZMAIN\nBYE\n");
}

int zl_project(int target, const zl_prog_t *p, const char *name, char *out, size_t cap,
               size_t *out_len, zl_diag_t *d)
{
    static uint8_t lead[ZL_MAX_CODE];
    if (out_len) *out_len = 0;
    if (!p || !out || !out_len || cap == 0) return bad(d, ZL_E_ARG, 0, "missing argument");
    out[0] = 0;
    if (target != ZL_PROJ_C && target != ZL_PROJ_X86_64 && target != ZL_PROJ_FORTH)
        return bad(d, ZL_E_ARG, 0, "unknown projection target");
    if (target != ZL_PROJ_FORTH && !name_ok(name))
        return bad(d, ZL_E_ARG, 0, "function name: a-z, 0-9 and _, at most 40");
    if (zl_verify(p, 0) != ZL_OK) return bad(d, ZL_E_VERIFY, 0, "the program does not verify");
    if (p->word_bits == 62)
        return bad(d, ZL_E_UNSUPPORTED, 0, "list programs (62-bit words) are not projected yet");
    for (uint32_t k = 0; k < p->ncode; k++)
        if (!zl_project_op(target, p->code[k].op))
            return bad(d, ZL_E_UNSUPPORTED, p->code[k].line, "an instruction outside the subset");
    zl_out_t ob = {out, cap, 0, 0, 0, 0, 0, 0}, *o = &ob;
    if (target == ZL_PROJ_C)
        project_c(o, p, name);
    else if (target == ZL_PROJ_X86_64)
        project_x86(o, p, name);
    else {
        leaders(p, lead);
        project_forth(o, p, lead);
    }
    if (ob.over) {
        out[0] = 0;
        return bad(d, ZL_E_LIMIT, 0, "the output buffer is too small");
    }
    *out_len = ob.len;
    return bad(d, ZL_OK, 0, "ok");
}
