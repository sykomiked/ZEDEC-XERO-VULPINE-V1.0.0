/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aml_core.c — the evaluator core at the boundaries: every operator of
 * fixtures/core.asl called with many argument values, once in a revision 2
 * table (64-bit integers) and once in the same table marked revision 1
 * (32-bit integers); the heap, value and conversion helpers called directly;
 * budgets at their exact limits; references, typed stores, buffer fields,
 * the notify ring; and hand-assembled AML for what iasl will not emit.
 * Each operator call checks the value, the error code, that the heap went
 * back to where it was and that no frame or nesting level was left behind. */
#include "amltest.h"

#define ONES 0xFFFFFFFFFFFFFFFFull

static amlfake_t f;
static aml_ctx_t c;
static int m64;     /* the loaded table's integer width: 1 = 64 bits */
static int resized; /* the call changes the size of a named object */
static uint8_t tbl[1 << 16];
static uint8_t core32[sizeof fx_core];

static uint32_t heap(void)
{
    return aml_stats(&c).heap_used;
}

/* ---- value builders ------------------------------------------------------- */
static aml_val_t VI(uint64_t v)
{
    return aml_int(v);
}

static aml_val_t VS(const char *s)
{
    return aml_str(&c, s);
}

static aml_val_t VB(const uint8_t *b, uint32_t n)
{
    return aml_buf(&c, b, n);
}
#define B(...) VB((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

/* A package of n integers. */
static aml_val_t VP(uint32_t n, const uint64_t *ints)
{
    aml_val_t p;
    memset(&p, 0, sizeof p);
    p.u.elems = (aml_val_t *) aml_alloc(&c, (n ? n : 1) * (uint32_t) sizeof(aml_val_t));
    if (!p.u.elems) return p;
    p.type = AML_T_PKG;
    p.len = n;
    for (uint32_t i = 0; i < n; i++) p.u.elems[i] = aml_int(ints[i]);
    return p;
}
#define P(...)                                                                                     \
    VP(sizeof((const uint64_t[]){__VA_ARGS__}) / sizeof(uint64_t), (const uint64_t[]){__VA_ARGS__})

/* 64-bit or 32-bit expectation */
#define W(a, b) (m64 ? (uint64_t) (a) : (uint64_t) (b))

/* ---- callers -------------------------------------------------------------- */
static int call(const char *m, aml_val_t *a, uint32_t n, aml_val_t *out)
{
    int e = aml_eval(&c, 0, m, a, n, out);
    for (uint32_t i = 0; i < n; i++) aml_val_free(&c, &a[i]);
    return e;
}

static void after(int line, const char *m, uint32_t h0)
{
    g_checks += 3;
    if (heap() != h0 && !resized) {
        g_fails++;
        printf("FAIL line %d %s: heap %u, was %u\n", line, m, heap(), h0);
    }
    if (c.depth || c.nest) {
        g_fails++;
        printf("FAIL line %d %s: depth %u nest %u\n", line, m, c.depth, c.nest);
    }
}

static void t_int(int line, const char *m, aml_val_t *a, uint32_t n, int err, uint64_t want,
                  uint32_t h0)
{
    aml_val_t r;
    uint64_t v = 0xDEAD;
    int e = call(m, a, n, &r);
    if (!e) {
        if (r.type != AML_T_INT) e = 1000 + r.type;
        v = r.u.i;
        aml_val_free(&c, &r);
    }
    g_checks++;
    if (e != err || (!e && v != want)) {
        g_fails++;
        printf("FAIL line %d %s (%s): err %d value 0x%llx, want err %d value 0x%llx\n", line, m,
               m64 ? "64" : "32", e, (unsigned long long) v, err, (unsigned long long) want);
    }
    after(line, m, h0);
}

static void t_val(int line, const char *m, aml_val_t *a, uint32_t n, uint8_t type, const void *want,
                  uint32_t wlen, uint32_t h0)
{
    aml_val_t r;
    int e = call(m, a, n, &r);
    int ok = !e && r.type == type && r.len == wlen && memcmp(r.u.bytes, want, wlen) == 0 &&
             (type != AML_T_STR || r.u.bytes[wlen] == 0);
    g_checks++;
    if (!ok) {
        g_fails++;
        printf("FAIL line %d %s (%s): err %d type %u len %u", line, m, m64 ? "64" : "32", e, r.type,
               r.len);
        if (!e && (r.type == AML_T_STR || r.type == AML_T_BUF))
            for (uint32_t i = 0; i < r.len && i < 24; i++) printf(" %02x", r.u.bytes[i]);
        printf("\n");
    }
    aml_val_free(&c, &r);
    after(line, m, h0);
}

/* TI(method, err, want, args...): integer result. TS: string. TB: buffer. */
#define TI(m, err, want, ...)                                                                      \
    do {                                                                                           \
        uint32_t h_ = heap();                                                                      \
        aml_val_t a_[] = {__VA_ARGS__};                                                            \
        t_int(__LINE__, m, a_, sizeof a_ / sizeof a_[0], err, want, h_);                           \
    } while (0)
#define T0(m, err, want)                                                                           \
    do {                                                                                           \
        uint32_t h_ = heap();                                                                      \
        t_int(__LINE__, m, 0, 0, err, want, h_);                                                   \
    } while (0)
#define TS(m, want, ...)                                                                           \
    do {                                                                                           \
        uint32_t h_ = heap();                                                                      \
        aml_val_t a_[] = {__VA_ARGS__};                                                            \
        t_val(__LINE__, m, a_, sizeof a_ / sizeof a_[0], AML_T_STR, want, strlen(want), h_);       \
    } while (0)
#define TB(m, want, ...)                                                                           \
    do {                                                                                           \
        uint32_t h_ = heap();                                                                      \
        aml_val_t a_[] = {__VA_ARGS__};                                                            \
        t_val(__LINE__, m, a_, sizeof a_ / sizeof a_[0], AML_T_BUF, want, h_);                     \
    } while (0)
#define TB0(m, want)                                                                               \
    do {                                                                                           \
        uint32_t h_ = heap();                                                                      \
        t_val(__LINE__, m, 0, 0, AML_T_BUF, want, h_);                                             \
    } while (0)
#define ARR(x)     x, sizeof x
#define BYTES(...) (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__})

static int load(int wide)
{
    fk_init(&f);
    if (aml_init(&c, &f.plat, g_arena, ARENA_SIZE)) return -100;
    m64 = wide;
    int r = aml_load_table(&c, wide ? fx_core : core32, sizeof fx_core);
    /* the first named object a method creates grows the node pool once */
    if (!r) r = aml_eval(&c, 0, "BFE3", 0, 0, 0);
    return r;
}

/* ---- operators -------------------------------------------------------------- */
static void test_arith(void)
{
    TI("OADD", 0, 12, VI(5), VI(7));
    TI("OADD", 0, 1, VI(ONES), VI(2));
    TI("OADD", 0, W(0x100000000ull, 0), VI(0xFFFFFFFF), VI(1));
    TI("OSUB", 0, W(0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFE), VI(3), VI(5));
    TI("OSUB", 0, 2, VI(5), VI(3));
    TI("OMUL", 0, W(0x100000000ull, 0), VI(0x10000), VI(0x10000));
    TI("OMUL", 0, 21, VI(3), VI(7));
    TI("OSHL", 0, W(0x8000000000000000ull, 0), VI(1), VI(63));
    TI("OSHL", 0, 0, VI(1), VI(64));
    TI("OSHL", 0, 0, VI(1), VI(65));
    TI("OSHL", 0, 6, VI(3), VI(1));
    TI("OSHL", 0, W(0x100000000ull, 0), VI(1), VI(32));
    TI("OSHL", 0, 0x80000000, VI(1), VI(31));
    TI("OSHR", 0, W(1, 0), VI(0x8000000000000000ull), VI(63));
    TI("OSHR", 0, 0, VI(ONES), VI(64));
    TI("OSHR", 0, 0x10, VI(0x100), VI(4));
    TI("OSHR", 0, W(1, 0), VI(ONES), VI(63));
    TI("OAND", 0, 0xF0, VI(0xF0F0), VI(0x0FF0));
    TI("ONAN", 0, W(0xFFFFFFFFFFFFFFCFull, 0xFFFFFFCF), VI(0xF0), VI(0x3C));
    TI("OOR_", 0, 0xF00F, VI(0xF000), VI(0x0F));
    TI("ONOR", 0, W(0xFFFFFFFFFFFFFF00ull, 0xFFFFFF00), VI(0xF0), VI(0x0F));
    TI("OXOR", 0, 0xF0F0, VI(0xFF00), VI(0x0FF0));
    TI("OMOD", 0, 2, VI(100), VI(7));
    TI("OMOD", 0, 0, VI(7), VI(7));
    TI("OMOD", AML_EDIV0, 0, VI(5), VI(0));
    TI("ODIV", 0, 14, VI(100), VI(7));
    TI("ODIV", AML_EDIV0, 0, VI(1), VI(0));
    TI("ODVR", 0, 2, VI(100), VI(7));
    TI("ODVQ", 0, 14, VI(100), VI(7));
    TI("ODVR", AML_EDIV0, 0, VI(100), VI(0));
    TI("ONOT", 0, W(0xFFFFFFFFFFFFFFF0ull, 0xFFFFFFF0), VI(0x0F));
    TI("ONOT", 0, W(ONES, 0xFFFFFFFF), VI(0));
    TI("OFSL", 0, 0, VI(0));
    TI("OFSL", 0, 1, VI(1));
    TI("OFSL", 0, 3, VI(6));
    TI("OFSL", 0, 32, VI(0x80000000));
    TI("OFSL", 0, W(64, 0), VI(0x8000000000000000ull));
    TI("OFSL", 0, W(64, 32), VI(ONES));
    TI("OFSR", 0, 0, VI(0));
    TI("OFSR", 0, 1, VI(1));
    TI("OFSR", 0, 2, VI(6));
    TI("OFSR", 0, 1, VI(ONES));
    TI("OFSR", 0, W(64, 0), VI(0x8000000000000000ull));
    TI("OFSR", 0, W(33, 0), VI(0x100000000ull));
    TI("OINC", 0, 6, VI(5));
    TI("OINC", 0, 0, VI(ONES));
    TI("OINC", 0, W(0x100000000ull, 0), VI(0xFFFFFFFF));
    TI("ODEC", 0, 4, VI(5));
    TI("ODEC", 0, W(ONES, 0xFFFFFFFF), VI(0));
    TI("ODEC", AML_ETYPE, 0, P(1));
    T0("QWRD", 0, W(0x1122334455667788ull, 0x55667788));
}

static void test_logic(void)
{
    uint64_t T = W(ONES, 0xFFFFFFFF);
    TI("OLAN", 0, T, VI(1), VI(2));
    TI("OLAN", 0, 0, VI(0), VI(1));
    TI("OLAN", 0, 0, VI(1), VI(0));
    TI("OLOR", 0, 0, VI(0), VI(0));
    TI("OLOR", 0, T, VI(0), VI(3));
    TI("OLOR", 0, T, VI(3), VI(0));
    TI("OLNT", 0, T, VI(0));
    TI("OLNT", 0, 0, VI(2));
    TI("OLNT", 0, W(0, 0xFFFFFFFF), VI(0x100000000ull));
    TI("OLEQ", 0, T, VI(5), VI(5));
    TI("OLEQ", 0, 0, VI(5), VI(6));
    TI("OLEQ", 0, 0, VI(6), VI(5));
    TI("OLGT", 0, T, VI(6), VI(5));
    TI("OLGT", 0, 0, VI(5), VI(5));
    TI("OLGT", 0, 0, VI(5), VI(6));
    TI("OLLT", 0, T, VI(5), VI(6));
    TI("OLLT", 0, 0, VI(5), VI(5));
    TI("OLLT", 0, 0, VI(6), VI(5));
    TI("OLEQ", 0, T, VS("abc"), VS("abc"));
    TI("OLEQ", 0, 0, VS("abc"), VS("abd"));
    TI("OLEQ", 0, 0, VS("ab"), VS("abc"));
    TI("OLEQ", 0, 0, VS("abc"), VS("ab"));
    TI("OLLT", 0, T, VS("abc"), VS("abd"));
    TI("OLLT", 0, 0, VS("abd"), VS("abc"));
    TI("OLGT", 0, T, VS("abd"), VS("abc"));
    TI("OLLT", 0, T, VS("ab"), VS("abc"));
    TI("OLGT", 0, T, VS("abc"), VS("ab"));
    TI("OLLT", 0, 0, VS("abc"), VS("ab"));
    TI("OLGT", 0, 0, VS("ab"), VS("abc"));
    TI("OLGT", 0, T, VS("b"), VS("abc"));
    TI("OLLT", 0, T, VS("abc"), VS("b"));
    TI("OLEQ", 0, T, B(1, 2), B(1, 2));
    TI("OLLT", 0, T, B(1), B(1, 0));
    TI("OLGT", 0, T, B(2), B(1, 5));
    TI("OLLT", 0, T, B(0x10), B(0x80));
    TI("OLGT", 0, T, B(0x80), B(0x10));
    /* the second operand converts to the first one's type */
    TI("OLEQ", 0, 0, VS("1F"), VI(0x1F));
    TI("OLEQ", 0, T, VI(0x1F), VS("1F"));
    TI("OLEQ", 0, T, VS(m64 ? "000000000000001F" : "0000001F"), VI(0x1F));
    TI("OLEQ", 0, W(T, 0), B(1, 0, 0, 0, 0, 0, 0, 0), VI(1));
    TI("OLEQ", 0, W(0, T), B(1, 0, 0, 0), VI(1));
    TI("OLEQ", 0, T, B(0x41, 0), VS("A"));
    TI("OLEQ", AML_ETYPE, 0, P(1), VI(1));
    TI("OLEQ", AML_ETYPE, 0, VI(1), P(1));
    TI("OLEQ", AML_ETYPE, 0, VS("a"), P(1));
}

static void test_strings(void)
{
    static const uint8_t c11[] = {1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t c11n[] = {1, 0, 0, 0, 2, 0, 0, 0};
    static char big[AML_MAX_OBJ / 5 + 2];
    if (m64)
        TB("OCAT", ARR(c11), VI(1), VI(2));
    else
        TB("OCAT", ARR(c11n), VI(1), VI(2));
    if (m64)
        TB("OCAT",
           BYTES(0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB,
                 0xAA, 0x99),
           VI(0x1122334455667788ull), VI(0x99AABBCCDDEEFF00ull));
    else
        TB("OCAT", BYTES(0x88, 0x77, 0x66, 0x55, 0x00, 0xFF, 0xEE, 0xDD), VI(0x1122334455667788ull),
           VI(0x99AABBCCDDEEFF00ull));
    TS("OCAT", m64 ? "ab000000000000001F" : "ab0000001F", VS("ab"), VI(0x1F));
    TS("OCAT", "ab01 A2", VS("ab"), B(1, 0xA2));
    TS("OCAT", "ab", VS("a"), VS("b"));
    TS("OCAT", "", VS(""), VS(""));
    TB("OCAT", BYTES(1, 2, 'x', 'y'), B(1, 2), VS("xy"));
    TB("OCAT", BYTES(1), B(1), VS(""));
    if (m64)
        TB("OCAT", BYTES(1, 2, 3, 0, 0, 0, 0, 0, 0), B(1), VI(0x0302));
    else
        TB("OCAT", BYTES(1, 2, 3, 0, 0), B(1), VI(0x0302));
    TB("OCAT", BYTES(1, 9, 8), B(1), B(9, 8));
    TI("OCAT", AML_ETYPE, 0, P(1), VI(1));
    TI("OCAT", AML_ETYPE, 0, VI(1), P(1));
    TI("OCAT", AML_ETYPE, 0, VS("a"), P(1));
    TI("OCAT", AML_ETYPE, 0, B(1), P(1));

    /* ConcatenateResTemplate: each side up to (not including) its end tag */
    TB("OCRT", BYTES(0x47, 1, 2, 3, 4, 5, 6, 7, 0x22, 1, 0, 0x79, 0),
       B(0x47, 1, 2, 3, 4, 5, 6, 7, 0x79, 0), B(0x22, 1, 0, 0x79, 0));
    TB("OCRT", BYTES(0x86, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x22, 1, 0, 0x79, 0),
       B(0x86, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x79, 0), B(0x22, 1, 0));
    TB("OCRT", BYTES(0x86, 1, 0, 0xAA, 0x22, 1, 0, 0x79, 0), B(0x86, 1, 0, 0xAA, 0x79, 0),
       B(0x22, 1, 0));
    TB("OCRT", BYTES(0x22, 1, 0, 0x79, 0), B(0x86, 9, 0, 1, 2), B(0x22, 1, 0));
    TB("OCRT", BYTES(0x22, 1, 0, 0x79, 0), B(0x86, 9), B(0x22, 1, 0));
    TB("OCRT", BYTES(0x86, 0, 0, 0x79, 0), B(0x86, 0, 0, 0x23, 1), B(0x78, 1, 0));
    TB("OCRT", BYTES(0x22, 1, 0, 0x86, 1, 0, 7, 0x79, 0), B(0x22, 1, 0, 0x7F, 0, 0, 0, 0, 0, 0),
       B(0x86, 1, 0, 7));
    TB("OCRT", BYTES(0x23, 1, 2, 3, 0x79, 0), B(0x23, 1, 2, 3), B(0x79));
    TB("OCRT", BYTES(0x79, 0), B(0x23, 1, 2), B(0x86, 0, 1, 5));
    TI("OCRT", AML_ETYPE, 0, VI(1), B(0x79, 0));
    TI("OCRT", AML_ETYPE, 0, B(0x79, 0), VS("x"));

    /* explicit conversions */
    if (m64)
        TB("OTBF", BYTES(2, 1, 0, 0, 0, 0, 0, 0), VI(0x0102));
    else
        TB("OTBF", BYTES(2, 1, 0, 0), VI(0x0102));
    TB("OTBF", BYTES(0x41, 0x42, 0), VS("AB"));
    TB("OTBF", BYTES(0), VS(""));
    TB("OTBF", BYTES(5), B(5));
    TI("OTBF", AML_ETYPE, 0, P(1));
    TS("OTDS", "0", VI(0));
    TS("OTDS", "7", VI(7));
    TS("OTDS", "1234", VI(1234));
    TS("OTDS", m64 ? "18446744073709551615" : "4294967295", VI(ONES));
    TS("OTDS", "0,9,10,99,100,255", B(0, 9, 10, 99, 100, 255));
    TS("OTDS", "", VB(0, 0));
    TS("OTDS", "str", VS("str"));
    TS("OTHS", m64 ? "00000000000000AB" : "000000AB", VI(0xAB));
    TS("OTHS", m64 ? "FEDCBA9876543210" : "76543210", VI(0xFEDCBA9876543210ull));
    TS("OTHS", "0xAB,0x01", B(0xAB, 0x01));
    TS("OTHS", "0x0F", B(0x0F));
    TS("OTHS", "x", VS("x"));
    TI("OTHS", AML_ETYPE, 0, P(1));
    TI("OTDS", AML_ETYPE, 0, P(1));
    /* a buffer whose string would pass the object size limit */
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = 0;
    {
        uint32_t h = heap();
        aml_val_t a = VB((const uint8_t *) big, AML_MAX_OBJ / 5 + 1);
        t_int(__LINE__, "OTHS", &a, 1, AML_ENOMEM, 0, h);
        aml_val_t r;
        a = VB((const uint8_t *) big, AML_MAX_OBJ / 5);
        CHECK_EQ(call("OTHS", &a, 1, &r), 0);
        CHECK_EQ(r.len, AML_MAX_OBJ / 5 * 5 - 1);
        CHECK_EQ(r.u.bytes[4], ',');
        CHECK_EQ(r.u.bytes[r.len], 0);
        aml_val_free(&c, &r);
        CHECK_EQ(heap(), h);
    }
    TI("OTIN", 0, 123, VS("123"));
    TI("OTIN", 0, 0x1F, VS("  0x1F"));
    TI("OTIN", 0, 0x1F, VS("0X1f"));
    TI("OTIN", 0, 0xAF, VS("0xaFg"));
    TI("OTIN", 0, 0x9A, VS("0x9A:"));
    TI("OTIN", 0, 42, VS("\t 42abc"));
    TI("OTIN", 0, 0, VS(""));
    TI("OTIN", 0, 0, VS(" "));
    TI("OTIN", 0, 0, VS("0x"));
    TI("OTIN", 0, 0, VS("0"));
    TI("OTIN", 0, 9, VS("09"));
    TI("OTIN", 0, 90, VS("90:"));
    TI("OTIN", 0, 1, VS("1/"));
    TI("OTIN", 0, W(0x6BC75E2D630FFFFFull, 0x630FFFFF), VS("99999999999999999999"));
    TI("OTIN", 0, W(0x123456789ull, 0x23456789), VS("0x123456789"));
    TI("OTIN", 0, 0x1234, B(0x34, 0x12));
    TI("OTIN", 0, 5, VI(5));
    TI("OCPY", 0, W(ONES, 0xFFFFFFFF), VI(ONES));
    TI("OTIN", AML_ETYPE, 0, P(1));
    TS("OTST", "AB", B(0x41, 0x42, 0, 0x43), VI(ONES));
    TS("OTST", "AB", B(0x41, 0x42, 0x43), VI(2));
    TS("OTST", "ABC", B(0x41, 0x42, 0x43), VI(3));
    TS("OTST", "ABC", B(0x41, 0x42, 0x43), VI(4));
    TS("OTST", "", B(0x41, 0x42), VI(0));
    TS("OTST", "", B(0), VI(5));
    TI("OTST", AML_ETYPE, 0, VS("x"), VI(1));
    TS("OMID", "ell", VS("hello"), VI(1), VI(3));
    TS("OMID", "", VS("hello"), VI(5), VI(1));
    TS("OMID", "", VS("hello"), VI(6), VI(1));
    TS("OMID", "o", VS("hello"), VI(4), VI(10));
    TS("OMID", "hello", VS("hello"), VI(0), VI(5));
    TS("OMID", "hell", VS("hello"), VI(0), VI(4));
    TS("OMID", "", VS("hello"), VI(0), VI(0));
    TS("OMID", m64 ? "bc" : "b", VS("abc"), VI(1), VI(0x100000001ull));
    TS("OMID", m64 ? "" : "a", VS("abc"), VI(0x100000000ull), VI(1));
    TB("OMID", BYTES(3, 4), B(1, 2, 3, 4), VI(2), VI(10));
    TB("OMID", BYTES(2), B(1, 2, 3, 4), VI(1), VI(1));
    TI("OMID", AML_ETYPE, 0, VI(5), VI(0), VI(1));
    TI("OFBC", 0, 9876, VI(0x9876));
    TI("OFBC", 0, 99, VI(0x99));
    TI("OFBC", 0, 0, VI(0));
    TI("OFBC", 0, W(1234567890123456ull, 90123456), VI(0x1234567890123456ull));
    TI("OFBC", 0, W(9999999999999999ull, 99999999), VI(0x9999999999999999ull));
    TI("OTBC", 0, 0x1234, VI(1234));
    TI("OTBC", 0, 0, VI(0));
    TI("OTBC", 0, 0x10, VI(10));
    TI("OTBC", 0, W(0x9999999999999999ull, 0x74919423), VI(9999999999999999ull));
    TI("OTBC", 0, W(0x2345678901234567ull, 0), VI(m64 ? 12345678901234567ull : 0));
    TI("OSIZ", 0, 5, VS("hello"));
    TI("OSIZ", 0, 0, VS(""));
    TI("OSIZ", 0, 3, B(1, 2, 3));
    TI("OSIZ", 0, 4, P(1, 2, 3, 4));
    TI("OSIZ", AML_ETYPE, 0, VI(1));
    TI("OTYP", 0, 1, VI(1));
    TI("OTYP", 0, 2, VS("a"));
    TI("OTYP", 0, 3, B(1));
    TI("OTYP", 0, 4, P(1));
}

static void test_packages(void)
{
    uint64_t N = W(ONES, 0xFFFFFFFF);
    TI("OIDX", 0, 1, P(1, 2, 3), VI(0));
    TI("OIDX", 0, 3, P(1, 2, 3), VI(2));
    TI("OIDX", AML_EBOUNDS, 0, P(1, 2, 3), VI(3));
    TI("OIDX", m64 ? AML_EBOUNDS : 0, 1, P(1, 2, 3), VI(0x100000000ull));
    TI("OIDX", 0, 6, B(4, 5, 6), VI(2));
    TI("OIDX", AML_EBOUNDS, 0, B(4, 5, 6), VI(3));
    TI("OIDX", 0, 'y', VS("xyz"), VI(1));
    TI("OIDX", AML_EBOUNDS, 0, VS("xyz"), VI(3));
    TI("OIDX", AML_ETYPE, 0, VI(5), VI(0));
    TI("OITY", 0, 1, P(1, 2), VI(1));
    TI("OITY", 0, AML_T_BUFFIELD, B(1), VI(0));
    TI("OITY", 0, AML_T_BUFFIELD, VS("ab"), VI(1));
    TI("OMEQ", 0, 2, P(1, 5, 10, 15), VI(10), VI(0));
    TI("OMEQ", 0, 2, P(1, 5, 10, 15), VI(10), VI(2));
    TI("OMEQ", 0, N, P(1, 5, 10, 15), VI(10), VI(3));
    TI("OMEQ", 0, 3, P(1, 5, 10, 15), VI(15), VI(3));
    TI("OMEQ", 0, N, P(1, 5, 10, 15), VI(99), VI(0));
    TI("OMEQ", AML_EBOUNDS, 0, P(1, 5, 10, 15), VI(1), VI(4));
    TI("OMEQ", AML_ETYPE, 0, VI(1), VI(1), VI(0));
    TI("OMLE", 0, 1, P(1, 5, 10, 15), VI(5), VI(1));
    TI("OMLE", 0, 1, P(1, 5, 10, 15), VI(10), VI(1));
    TI("OMLE", 0, N, P(1, 5, 10, 15), VI(4), VI(1));
    TI("OMLT", 0, N, P(1, 5, 10, 15), VI(5), VI(1));
    TI("OMLT", 0, 1, P(1, 5, 10, 15), VI(6), VI(1));
    TI("OMGE", 0, 2, P(1, 5, 10, 15), VI(10), VI(0));
    TI("OMGE", 0, 3, P(1, 5, 10, 15), VI(11), VI(0));
    TI("OMGE", 0, N, P(1, 5, 10, 15), VI(16), VI(0));
    TI("OMGT", 0, 3, P(1, 5, 10, 15), VI(10), VI(0));
    TI("OMGT", 0, 2, P(1, 5, 10, 15), VI(9), VI(0));
    TI("OMGT", 0, N, P(1, 5, 10, 15), VI(15), VI(0));
    TI("OMTR", 0, 2, P(1, 5, 10, 15), VI(10), VI(0));
    TI("OMTR", 0, N, P(1, 5, 10, 15), VI(11), VI(0));
    TI("OMEQ", AML_ETYPE, 0, P(1, 5), P(1), VI(0));
    {
        /* elements that are not integers, strings or buffers are skipped */
        uint32_t h = heap();
        aml_val_t a[3] = {P(10, 10), VI(10), VI(0)};
        aml_val_free(&c, &a[0].u.elems[0]);
        a[0].u.elems[0] = P(10);
        t_int(__LINE__, "OMEQ", a, 3, 0, 1, h);
    }
    {
        uint32_t h = heap();
        aml_val_t a[3] = {P(0, 0), VS("bb"), VI(0)};
        a[0].u.elems[0] = VS("a");
        a[0].u.elems[1] = VS("bb");
        t_int(__LINE__, "OMEQ", a, 3, 0, 1, h);
    }
    {
        uint32_t h = heap();
        aml_val_t a[3] = {P(0, 0), B(2), VI(0)};
        a[0].u.elems[0] = B(1);
        a[0].u.elems[1] = B(2);
        t_int(__LINE__, "OMGE", a, 3, 0, 1, h);
    }
    TB("OBUF", BYTES(1, 2, 3, 0, 0), VI(5));
    TB("OBUF", BYTES(1, 2, 3), VI(2));
    TB("OBUF", BYTES(1, 2, 3), VI(3));
    TB("OBUF", BYTES(1, 2, 3, 0), VI(4));
    TB("OBUF", BYTES(1, 2, 3), VI(0));
    TI("OBUF", AML_ENOMEM, 0, VI(AML_MAX_OBJ + 1));
    if (m64) TI("OBUF", AML_ENOMEM, 0, VI(0x100000003ull));
    {
        aml_val_t a = VI(AML_MAX_OBJ), r;
        CHECK_EQ(call("OBUF", &a, 1, &r), 0);
        CHECK_EQ(r.type, AML_T_BUF);
        CHECK_EQ(r.len, AML_MAX_OBJ);
        CHECK_EQ(r.u.bytes[2], 3);
        CHECK_EQ(r.u.bytes[AML_MAX_OBJ - 1], 0);
        aml_val_free(&c, &r);
    }
    {
        aml_val_t a = VI(5), r;
        CHECK_EQ(call("OPKG", &a, 1, &r), 0);
        CHECK_EQ(r.type, AML_T_PKG);
        CHECK_EQ(r.len, 5);
        CHECK_EQ(aml_pkg_elem(&r, 2)->u.i, 3);
        CHECK_EQ(aml_pkg_elem(&r, 3)->type, AML_T_UNINIT);
        CHECK(aml_pkg_elem(&r, 4) != 0);
        CHECK(aml_pkg_elem(&r, 5) == 0);
        aml_val_free(&c, &r);
        a = VI(2);
        CHECK_EQ(call("OPKG", &a, 1, &r), 0);
        CHECK_EQ(r.len, 2);
        CHECK_EQ(aml_pkg_elem(&r, 1)->u.i, 2);
        aml_val_free(&c, &r);
        a = VI(AML_MAX_PKG);
        CHECK_EQ(call("OPKG", &a, 1, &r), 0);
        CHECK_EQ(r.len, AML_MAX_PKG);
        aml_val_free(&c, &r);
        a = VI(AML_MAX_PKG + 1);
        CHECK_EQ(call("OPKG", &a, 1, &r), AML_ENOMEM);
    }
}

static void test_stores(void)
{
    resized = 1;
    TI("SINT", 0, 0x1A, VS("1A"));
    TI("SINT", 0, 0x201, B(1, 2));
    TI("SINT", 0, 5, VI(5));
    TS("SSTR", m64 ? "0000000000000041" : "00000041", VI(0x41));
    TS("SSTR", "01 02", B(1, 2));
    TS("SSTR", "q", VS("q"));
    TB("SBUF", BYTES(2, 1, 0), VI(0x0102));
    TB("SBUF", BYTES('a', 'b', 'c'), VS("abcdef"));
    TB("SBUF", BYTES(7, 0, 0), B(7));
    if (m64)
        TB("SBF0", BYTES(2, 1, 0, 0, 0, 0, 0, 0), VI(0x0102));
    else
        TB("SBF0", BYTES(2, 1, 0, 0), VI(0x0102));
    TI("SPKG", 0, 5, VI(5));
    TI("SMTH", AML_ETYPE, 0, VI(1));
    /* a package stored into an integer name replaces it */
    {
        aml_val_t a = P(4, 5), r;
        CHECK_EQ(call("SINT", &a, 1, &r), 0);
        CHECK_EQ(r.type, AML_T_PKG);
        CHECK_EQ(r.len, 2);
        aml_val_free(&c, &r);
        CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "NINT")), AML_T_PKG);
    }
    resized = 0;
}

static void test_buffer_fields(void)
{
    static uint8_t z[16];
    TB0("BFCL", ARR(z));
    if (m64) {
        TB("BFW7", BYTES(0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0, 0, 0, 0, 0, 0, 0),
           VI(ONES));
        TB0("BFR7", BYTES(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0));
        T0("BFR6", 0, 0xFFFFFFFFFFFFFFFEull);
    } else {
        TB("BFW7", BYTES(0xF0, 0xFF, 0xFF, 0xFF, 0x0F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), VI(ONES));
        TB0("BFR7", BYTES(0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0));
        TB0("BFR6", BYTES(0xFE, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0));
    }
    TB0("BFCL", ARR(z));
    TB0("BFBT", BYTES(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80));
    TB("BFW6", BYTES(0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80), B(0x12));
    TB("BFW6", BYTES(0x08, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0, 0, 0, 0, 0, 0, 0, 0x80),
       B(0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xFF));
    TI("BFW6", AML_ETYPE, 0, P(1));
    T0("BFE1", AML_EBOUNDS, 0);
    T0("BFE2", AML_EBOUNDS, 0);
    TB0("BFE3", BYTES(0, 0, 0, 0x80));
    T0("BFE4", AML_EBOUNDS, 0);
    TB0("BFE5", BYTES(0xEF, 0xCD, 0xAB, 0x89));
    T0("BFE6", AML_EBOUNDS, 0);
    if (m64)
        TB0("BFE7", BYTES(8, 7, 6, 5, 4, 3, 2, 1));
    else
        TB0("BFE7", BYTES(8, 7, 6, 5, 0, 0, 0, 0));
    TB0("BFE8", BYTES(0, 0xAB));
    T0("BFE9", AML_EBOUNDS, 0);
    T0("BFEA", AML_ETYPE, 0);
    TB0("BFEB", BYTES(0x1F, 0x04, 0xF0, 0xFF));
    TB0("BFEC", BYTES(0xFF, 0x12, 0x00, 0xFF));
}

static void test_refs(void)
{
    uint64_t T = W(ONES, 0xFFFFFFFF);
    aml_val_t r;
    T0("REF1", 0, AML_T_INT);
    T0("REF2", 0, AML_T_STR);
    T0("REF3", 0, AML_T_BUFFIELD);
    T0("REF4", 0, 'z');
    {
        uint32_t h = heap();
        t_val(__LINE__, "REF5", 0, 0, AML_T_STR, "Ayz", 3, h);
    }
    TB0("REF6", BYTES(4, 5, 0xFF));
    T0("REF7", AML_EBOUNDS, 0);
    T0("REF8", AML_ETYPE, 0);
    T0("REF9", 0, 3);
    T0("REFA", 0, 8);
    if (m64)
        TB0("REFB", BYTES(7, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0));
    else
        TB0("REFB", BYTES(7, 0, 0, 0, 2, 0, 0, 0));
    if (m64)
        TB0("REFC", BYTES(7, 0, 0, 0, 0, 0, 0, 0));
    else
        TB0("REFC", BYTES(7, 0, 0, 0));
    T0("REFD", 0, AML_T_PKG);
    T0("REFE", 0, 9);
    T0("REFF", AML_ETYPE, 0);
    T0("REFG", 0, T);
    T0("REFH", 0, AML_T_STR);
    T0("REFI", 0, 1);
    T0("REFJ", 0, 5);
    T0("REFK", 0, 5);
    T0("REFO", 0, 5);
    T0("REFP", 0, 6);
    CHECK_EQ(aml_eval(&c, 0, "REFL", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_UNINIT);
    /* a reference survives a copy: same location, same index */
    uint32_t h = heap();
    CHECK_EQ(aml_eval(&c, 0, "REFM", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_REF);
    aml_val_t cp;
    CHECK_EQ(aml_val_copy(&c, &cp, &r), 0);
    CHECK_EQ(cp.type, AML_T_REF);
    CHECK(cp.u.ref != r.u.ref);
    CHECK_EQ(cp.u.ref->indexed, 1);
    CHECK_EQ(cp.u.ref->index, 1);
    CHECK(cp.u.ref->base.node == r.u.ref->base.node);
    uint64_t v = 0;
    CHECK_EQ(aml_val_to_int(&c, &cp, &v), 0);
    CHECK_EQ(v, 5);
    aml_val_free(&c, &cp);
    aml_val_free(&c, &r);
    /* one that owns its package */
    CHECK_EQ(aml_eval(&c, 0, "REFN", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_REF);
    CHECK_EQ(r.u.ref->base.kind, AML_LOC_OWNED);
    CHECK_EQ(aml_val_copy(&c, &cp, &r), 0);
    CHECK_EQ(cp.u.ref->owned.type, AML_T_PKG);
    CHECK(cp.u.ref->owned.u.elems != r.u.ref->owned.u.elems);
    aml_val_free(&c, &r);
    CHECK_EQ(aml_val_to_int(&c, &cp, &v), 0);
    CHECK_EQ(v, 2);
    aml_val_free(&c, &cp);
    CHECK_EQ(heap(), h);
    /* a package names a device by reference, an unknown name as a string */
    CHECK_EQ(aml_eval(&c, 0, "PKG2", 0, 0, &r), 0);
    CHECK_EQ(r.len, 3);
    CHECK_EQ(aml_pkg_elem(&r, 0)->type, AML_T_REF);
    CHECK(aml_pkg_elem(&r, 0)->u.ref->base.node == aml_lookup(&c, 0, "PDEV"));
    CHECK(val_is_str(aml_pkg_elem(&r, 1), "NOPE"));
    CHECK_EQ(aml_pkg_elem(&r, 2)->type, AML_T_INT);
    CHECK_EQ(aml_pkg_elem(&r, 2)->u.i, 7);
    aml_val_free(&c, &r);
    CHECK_EQ(heap(), h);
    T0("PKG3", 0, 0);
    T0("NAM1", 0, 0x5B);
    T0("NAM2", 0, AML_T_DEVICE);
    /* a reference to a caller's Local stays a reference; to the returning
     * frame's own Local or Arg it becomes the value */
    T0("RFA1", 0, 4);
    T0("RFA2", 0, 3);
    TI("RFA3", 0, 9, VI(9));
    T0("RDEP", AML_ETYPE, 0); /* one argument missing */
    TI("RDEP", 0, 23, VI(23));
    TI("RDEP", AML_ELIMIT, 0, VI(24));
    CHECK_EQ(c.last_error, AML_ELIMIT);
    CHECK(c.last_error_where && strcmp(c.last_error_where, "call depth") == 0);
    T0("CRF1", 0, 3);
    T0("CRF2", 0, 7);
    CHECK(!fk_log_has(&f, "NOPE"));
}

static void test_sync(void)
{
    uint64_t N = W(ONES, 0xFFFFFFFF);
    aml_node_t *mx = aml_lookup(&c, 0, "MX15"), *ev = aml_lookup(&c, 0, "EV00");
    T0("MXA1", 0, 0);
    CHECK_EQ(mx->x.mx.sync, 15);
    CHECK_EQ(mx->x.mx.depth, 0);
    T0("EVW1", 0, N);
    T0("EVS1", 0, N);
    CHECK_EQ(ev->x.ev.count, 0);
    /* Debug of each kind of value */
    fk_log_clear(&f);
    uint32_t dl = c.debug_lines;
    T0("DBG1", 0, 1);
    CHECK_EQ(c.debug_lines - dl, 3);
    CHECK(fk_log_has(&f, "[AML] Debug: buffer, bytes 3"));
    CHECK(fk_log_has(&f, "[AML] Debug: package, elements 2"));
    CHECK(fk_log_has(&f, "[AML] Debug: object of type 20"));
    T0("FTL1", AML_EFATAL, 0);
    CHECK(fk_log_has(&f, "[AML] Fatal opcode, code 0xDEADBEEF"));
    CHECK_EQ(c.last_error, AML_EFATAL);
    CHECK(strcmp(c.last_error_where, "Fatal") == 0);
}

static void test_notify(void)
{
    aml_notify_t n;
    aml_node_t *nd = aml_lookup(&c, 0, "NDEV");
    while (aml_notify_pop(&c, &n)) {
    }
    c.notify_dropped = 0;
    for (uint32_t i = 0; i < AML_MAX_NOTIFY; i++) {
        aml_val_t a = VI(i);
        CHECK_EQ(call("NTF1", &a, 1, 0), 0);
    }
    CHECK_EQ(c.notify_count, AML_MAX_NOTIFY);
    CHECK_EQ(c.notify_dropped, 0);
    aml_val_t a = VI(99);
    CHECK_EQ(call("NTF1", &a, 1, 0), 0);
    CHECK_EQ(c.notify_dropped, 1);
    for (uint32_t i = 0; i < 10; i++) {
        CHECK_EQ(aml_notify_pop(&c, &n), 1);
        CHECK_EQ(n.value, i);
        CHECK(n.node == nd);
    }
    for (uint32_t i = 0; i < 10; i++) {
        a = VI(100 + i);
        CHECK_EQ(call("NTF1", &a, 1, 0), 0);
    }
    CHECK_EQ(c.notify_dropped, 1);
    a = VI(0x100000005ull);
    CHECK_EQ(call("NTF1", &a, 1, 0), 0);
    CHECK_EQ(c.notify_dropped, 2);
    for (uint32_t i = 10; i < 42; i++) {
        CHECK_EQ(aml_notify_pop(&c, &n), 1);
        CHECK_EQ(n.value, i < 32 ? i : 100 + i - 32);
    }
    CHECK_EQ(aml_notify_pop(&c, &n), 0);
    CHECK_EQ(c.notify_count, 0);
    a = VI(0x100000005ull);
    CHECK_EQ(call("NTF1", &a, 1, 0), 0);
    CHECK_EQ(aml_notify_pop(&c, &n), 1);
    CHECK_EQ(n.value, 5);
    /* a notify for a method's temporary device is dropped once it is gone */
    fk_log_clear(&f);
    CHECK_EQ(aml_eval(&c, 0, "NTF3", 0, 0, 0), 0);
    CHECK(fk_log_has(&f, "[AML] Notify \\NTF3.TMPN 0x4"));
    CHECK(fk_log_has(&f, "[AML] Notify \\NDEV 0x5"));
    CHECK_EQ(c.notify_count, 2);
    CHECK_EQ(aml_notify_pop(&c, &n), 1);
    CHECK(n.node == nd);
    CHECK_EQ(n.value, 5);
    CHECK_EQ(c.notify_count, 0);
    CHECK_EQ(aml_notify_pop(&c, &n), 0);
    CHECK_EQ(aml_notify_pop(0, &n), 0);
}

static void test_budgets(void)
{
    uint64_t t0;
    TI("LOOP", 0, 0xFFFF, VI(0xFFFF));
    TI("LOOP", AML_ELIMIT, 0, VI(0x10000));
    CHECK(strcmp(c.last_error_where, "While iterations") == 0);
    TI("LOOP", 0, 0, VI(0));
    t0 = f.now_us;
    uint64_t ft = c.fake_time;
    TI("SLPU", 0, 1, VI(AML_MAX_SLEEP_US));
    CHECK_EQ(f.now_us - t0, AML_MAX_SLEEP_US);
    CHECK_EQ(c.fake_time - ft, AML_MAX_SLEEP_US * 10ull);
    CHECK_EQ(c.sleep_us, AML_MAX_SLEEP_US);
    t0 = f.now_us;
    TI("SLPU", AML_ELIMIT, 0, VI(AML_MAX_SLEEP_US + 1));
    CHECK_EQ(f.now_us, t0);
    CHECK(strcmp(c.last_error_where, "sleep budget") == 0);
    TI("SLPU", m64 ? AML_ELIMIT : 0, 1, VI(0x100000000ull + 5));
    TI("SLPM", 0, 1, VI(AML_MAX_SLEEP_US / 1000));
    TI("SLPM", AML_ELIMIT, 0, VI(AML_MAX_SLEEP_US / 1000 + 1));
    TI("SLPM", 0, 1, VI(0));
    /* the platform clock, and without one the interpreter's own */
    f.now_us = 1234;
    T0("TIMR", 0, 12340);
    f.plat.timer_100ns = 0;
    ft = c.fake_time;
    TI("SLPU", 0, 1, VI(7));
    T0("TIMR", 0, ft + 70);
    f.plat.delay_us = 0;
    t0 = f.now_us;
    TI("SLPU", 0, 1, VI(7));
    CHECK_EQ(f.now_us, t0);
    T0("TIMR", 0, ft + 140);
    /* a caller's op budget: exactly limit + 1 opcodes are counted */
    c.op_limit = 50;
    TI("LOOP", AML_ELIMIT, 0, VI(100));
    CHECK_EQ(c.ops, 51);
    CHECK(strcmp(c.last_error_where, "op budget") == 0);
    c.op_limit = 0;
    TI("LOOP", 0, 100, VI(100));
}

/* The size an arena block was asked for (its header keeps it). */
static uint32_t req_size(const void *p)
{
    uint32_t n;
    memcpy(&n, (const uint8_t *) p - 4, 4);
    return n;
}

/* Result of m (string or buffer) kept for a look at its block. */
static uint32_t str_req(const char *m, aml_val_t *a, uint32_t n, uint32_t *len)
{
    aml_val_t r;
    uint32_t q = 0;
    *len = 0;
    if (!call(m, a, n, &r) && (r.type == AML_T_STR || r.type == AML_T_BUF)) {
        q = req_size(r.u.bytes);
        *len = r.len;
    }
    aml_val_free(&c, &r);
    return q;
}

static void test_refs_more(void)
{
    uint32_t len;
    /* references whose object is gone */
    T0("SRT2", AML_ENOENT, 0);
    T0("SRL2", AML_ENOENT, 0);
    T0("SRL4", AML_ENOENT, 0);
    T0("SRL7", 0, 0x17);
    T0("SRT3", 0, 0);
    TI("IFE1", 0, 1, VI(1));
    TI("IFE1", 0, 2, VI(0));
    TI("IFE2", 0, 1, VI(1));
    TI("IFE2", AML_ENOENT, 0, VI(0));
    resized = 1;
    T0("OTX1", 0, AML_T_BUFFIELD);
    resized = 0;
    TI("SRA6", 0, 7, VI(1), VI(2), VI(3), VI(4), VI(5), VI(6), VI(7));
    /* chains of references: 9 deep is read, 10 is over the limit */
    T0("RCHS", AML_ETYPE, 0);
    resized = 1;
    TI("RCH", 0, 7, VI(8));
    TI("RCH", 0, 7, VI(9));
    TI("RCH", AML_ELIMIT, 0, VI(10));
    T0("RCHS", AML_ENOENT, 0); /* RCN1 still points into RCH's frame */
    /* indexed references into a string that shrank or stopped being one */
    T0("RSB1", AML_EBOUNDS, 0);
    T0("RSB2", AML_EBOUNDS, 0);
    T0("RSB3", AML_ETYPE, 0);
    T0("RSB4", AML_ETYPE, 0);
    T0("RSB5", AML_ETYPE, 0);
    {
        uint32_t h = heap();
        t_val(__LINE__, "RSB6", 0, 0, AML_T_STR, "aZc", 3, h);
    }
    resized = 0;
    /* objects read through a reference: a method runs, a device stays one */
    T0("RNM1", 0, 1);
    T0("RND1", 0, 6);
    if (m64) {
        T0("RNF1", 0, 0);
        TB("RNF2", BYTES(0x48, 0x3C, 0x2B, 0x1A, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
           VI(0x123456789ull));
    } else {
        TB0("RNF1", BYTES(0, 0, 0, 0, 0, 0, 0, 0));
        TB("RNF2", BYTES(0x48, 0x3C, 0x2B, 0x1A, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
           VI(0x123456789ull));
    }
    /* a 65-bit field is a 9-byte buffer; the bit after it is set */
    TB0("BFS1", BYTES(0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x70, 0x00));
    if (m64)
        T0("BFS2", 0, 0x80402010);
    else
        TB0("BFS2", BYTES(0x10, 0x20, 0x40, 0x80, 0x00));
    TB0("BFS3",
        BYTES(0xE1, 0, 0, 0, 0, 0x20, 0x40, 0x80, 0x13, 0x0C, 0x30, 0xC0, 0x11, 0x22, 0x44, 0x88));
    resized = 1;
    T0("BFS4", AML_EBOUNDS, 0);
    T0("BFS5", AML_EBOUNDS, 0);
    T0("BFS6", AML_ETYPE, 0);
    T0("BFS7", AML_ETYPE, 0);
    TB0("BFS8", BYTES(0, 0, 1));
    resized = 0;
    /* names in a package: data read, others referenced, unknown ones as text;
     * a buffer field is read in a method but not while the table loads */
    {
        static const uint8_t ty64[] = {AML_T_INT, AML_T_STR, AML_T_BUF, AML_T_PKG,
                                       AML_T_INT, AML_T_REF, AML_T_REF, AML_T_STR};
        aml_val_t r;
        uint32_t h = heap();
        CHECK_EQ(call("PKT1", 0, 0, &r), 0);
        CHECK_EQ(r.len, 8);
        for (uint32_t i = 0; i < 8 && i < r.len; i++)
            CHECK_EQ(aml_pkg_elem(&r, i)->type, (i == 4 && !m64 ? AML_T_BUF : ty64[i]));
        aml_val_free(&c, &r);
        CHECK_EQ(heap(), h);
        CHECK_EQ(call("PKLD", 0, 0, &r), 0);
        CHECK_EQ(r.len, 2);
        CHECK_EQ(aml_pkg_elem(&r, 0)->type, AML_T_REF);
        CHECK_EQ(aml_pkg_elem(&r, 1)->type, AML_T_INT);
        aml_val_free(&c, &r);
    }
    /* Debug output */
    fk_log_clear(&f);
    T0("DBG2", 0, 0);
    CHECK_EQ(f.nlog, 3);
    CHECK(strcmp(f.log[0], "[AML] Debug: 0x1F") == 0);
    CHECK(strcmp(f.log[1], "[AML] Debug: hi") == 0);
    CHECK(strcmp(f.log[2], "[AML] Debug: 0xC") == 0);

    /* the text forms of a buffer are sized for the widest byte */
    {
        aml_val_t a[] = {B(1, 2, 3, 4, 5, 6, 7, 8)};
        CHECK_EQ(str_req("OTHS", a, 1, &len), 8 * 5 + 1);
        CHECK_EQ(len, 39);
    }
    {
        aml_val_t a[] = {B(1, 2, 3, 4, 5, 6, 7, 8)};
        CHECK_EQ(str_req("OTDS", a, 1, &len), 8 * 4 + 1);
        CHECK_EQ(len, 15);
    }
    {
        aml_val_t a[] = {B(1, 2, 3, 4, 5, 6)};
        CHECK_EQ(str_req("OTDS", a, 1, &len), 6 * 4 + 1);
        CHECK_EQ(len, 11);
    }
    TS("OTDS", "99,100,9,10,255", B(99, 100, 9, 10, 255));
    TI("OLEQ", 0, W(ONES, 0xFFFFFFFF), VS("01 02"), B(1, 2));
    TI("OLEQ", 0, 0, VS("0x01,0x02"), B(1, 2));
    resized = 1;
    {
        aml_val_t a[] = {B(1, 2, 3, 4, 5, 6)}, r;
        CHECK_EQ(call("SSTR", a, 1, &r), 0);
        aml_val_free(&c, &r);
        aml_node_t *n = aml_lookup(&c, 0, "NSTR");
        CHECK_EQ(req_size(n->v.u.bytes), 6 * 3 + 1);
        CHECK_EQ(n->v.len, 17);
    }
    /* a buffer too long for its text form, at the limit and one past it */
    {
        aml_val_t a[1], r;
        uint32_t big = AML_MAX_OBJ / 5;
        CHECK_EQ(aml_mkbuf(&c, &a[0], big + 1), 0);
        CHECK_EQ(call("OTDS", a, 1, &r), AML_ENOMEM);
        CHECK_EQ(aml_mkbuf(&c, &a[0], big + 1), 0);
        CHECK_EQ(call("SSTR", a, 1, &r), AML_ENOMEM);
        CHECK(strcmp((const char *) aml_lookup(&c, 0, "NSTR")->v.u.bytes, "01 02 03 04 05 06") ==
              0);
        CHECK_EQ(aml_mkbuf(&c, &a[0], big), 0);
        CHECK_EQ(call("OTDS", a, 1, &r), 0);
        CHECK_EQ(r.len, big * 2 - 1);
        aml_val_free(&c, &r);
    }
    resized = 0;
}

static void run_table(int wide)
{
    CHECK_EQ(load(wide), 0);
    CHECK_EQ(c.int64, wide);
    CHECK_EQ(c.dsdt_rev, wide ? 2 : 1);
    test_arith();
    test_logic();
    test_strings();
    test_packages();
    test_stores();
    test_buffer_fields();
    test_refs();
    test_sync();
    test_notify();
    test_budgets();
    test_refs_more();
}

/* ---- heap and value helpers, called directly ------------------------------------ */
static void test_heap(void)
{
    static uint8_t small[0x10000] __attribute__((aligned(16)));
    fk_init(&f);
    /* the arena is aligned up to 16 bytes and its size shrinks to match */
    CHECK_EQ(aml_init(&c, &f.plat, g_arena + 1, ARENA_SIZE - 1), 0);
    CHECK(c.arena == g_arena + 16);
    CHECK_EQ(c.arena_size, ARENA_SIZE - 16);
    CHECK_EQ(aml_init(&c, &f.plat, small, 0xFFFF), AML_EARG);
    CHECK_EQ(aml_init(&c, &f.plat, 0, ARENA_SIZE), AML_EARG);
    CHECK_EQ(aml_init(&c, &f.plat, small, sizeof small), 0);
    aml_stats_t s = aml_stats(&c);
    CHECK_EQ(s.objects, 0);
    CHECK_EQ(s.methods, 0);
    CHECK_EQ(s.devices, 0);
    CHECK_EQ(s.heap_size, sizeof small);
    CHECK_EQ(s.heap_used, c.arena_top);
    CHECK_EQ(s.heap_peak, c.arena_top);

    /* size classes 16, 24, 32, 48, 64 (8 bytes of header each) */
    static const uint32_t req[] = {0, 8, 9, 16, 17, 24, 25, 40, 41, 56, 57};
    static const uint32_t cls[] = {16, 16, 24, 24, 32, 32, 48, 48, 64, 64, 96};
    void *p[11];
    for (int i = 0; i < 11; i++) {
        uint32_t h = heap(), top = c.arena_top;
        p[i] = aml_alloc(&c, req[i]);
        CHECK(p[i] != 0);
        CHECK_EQ(heap() - h, cls[i]);
        CHECK_EQ(c.arena_top - top, cls[i]);
        CHECK((uint8_t *) p[i] == c.arena + top + 8);
    }
    /* a freed block is reused, zeroed, by the next request of its class */
    uint32_t h = heap(), peak = c.heap_peak;
    memset(p[3], 0xAA, 16);
    aml_free(&c, p[3]);
    CHECK_EQ(h - heap(), 24);
    CHECK_EQ(c.heap_peak, peak);
    aml_free(&c, p[3]); /* double free: ignored */
    CHECK_EQ(h - heap(), 24);
    uint32_t top = c.arena_top;
    void *q = aml_alloc(&c, 12);
    CHECK(q == p[3]);
    CHECK_EQ(c.arena_top, top);
    CHECK_EQ(((uint8_t *) q)[0], 0);
    CHECK_EQ(((uint8_t *) q)[15], 0);
    CHECK_EQ(heap(), h);
    /* two frees of one class come back last in, first out */
    aml_free(&c, p[1]);
    aml_free(&c, p[0]);
    CHECK(aml_alloc(&c, 1) == p[0]);
    CHECK(aml_alloc(&c, 1) == p[1]);
    CHECK_EQ(heap(), h);
    /* pointers that are not live blocks are ignored */
    uint8_t stack[32];
    aml_free(&c, stack + 8);
    aml_free(&c, 0);
    aml_free(&c, (uint8_t *) p[5] + 8); /* inside a block: no header there */
    CHECK_EQ(heap(), h);
    uint32_t live = 0xA7000000u, bad = 0xA7000000u | 45u;
    memcpy(c.arena + c.arena_top, &live, 4); /* a stale tag past the top */
    aml_free(&c, c.arena + c.arena_top + 8);
    CHECK_EQ(heap(), h);
    memcpy((uint8_t *) p[4] - 8, &bad, 4); /* a class number out of range */
    aml_free(&c, p[4]);
    CHECK_EQ(heap(), h);
    memcpy(g_arena, &live, 4); /* below the arena */
    aml_init(&c, &f.plat, g_arena + 8, ARENA_SIZE - 8);
    h = heap();
    aml_free(&c, g_arena + 8);
    CHECK_EQ(heap(), h);
    /* the first block of the arena is a block like any other */
    aml_free(&c, c.root);
    CHECK(h > heap());

    /* exhaustion: an exact fit still succeeds */
    CHECK_EQ(aml_init(&c, &f.plat, small, sizeof small), 0);
    if ((c.arena_size - c.arena_top) % 16) aml_alloc(&c, 16);
    CHECK_EQ((c.arena_size - c.arena_top) % 16, 0);
    uint32_t n = 0, room = c.arena_size - c.arena_top;
    while (aml_alloc(&c, 8)) n++;
    CHECK_EQ(n, room / 16);
    CHECK_EQ(c.arena_top, c.arena_size);
    CHECK(aml_alloc(&c, 1) == 0);
    CHECK_EQ(c.heap_peak, c.heap_used);
    CHECK(aml_alloc(&c, 0x400001) == 0);
    CHECK_EQ(c.heap_peak, c.heap_used);
}

static aml_val_t nest(uint32_t depth)
{
    aml_val_t v = aml_int(depth);
    for (uint32_t i = 0; i < depth; i++) {
        aml_val_t p;
        memset(&p, 0, sizeof p);
        p.type = AML_T_PKG;
        p.len = 1;
        p.u.elems = (aml_val_t *) aml_alloc(&c, (uint32_t) sizeof(aml_val_t));
        p.u.elems[0] = v;
        v = p;
    }
    return v;
}

static void test_values(void)
{
    static char big[AML_MAX_OBJ + 2];
    aml_val_t v, w;
    uint64_t x;
    CHECK_EQ(setup(&f, &c, 0, 0), 0);
    uint32_t h = heap();
    CHECK_EQ(aml_mkbuf(&c, &v, 0), 0);
    CHECK_EQ(v.type, AML_T_BUF);
    CHECK_EQ(v.len, 0);
    CHECK_EQ(heap() - h, 16);
    aml_val_free(&c, &v);
    CHECK_EQ(v.type, 0);
    CHECK(v.u.bytes == 0);
    CHECK_EQ(aml_mkbuf(&c, &v, AML_MAX_OBJ), 0);
    CHECK_EQ(v.len, AML_MAX_OBJ);
    aml_val_free(&c, &v);
    CHECK_EQ(aml_mkbuf(&c, &v, AML_MAX_OBJ + 1), AML_ENOMEM);
    CHECK_EQ(v.type, 0);
    CHECK_EQ(aml_mkstr(&c, &v, AML_MAX_OBJ), 0);
    CHECK_EQ(v.type, AML_T_STR);
    CHECK_EQ(v.len, AML_MAX_OBJ);
    aml_val_free(&c, &v);
    CHECK_EQ(aml_mkstr(&c, &v, AML_MAX_OBJ + 1), AML_ENOMEM);
    CHECK_EQ(v.type, 0);
    CHECK_EQ(aml_mkstr(&c, &v, 0), 0);
    CHECK_EQ(heap() - h, 16);
    aml_val_free(&c, &v);
    v = aml_str(&c, 0);
    CHECK(val_is_str(&v, ""));
    aml_val_free(&c, &v);
    memset(big, 'a', sizeof big - 1);
    v = aml_str(&c, big);
    CHECK_EQ(v.type, AML_T_STR);
    CHECK_EQ(v.len, AML_MAX_OBJ);
    aml_val_free(&c, &v);
    v = aml_buf(&c, 0, 3);
    CHECK(val_is_buf(&v, (const uint8_t *) "\0\0\0", 3));
    aml_val_free(&c, &v);
    v = aml_buf(&c, big, AML_MAX_OBJ + 1);
    CHECK_EQ(v.type, 0);
    CHECK_EQ(heap(), h);
    aml_val_free(&c, 0);

    /* copies nest at most 48 deep */
    v = nest(48);
    CHECK_EQ(aml_val_copy(&c, &w, &v), 0);
    CHECK_EQ(w.type, AML_T_PKG);
    aml_val_free(&c, &w);
    aml_val_free(&c, &v);
    CHECK_EQ(heap(), h);
    v = nest(49);
    CHECK_EQ(aml_val_copy(&c, &w, &v), AML_ELIMIT);
    CHECK_EQ(w.type, 0);
    aml_val_free(&c, &v);
    CHECK_EQ(heap(), h);
    v = aml_int(5);
    CHECK_EQ(aml_val_copy(&c, &w, &v), 0);
    CHECK_EQ(w.u.i, 5);
    CHECK(aml_pkg_elem(0, 0) == 0);

    /* integer conversion of strings: hex digits, whitespace, 0x, 16 digits */
    static const struct {
        const char *s;
        uint64_t want;
    } hs[] = {
        {"0", 0},
        {"9", 9},
        {"10", 0x10},
        {"19", 0x19},
        {"1A", 0x1A},
        {"1F", 0x1F},
        {"1a", 0x1A},
        {"1f", 0x1F},
        {"1/", 1},
        {"1:", 1},
        {"1@", 1},
        {"1G", 1},
        {"1`", 1},
        {"1g", 1},
        {"  \t0Xff", 0xFF},
        {"0x", 0},
        {"", 0},
        {"   ", 0},
        {"x1", 0},
        {"05", 5},
        {"0X5", 5},
        {" 0x 5", 0},
        {"123456789ABCDEF01", 0x123456789ABCDEF0ull},
        {"FFFFFFFFFFFFFFFF", ONES},
    };
    for (uint32_t i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        v = aml_str(&c, hs[i].s);
        x = 0xDEAD;
        CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
        if (x != hs[i].want) printf("  (string \"%s\")\n", hs[i].s);
        CHECK_EQ(x, hs[i].want);
        aml_val_free(&c, &v);
    }
    v = aml_buf(&c, "\x01\x02\x03\x04\x05\x06\x07\x08\x09", 9);
    CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
    CHECK_EQ(x, 0x0807060504030201ull);
    c.int64 = 0;
    CHECK_EQ(aml_ones(&c), 0xFFFFFFFFull);
    CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
    CHECK_EQ(x, 0x04030201);
    aml_val_free(&c, &v);
    v = aml_str(&c, "123456789");
    CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
    CHECK_EQ(x, 0x23456789);
    aml_val_free(&c, &v);
    v = aml_int(0x1FFFFFFFFull);
    CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
    CHECK_EQ(x, 0xFFFFFFFF);
    c.int64 = 1;
    CHECK_EQ(aml_ones(&c), ONES);
    v = aml_buf(&c, "\x01\x02", 2);
    CHECK_EQ(aml_val_to_int(&c, &v, &x), 0);
    CHECK_EQ(x, 0x0201);
    aml_val_free(&c, &v);
    CHECK_EQ(aml_val_to_int(0, &v, &x), AML_EARG);
    CHECK_EQ(aml_val_to_int(&c, &v, 0), AML_EARG);
    v = nest(1);
    x = 77;
    CHECK_EQ(aml_val_to_int(&c, &v, &x), AML_ETYPE);
    CHECK_EQ(x, 77);
    CHECK_EQ(aml_to_buf(&c, &v, &w), AML_ETYPE);
    CHECK_EQ(w.type, 0);
    aml_val_free(&c, &v);
    CHECK_EQ(heap(), h);
}

/* ---- hand-assembled AML ------------------------------------------------------------- */
static uint8_t hb[0x2000];
static uint32_t hk;

static void emit(const uint8_t *b, uint32_t n)
{
    memcpy(hb + hk, b, n);
    hk += n;
}
#define E(...) emit((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static uint32_t pkglen(uint8_t *o, uint32_t body)
{
    uint32_t n = body + 1;
    if (n < 0x40) {
        o[0] = (uint8_t) n;
        return 1;
    }
    n = body + 2;
    o[0] = (uint8_t) (0x40 | (n & 0xF));
    o[1] = (uint8_t) (n >> 4);
    return 2;
}

static void emit_method(const char *name, uint8_t flags, const uint8_t *body, uint32_t blen)
{
    hb[hk++] = 0x14;
    hk += pkglen(hb + hk, 5 + blen);
    memcpy(hb + hk, name, 4);
    hk += 4;
    hb[hk++] = flags;
    emit(body, blen);
}
#define M(name, flags, ...)                                                                        \
    emit_method(name, flags, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static int load_hand(uint8_t rev)
{
    uint32_t n = fk_table(tbl, sizeof tbl, "DSDT", rev, hb, hk);
    fk_init(&f);
    if (aml_init(&c, &f.plat, g_arena, ARENA_SIZE)) return -100;
    m64 = rev >= 2;
    return aml_load_table(&c, tbl, n);
}

#define SDAT 'S', 'D', 'A', 'T'
#define NOPE 'N', 'O', 'P', 'E'

static void cut_methods(void);

static void test_hand_methods(void)
{
    uint64_t v;
    hk = 0;
    E(0x08, SDAT, 0x0A, 0x2A);                                             /* Name (SDAT, 0x2A) */
    E(0x5B, 0x80, 'R', 'G', 'N', '0', 0x00, 0x0B, 0x00, 0x10, 0x0A, 0x10); /* region */
    M("H001", 0, 0xA4, 0x12, 0x09, 0x02, 0x01, 0x0A, 0x02, 0x0A, 0x03, 0x0A, 0x04);
    M("H002", 0, 0xA4, 0x89, 0x12, 0x03, 0x01, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00);
    M("H003", 0, 0xA4, 0x89, 0x12, 0x03, 0x01, 0x01, 0x00, 0x01, 0x06, 0x00, 0x00);
    M("H004", 0, 0x10, 0x05, SDAT);
    M("H005", 0, 0x10, 0x05, NOPE);
    M("H006", 0, 0x5B, 0x2A, SDAT, 0xA4, 0x01);
    M("H007", 0, 0xA4, 0x5B, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
    M("H008", 0, 0x5B, 0x23, SDAT, 0xFF, 0xFF);
    M("H009", 0, 0x5B, 0x24, SDAT);
    M("H010", 0, 0xA4, 0x5B, 0x25, SDAT, 0x00);
    M("H011", 0, 0x86, SDAT, 0x01);
    M("H012", 0, 0xA4, 0x5B, 0x99);
    M("H013", 0, 0xA4, 0x02);
    M("H014", 0, 0xA4, 0x2F, 0x00);
    M("H017", 0, 0xA5);
    M("H018", 0, 0x9F);
    M("H019", 0, 0xA3, 0xCC, 0xA4, 0x0A, 0x07);
    M("H020", 0, 0xA1, 0x03, 0xA4, 0x01, 0xA4, 0x0A, 0x02);
    M("H021", 0, 0xA2, 0x05, 0x01, 0xA4, 0x0A, 0x05);
    M("H022", 0, 0xA4, 0x0D, 'a', 'b');
    M("H023", 0, 0x70, 0x0A, 0x05, 0x71, SDAT, 0xA4, SDAT);
    M("H024", 0, 0xA4, 0x5B, 0x12, SDAT, 0x00);
    M("H025", 0, 0xA4, 0x5B, 0x12, NOPE, 0x00);
    M("H026", 0, 0x70, 0x01, 0x83, 0x0A, 0x05);
    M("H027", 0, 0x70, 0x01, 0x83, 0x0D, NOPE, 0x00);
    M("H028", 0, 0x70, 0x01, 0x5B, 0x99);
    M("H029", 0, 0x70, 0x01, 0x0A, 0x05);
    M("H030", 0, 0x70, 0x01, 0x71, 0x0A, 0x05);
    /* CreateByteField (Buffer (4) {}, 0x2000000000000000, BFX0): the byte
     * index times 8 wraps to 0 */
    M("H031", 0, 0x70, 0x11, 0x03, 0x0A, 0x04, 0x60, 0x8C, 0x60, 0x0E, 0, 0, 0, 0, 0, 0, 0, 0x20,
      'B', 'F', 'X', '0', 0xA4, 0x01);
    M("H032", 0, 0x70, 0x11, 0x03, 0x0A, 0x04, 0x60, 0x5B, 0x13, 0x60, 0x0E, 0, 0, 0, 0, 1, 0, 0, 0,
      0x01, 'B', 'F', 'X', '1', 0xA4, 0x01);
    /* DataTableRegion: signature, OEM id and table id prefixes must match */
    M("H033", 0, 0x5B, 0x88, 'D', 'T', 'R', '0', 0x0D, 'D', 'S', 'D', 'T', 0, 0x0D, 'Z', 'X', 'V',
      0, 0x0D, 'A', 'M', 'L', 'T', 'E', 'S', 'T', 0, 0xA4, 0x8E, 'D', 'T', 'R', '0');
    M("H034", 0, 0x5B, 0x88, 'D', 'T', 'R', '1', 0x0D, 'D', 'S', 'D', 'X', 0, 0x0D, 0, 0x0D, 0,
      0xA4, 0x01);
    M("H035", 0, 0x5B, 0x88, 'D', 'T', 'R', '2', 0x0D, 'D', 'S', 'D', 'T', 0, 0x0D, 'Z', 'X', 'W',
      0, 0x0D, 0, 0xA4, 0x01);
    M("H036", 0, 0x5B, 0x88, 'D', 'T', 'R', '3', 0x0D, 'D', 'S', 'D', 'T', 0, 0x0D, 0, 0x0D, 'A',
      'M', 'L', 'T', 'E', 'X', 0, 0xA4, 0x01);
    M("H037", 0, 0x5B, 0x88, 'D', 'T', 'R', '4', 0x0D, 'D', 'S', 'D', 0, 0x0D, 0, 0x0D, 0, 0xA4,
      0x01);
    M("H038", 0, 0x5B, 0x88, 'D', 'T', 'R', '5', 0x0A, 0x01, 0x0D, 0, 0x0D, 0, 0xA4, 0x01);
    /* a Field in a method may not reuse a name */
    M("H039", 0, 0x5B, 0x81, 0x10, 'R', 'G', 'N', '0', 0x01, 'F', 'D', 'U', 'P', 0x08, 'F', 'D',
      'U', 'P', 0x08, 0xA4, 0x01);
    M("H040", 0, 0x5B, 0x81, 0x0B, 'R', 'G', 'N', '0', 0x06, 'F', 'X', 'X', 'X', 0x08, 0xA4, 0x01);
    M("H041", 0, 0x5B, 0x81, 0x09, 'R', 'G', 'N', '0', 0x01, 0x01, 0x06, 0x00, 0xA4, 0x01);
    M("H042", 0, 0x5B, 0x81, 0x0B, 'S', 'D', 'A', 'T', 0x01, 'F', 'X', 'X', 'X', 0x08, 0xA4, 0x01);
    M("H043", 0, 0x5B, 0x81, 0x0B, 'N', 'O', 'P', 'E', 0x01, 'F', 'X', 'X', 'X', 0x08, 0xA4, 0x01);
    /* Return (Mutex created in the method) */
    M("H044", 0, 0x5B, 0x01, 'M', 'X', '0', '1', 0x17, 0xA4, 0x8E, 'M', 'X', '0', '1');
    M("H046", 0, 0x5B, 0x88, 'D', 'T', 'R', '6', 0x0D, 'D', 'S', 'D', 'T', 0, 0x0D, 'Z', 'X', 'V',
      'F', 'K', ' ', 'Q', 0, 0x0D, 'A', 'M', 'L', 'T', 'E', 'S', 'T', ' ', 'Q', 0, 0xA4, 0x8E, 'D',
      'T', 'R', '6');
    M("H045", 0, 0x5B, 0x02, 'E', 'V', '0', '1', 0xA4, 0x8E, 'E', 'V', '0', '1');
    /* Load (TBUF, Local0) of a table held in a buffer; Load of a non-buffer */
    {
        static uint8_t ssdt[64];
        static const uint8_t body[] = {0x08, 'L', 'D', 'V', '0', 0x0A, 0x77};
        uint32_t n = fk_table(ssdt, sizeof ssdt, "SSDT", 2, body, sizeof body);
        E(0x08, 'T', 'B', 'U', 'F', 0x11);
        hk += pkglen(hb + hk, 2 + n);
        E(0x0A, (uint8_t) n);
        emit(ssdt, n);
        static uint8_t ssd2[64];
        static const uint8_t bod2[] = {0x08, 'L', 'D', 'V', '1', 0x0A, 0x78};
        uint32_t n2 = fk_table(ssd2, sizeof ssd2, "SSDT", 2, bod2, sizeof bod2);
        E(0x08, 'T', 'B', 'F', '2', 0x11);
        hk += pkglen(hb + hk, 2 + n2);
        E(0x0A, (uint8_t) n2);
        emit(ssd2, n2);
        E(0x08, 'T', 'I', 'N', 'T', 0x0A, 0x05);
        M("H047", 0, 0x5B, 0x20, 'T', 'B', 'U', 'F', 0x60, 0xA4, 0x60);
        M("H048", 0, 0x5B, 0x20, 'T', 'B', 'U', 'F', 0x61, 0xA4, 0x7D, 0x61, 0x5B, 0x20, 'T', 'B',
          'U', 'F', 0x00, 0x00);
        M("H049", 0, 0x5B, 0x20, 'T', 'I', 'N', 'T', 0x60);
        M("H050", 0, 0x5B, 0x20, 'N', 'O', 'P', 'E', 0x60);
        M("H051", 0, 0xA4, 0x5B, 0x20, 'T', 'B', 'F', '2', 0x00);
    }
    /* 255 and 256 parent prefixes */
    {
        static uint8_t up[300];
        uint32_t k = 0;
        up[k++] = 0xA4;
        for (int i = 0; i < 255; i++) up[k++] = '^';
        memcpy(up + k, "SDAT", 4);
        emit_method("H015", 0, up, k + 4);
        memmove(up + 2, up + 1, k + 3);
        up[1] = '^';
        emit_method("H016", 0, up, k + 5);
    }
    CHECK_EQ(load_hand(2), 0);
    aml_val_t r;
    CHECK_EQ(aml_eval(&c, 0, "H001", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_PKG);
    CHECK_EQ(r.len, 2);
    CHECK_EQ(aml_pkg_elem(&r, 0)->u.i, 1);
    CHECK_EQ(aml_pkg_elem(&r, 1)->u.i, 2);
    aml_val_free(&c, &r);
    CHECK_EQ(ev_int(&c, "H002", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H003", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H004", &v), AML_ETYPE);
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "H005", &v), AML_ENOENT);
    CHECK(fk_log_has(&f, "[AML] Scope target not found: NOPE"));
    CHECK_EQ(ev_int(&c, "H006", &v), 0);
    CHECK(fk_log_has(&f, "[AML] Unload ignored"));
    CHECK_EQ(ev_int(&c, "H007", &v), 0);
    CHECK_EQ(v, 0);
    CHECK(fk_log_has(&f, "[AML] LoadTable not supported; returning Zero"));
    CHECK_EQ(ev_int(&c, "H008", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H009", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H010", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H011", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H012", &v), AML_EPARSE);
    CHECK(strcmp(c.last_error_where, "ext opcode") == 0);
    CHECK_EQ(ev_int(&c, "H013", &v), AML_EPARSE);
    CHECK(strcmp(c.last_error_where, "opcode") == 0);
    CHECK_EQ(ev_int(&c, "H014", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H015", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H016", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H017", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H018", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H019", &v), 0);
    CHECK_EQ(v, 7);
    CHECK_EQ(ev_int(&c, "H020", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(ev_int(&c, "H021", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(ev_int(&c, "H022", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H023", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(ev_int(&c, "H024", &v), 0);
    CHECK_EQ(v, ONES);
    CHECK_EQ(ev_int(&c, "H025", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(ev_int(&c, "H026", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H027", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H028", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H029", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H030", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H031", &v), AML_EBOUNDS);
    CHECK_EQ(ev_int(&c, "H032", &v), AML_EBOUNDS);
    CHECK_EQ(ev_int(&c, "H033", &v), 0);
    CHECK_EQ(v, AML_T_REGION);
    CHECK_EQ(ev_int(&c, "H034", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H035", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H036", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H037", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H038", &v), AML_ENOENT);
    CHECK_EQ(aml_stats(&c).regions, 2);
    CHECK_EQ(ev_int(&c, "H046", &v), 0);
    CHECK_EQ(v, AML_T_REGION);
    CHECK_EQ(ev_int(&c, "H039", &v), AML_EEXIST);
    CHECK_EQ(ev_int(&c, "H040", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H041", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "H042", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H043", &v), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "H044", &v), 0);
    CHECK_EQ(v, AML_T_MUTEX);
    CHECK_EQ(ev_int(&c, "H045", &v), 0);
    CHECK_EQ(v, AML_T_EVENT);
    CHECK(aml_lookup(&c, 0, "\\H044.MX01") == 0);
    CHECK(aml_lookup(&c, 0, "LDV0") == 0);
    CHECK_EQ(ev_int(&c, "H047", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(ev_int(&c, "LDV0", &v), 0);
    CHECK_EQ(v, 0x77);
    CHECK_EQ(ev_int(&c, "H048", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(c.n_tables, 2);
    CHECK_EQ(ev_int(&c, "H049", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "H050", &v), AML_ENOENT);
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "H051", &v), 0);
    CHECK_EQ(v, ONES);
    CHECK_EQ(c.n_tables, 3);
    CHECK(fk_log_has(&f, "[AML] Load: dynamic table ok 43"));
    CHECK_EQ(ev_int(&c, "LDV1", &v), 0);
    CHECK_EQ(v, 0x78);
    CHECK_EQ(c.depth, 0);
    CHECK_EQ(c.nest, 0);
    cut_methods();
}

/* Table-level code: the declaration pass, skipped terms, fields. */
static void test_hand_tables(void)
{
    uint64_t v;
    hk = 0;
    /* Store (\CPUX.MTHX (), FWDP) etc. before the containers are defined */
    E(0x08, 'F', 'W', 'D', 'P', 0x00, 0x08, 'F', 'W', 'D', 'Q', 0x00, 0x08, 'F', 'W', 'D', 'T',
      0x00);
    E(0x70, '\\', 0x2E, 'C', 'P', 'U', 'X', 'M', 'T', 'H', 'X', 'F', 'W', 'D', 'P');
    E(0x70, '\\', 0x2E, 'P', 'W', 'R', 'X', 'M', 'T', 'H', 'Y', 'F', 'W', 'D', 'Q');
    E(0x70, '\\', 0x2E, 'T', 'Z', 'X', '_', 'M', 'T', 'H', 'Z', 'F', 'W', 'D', 'T');
    E(0x5B, 0x83, 0x15, 'C', 'P', 'U', 'X', 0x01, 0x10, 0x00, 0x00, 0x00, 0x06, 0x14, 0x09, 'M',
      'T', 'H', 'X', 0x00, 0xA4, 0x0A, 0x33);
    E(0x5B, 0x84, 0x12, 'P', 'W', 'R', 'X', 0x03, 0x07, 0x02, 0x14, 0x09, 'M', 'T', 'H', 'Y', 0x00,
      0xA4, 0x0A, 0x44);
    E(0x5B, 0x85, 0x0F, 'T', 'Z', 'X', '_', 0x14, 0x09, 'M', 'T', 'H', 'Z', 0x00, 0xA4, 0x0A, 0x55);
    /* External (EXTM, MethodObj, 2) lets a failing call be stepped over */
    E(0x08, 'Q', 'Q', 'Q', 'Q', 0x00);
    E(0x15, 'E', 'X', 'T', 'M', 0x08, 0x02);
    E(0x70, 'E', 'X', 'T', 'M', 0x0A, 0x01, 0x0A, 0x02, 'Q', 'Q', 'Q', 'Q');
    E(0x08, 'A', 'F', 'T', 'R', 0x0A, 0x09);
    /* a region and a field list with every kind of element; FLDA twice */
    E(0x5B, 0x80, 'R', 'G', 'N', '0', 0x00, 0x0B, 0x00, 0x10, 0x0A, 0x10);
    {
        static const uint8_t fld[] = {'R',  'G',  'N',  '0',  0x01, 'F',  'L',  'D',  'A',  0x08,
                                      'F',  'L',  'D',  'A',  0x04, 0x00, 0x04, 0x01, 0x02, 0x00,
                                      'F',  'L',  'D',  'C',  0x08, 0x03, 0x03, 0x0B, 0x05, 'F',
                                      'L',  'D',  'D',  0x08, 0x02, 'Q',  'Q',  'Q',  'Q',  0x02,
                                      0x11, 0x03, 0x0A, 0x00, 'F',  'L',  'D',  'E',  0x48, 0x01};
        E(0x5B, 0x81);
        hk += pkglen(hb + hk, sizeof fld);
        emit(fld, sizeof fld);
    }
    CHECK_EQ(load_hand(2), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "FWDP", &v), 0);
    CHECK_EQ(v, 0x33);
    CHECK_EQ(ev_int(&c, "FWDQ", &v), 0);
    CHECK_EQ(v, 0x44);
    CHECK_EQ(ev_int(&c, "FWDT", &v), 0);
    CHECK_EQ(v, 0x55);
    aml_node_t *n = aml_lookup(&c, 0, "CPUX");
    CHECK_EQ(n->x.cpu.id, 1);
    CHECK_EQ(n->x.cpu.pblk, 0x10);
    CHECK_EQ(n->x.cpu.plen, 6);
    n = aml_lookup(&c, 0, "PWRX");
    CHECK_EQ(n->x.pr.level, 3);
    CHECK_EQ(n->x.pr.order, 0x207); /* 0x02: not a term, so the 3 bytes must be skipped */
    CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "TZX_")), AML_T_THERMAL);
    CHECK_EQ(ev_int(&c, "AFTR", &v), 0);
    CHECK_EQ(v, 9);
    CHECK_EQ(ev_int(&c, "QQQQ", &v), 0);
    CHECK_EQ(v, 0);
    CHECK(fk_log_has(&f, "duplicate field name skipped"));
    CHECK_EQ(aml_stats(&c).errors, 2);
    CHECK_EQ(aml_stats(&c).fields, 4);
    CHECK_EQ(aml_stats(&c).regions, 1);
    static const struct {
        const char *name;
        uint32_t bitoff, bitlen;
        uint8_t flags, attrib, acc_len;
    } fl[] = {
        {"FLDA", 0, 8, 0x01, 0, 0},
        {"FLDC", 16, 8, 0x02, 0, 0},
        {"FLDD", 24, 8, 0x03, 0x0B, 5},
        {"FLDE", 32, 0x18, 0x03, 0x0B, 5},
    };
    for (int i = 0; i < 4; i++) {
        n = aml_lookup(&c, 0, fl[i].name);
        CHECK(n != 0);
        if (!n) continue;
        CHECK_EQ(n->x.f.bitoff, fl[i].bitoff);
        CHECK_EQ(n->x.f.bitlen, fl[i].bitlen);
        CHECK_EQ(n->x.f.flags, fl[i].flags);
        CHECK_EQ(n->x.f.attrib, fl[i].attrib);
        CHECK_EQ(n->x.f.acc_len, fl[i].acc_len);
        CHECK_EQ(n->x.f.kind, AML_FIELD_NORMAL);
        CHECK(n->x.f.region == aml_lookup(&c, 0, "RGN0"));
    }

    /* Return and Break at the top level of a table */
    hk = 0;
    E(0x08, 'B', 'E', 'F', 'R', 0x01, 0xA4, 0x01, 0x08, 'A', 'F', 'T', 'R', 0x01);
    CHECK_EQ(load_hand(2), 0);
    CHECK(aml_lookup(&c, 0, "BEFR") != 0);
    CHECK(aml_lookup(&c, 0, "AFTR") == 0);
    hk = 0;
    E(0x08, 'B', 'E', 'F', 'R', 0x01, 0xA5, 0x08, 'A', 'F', 'T', 'R', 0x01);
    CHECK_EQ(load_hand(2), AML_EPARSE);
    CHECK_EQ(aml_stats(&c).errors, 1);

    /* table bookkeeping: integer width, checksum, the table limit */
    hk = 0;
    CHECK_EQ(setup(&f, &c, 0, 0), 0);
    uint32_t len = fk_table(tbl, sizeof tbl, "SSDT", 1, hb, 0);
    CHECK_EQ(len, 36);
    CHECK_EQ(aml_load_table(&c, tbl, len), 0);
    CHECK_EQ(c.int64, 0);
    CHECK_EQ(c.dsdt_rev, 1);
    CHECK(!fk_log_has(&f, "checksum"));
    len = fk_table(tbl + 64, sizeof tbl - 64, "DSDT", 2, hb, 0);
    tbl[64 + 9] ^= 0x03; /* checksum off by 3 */
    fk_log_clear(&f);
    CHECK_EQ(aml_load_table(&c, tbl + 64, len), 0);
    CHECK_EQ(c.int64, 1);
    CHECK_EQ(c.dsdt_rev, 2);
    CHECK(fk_log_count(&f, "[AML] table checksum mismatch (loaded anyway) 0x") == 1);
    len = fk_table(tbl + 128, sizeof tbl - 128, "SSDT", 1, hb, 0);
    CHECK_EQ(aml_load_table(&c, tbl + 128, len), 0);
    CHECK_EQ(c.int64, 1);
    CHECK_EQ(c.dsdt_rev, 2);
    CHECK_EQ(c.n_tables, 3);
    CHECK(memcmp(c.tables[2].sig, "SSDT", 4) == 0);
    CHECK(memcmp(c.tables[2].oem_id, "ZXVFK ", 6) == 0);
    CHECK(memcmp(c.tables[2].oem_tid, "AMLTEST ", 8) == 0);
    CHECK_EQ(c.tables[2].len, 36);
    for (uint32_t i = 3; i < AML_MAX_TABLES; i++) CHECK_EQ(aml_load_table(&c, tbl, 36), 0);
    CHECK_EQ(aml_load_table(&c, tbl, 36), AML_ENOMEM);
    CHECK_EQ(c.n_tables, AML_MAX_TABLES);
    CHECK_EQ(aml_stats(&c).tables, AML_MAX_TABLES);
    /* header length exactly the buffer, and one byte past it */
    CHECK_EQ(setup(&f, &c, 0, 0), 0);
    len = fk_table(tbl, sizeof tbl, "SSDT", 2, (const uint8_t *) "\x08XX__\x01", 6);
    CHECK_EQ(aml_load_table(&c, tbl, len), 0);
    CHECK_EQ(aml_load_table(&c, tbl, len - 1), AML_EPARSE);
    CHECK_EQ(aml_load_table(&c, tbl, 35), AML_EARG);
    static aml_ctx_t blank;
    CHECK_EQ(aml_load_table(&blank, tbl, len), AML_EARG);
    CHECK_EQ(aml_load_table(0, tbl, len), AML_EARG);
}

/* ---- stepping over terms --------------------------------------------------------
 * One table holds a term of every shape whose first operand names the missing
 * object ZZZZ, each followed by Increment (CNT_). The execution pass fails
 * each such term and must step over exactly its bytes, so CNT_ ends up at the
 * number of Increments and stats.errors at the number of failures. The
 * declaration pass steps over the same terms to reach FWD1, defined at the
 * very end and called by the first term. */
#define ZZ   'Z', 'Z', 'Z', 'Z'
#define MZ   0x5C, 0x2E, ZZ /* \ZZZZ.xxxx: a parent that does not exist */
#define INCC 0x75, 'C', 'N', 'T', '_'
static uint32_t n_fail, n_inc;
#define FAIL(...)                                                                                  \
    do {                                                                                           \
        E(__VA_ARGS__);                                                                            \
        E(INCC);                                                                                   \
        n_fail++;                                                                                  \
        n_inc++;                                                                                   \
    } while (0)

/* op pkglen body: a package-length op whose body starts with ZZZZ */
static void fail_pkg(const uint8_t *op, uint32_t opn, const uint8_t *body, uint32_t n)
{
    emit(op, opn);
    hk += pkglen(hb + hk, n);
    emit(body, n);
    E(INCC);
    n_fail++;
    n_inc++;
}
#define FAILP(op, ...)                                                                             \
    fail_pkg((const uint8_t[]){op}, 1, (const uint8_t[]){__VA_ARGS__},                             \
             sizeof((const uint8_t[]){__VA_ARGS__}))
#define FAILX(op, ...)                                                                             \
    fail_pkg((const uint8_t[]){0x5B, op}, 2, (const uint8_t[]){__VA_ARGS__},                       \
             sizeof((const uint8_t[]){__VA_ARGS__}))

static void test_skip_shapes(void)
{
    uint64_t v;
    hk = 0;
    n_fail = n_inc = 0;
    E(0x08, 'C', 'N', 'T', '_', 0x00, 0x08, 'R', 'E', 'S', '_', 0x00);
    E(0x08, 'B', 'U', 'F', 'X', 0x11, 0x03, 0x0A, 0x04);
    E(0x15, 'E', 'X', 'T', '2', 0x08, 0x02);       /* External (EXT2, 2) */
    E(0x15, 0x5C, 'E', 'X', 'T', '3', 0x08, 0x03); /* \EXT3, 3 args */
    E(0x15, 0x5C, 0x2E, '_', 'S', 'B', '_', 'E', 'X', 'T', '4', 0x08, 0x01);
    E(0x15, 0x5C, 0x2E, '_', 'S', 'B', '_', 'E', 'X', 'T', '7', 0x08, 0x03);
    E(0x15, 'I', 'N', 'T', 'X', 0x01, 0x00); /* not a method */
    E(0x10, 0x0D, 0x5C, '_', 'S', 'B', '_', 0x15, 'E', 'X', 'T', '6', 0x08, 0x02);
    M("MTH2", 2, 0xA4, 0x68);
    E(0x70, 'F', 'W', 'D', '1', 'R', 'E', 'S', '_'); /* Store (FWD1 (), RES_) */
    E(0xCC, 0xA3, 0xFF, 0x00, 0x01);                 /* BreakPoint Noop Ones Zero One */
    /* operators: targets of every kind, operands of every immediate width */
    FAIL(0x72, ZZ, 0xFF, 0x60);                                    /* Add (,Ones,Local0) */
    FAIL(0x74, ZZ, 0x5B, 0x30, 0x6E);                              /* Subtract (,Revision,Arg6) */
    FAIL(0x77, ZZ, 0x5B, 0x33, 0x00);                              /* Multiply (,Timer,Zero) */
    FAIL(0x7B, ZZ, 0x0E, 1, 2, 3, 4, 5, 6, 7, 8, 0x5B, 0x31);      /* And (,QWord,Debug) */
    FAIL(0x7D, ZZ, 0x0C, 1, 2, 3, 4, 0x71, 'C', 'N', 'T', '_');    /* Or (,DWord,RefOf) */
    FAIL(0x7F, ZZ, 0x0B, 1, 2, 0x83, 'R', 'E', 'S', '_');          /* XOr (,Word,DerefOf) */
    FAIL(0x85, ZZ, 0x0A, 7, 0x88, 'B', 'U', 'F', 'X', 0x00, 0x00); /* Mod (,Byte,Index) */
    FAIL(0x78, ZZ, 0x01, 0x61, 0x62);                              /* Divide */
    FAIL(0x70, ZZ, 0x67);                                          /* Store (, Local7) */
    FAIL(0x72, ZZ, 0x60, 0x00);                                    /* Add (,Local0,) */
    FAIL(0x72, ZZ, 0x6E, 0x00);                                    /* Add (,Arg6,) */
    FAIL(0x70, ZZ, '_', 'Q', 'Q', 'Q');                            /* Store (, _QQQ) */
    FAIL(0x80, ZZ, 0x68);                                          /* Not (, Arg0) */
    FAIL(0x81, ZZ, 0x00);
    FAIL(0x82, ZZ, 0x00);
    FAIL(0x96, ZZ, 0x00);
    FAIL(0x97, ZZ, 0x00);
    FAIL(0x98, ZZ, 0x00);
    FAIL(0x99, ZZ, 0x00);
    FAIL(0x9C, ZZ, 0x0A, 0x02, 0x00);           /* ToString TTS */
    FAIL(0x9D, ZZ, 0x60);                       /* CopyObject TS */
    FAIL(0x9E, ZZ, 0x00, 0x01, 0x00);           /* Mid TTTS */
    FAIL(0x73, ZZ, 0x0D, 'a', 'b', 0x00, 0x00); /* Concatenate (,"ab") */
    FAIL(0x84, ZZ, ZZ, 0x00);                   /* ConcatenateResTemplate */
    FAIL(0x79, ZZ, 0x01, 0x00);
    FAIL(0x7A, ZZ, 0x01, 0x00);
    FAIL(0x7C, ZZ, 0x01, 0x00);
    FAIL(0x7E, ZZ, 0x01, 0x00);
    FAIL(0x75, ZZ);
    FAIL(0x76, ZZ);
    FAIL(0x87, ZZ);
    FAIL(0x8E, ZZ);
    FAIL(0x86, ZZ, 0x0A, 0x80);                         /* Notify ST */
    FAIL(0x89, ZZ, 0x00, 0x01, 0x00, 0x0A, 0x02, 0x00); /* Match */
    FAIL(0x8A, ZZ, 0x00, 'F', 'L', 'D', '1');           /* CreateDWordField */
    FAIL(0x8B, ZZ, 0x00, 'F', 'L', 'D', '2');
    FAIL(0x8C, ZZ, 0x00, 'F', 'L', 'D', '3');
    FAIL(0x8D, ZZ, 0x00, 'F', 'L', 'D', '4');
    FAIL(0x8F, ZZ, 0x00, 'F', 'L', 'D', '5');
    FAIL(0x90, ZZ, 0x01);
    FAIL(0x91, ZZ, 0x01);
    FAIL(0x92, ZZ);
    FAIL(0x93, ZZ, 0x01);
    FAIL(0x94, ZZ, 0x01);
    FAIL(0x95, ZZ, 0x01);
    FAIL(0x83, ZZ);
    FAIL(0x88, ZZ, 0x00, 0x00);
    FAIL(0x71, ZZ);
    FAIL(0x08, MZ, 'N', 'A', 'M', 'Z', 0xFF); /* Name (\ZZZZ.NAMZ, Ones) */
    FAIL(0x06, ZZ, 'A', 'L', 'S', 'Z');       /* Alias (ZZZZ, ALSZ) */
    /* package-length objects */
    FAILP(0xA0, ZZ, 0xA3);                           /* If (ZZZZ) { Noop } */
    FAILP(0xA2, ZZ, 0xA3);                           /* While */
    FAILP(0x10, ZZ, 0xA3);                           /* Scope (ZZZZ) */
    FAILP(0x11, ZZ, 0x01);                           /* Buffer (ZZZZ) */
    FAILP(0x13, ZZ, 0x01);                           /* VarPackage (ZZZZ) */
    FAILP(0x14, MZ, 'M', 'T', 'H', 'Z', 0x00, 0xA3); /* Method (\ZZZZ.MTHZ) */
    FAILX(0x81, ZZ, 0x01, 'F', 'D', '0', '0', 0x08);
    FAILX(0x82, MZ, 'D', 'E', 'V', 'Z');
    FAILX(0x83, MZ, 'C', 'P', 'U', 'Z', 0x01, 0, 0, 0, 0, 0x00);
    FAILX(0x84, MZ, 'P', 'W', 'R', 'Z', 0x00, 0x00, 0x00);
    FAILX(0x85, MZ, 'T', 'Z', 'Z', 'Z');
    FAILX(0x86, ZZ, ZZ, 0x01, 'F', 'D', '0', '1', 0x08);
    FAILX(0x87, ZZ, ZZ, 0x00, 0x01, 'F', 'D', '0', '2', 0x08);
    /* extended operators */
    FAIL(0x5B, 0x01, MZ, 'M', 'T', 'X', 'Z', 0x00);                             /* Mutex */
    FAIL(0x5B, 0x02, MZ, 'E', 'V', 'T', 'Z');                                   /* Event */
    FAIL(0x5B, 0x12, 'C', 'N', 'T', '_', ZZ);                                   /* CondRefOf SS */
    FAIL(0x5B, 0x13, ZZ, 0x00, 0x0A, 0x08, 'F', 'L', 'D', 'Z');                 /* CreateField */
    FAIL(0x5B, 0x1F, ZZ, 0x0D, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x00); /* LoadTable */
    FAIL(0x5B, 0x20, ZZ, 0x60);                                                 /* Load NS */
    FAIL(0x5B, 0x21, ZZ);
    FAIL(0x5B, 0x22, ZZ);
    FAIL(0x5B, 0x23, ZZ, 0xFF, 0xFF); /* Acquire SW */
    FAIL(0x5B, 0x24, ZZ);
    FAIL(0x5B, 0x25, ZZ, 0x01); /* Wait ST */
    FAIL(0x5B, 0x26, ZZ);
    FAIL(0x5B, 0x27, ZZ);
    FAIL(0x5B, 0x28, ZZ, 0x00);
    FAIL(0x5B, 0x29, ZZ, 0x00);
    FAIL(0x5B, 0x2A, ZZ);
    FAIL(0x5B, 0x32, 0x01, 0x02, 0x00, 0x00, 0x00, ZZ);         /* Fatal BDT */
    FAIL(0x5B, 0x80, MZ, 'R', 'G', 'N', 'Z', 0x00, 0x00, 0x01); /* OperationRegion */
    FAIL(0x5B, 0x88, MZ, 'D', 'T', 'R', 'Z', 0x0D, 'A', 0x00, 0x0D, 0x00, 0x0D, 0x00);
    /* calls: the argument count comes from the method or from an External */
    /* each argument is an Increment: stepped over, it never runs */
    FAIL('M', 'T', 'H', '2', ZZ, INCC);
    FAIL('E', 'X', 'T', '2', INCC, INCC);
    FAIL(0x5C, 'E', 'X', 'T', '3', INCC, INCC, INCC);
    FAIL(0x5C, 0x2E, '_', 'S', 'B', '_', 'E', 'X', 'T', '4', INCC);
    FAIL(0x5C, 0x2E, '_', 'S', 'B', '_', 'E', 'X', 'T', '6', INCC, INCC);
    {
        /* inside \_SB: EXT4 and EXT6 by their own name, EXT2 by ^EXT2 */
        static const uint8_t body[] = {0x5C, '_',  'S', 'B',  '_',  'E',  'X',  'T',  '4',
                                       INCC, INCC, 'E', 'X',  'T',  '6',  INCC, INCC, INCC,
                                       '^',  'E',  'X', 'T',  '2',  INCC, INCC, INCC, 'E',
                                       'X',  'T',  '2', INCC, INCC, INCC};
        E(0x10);
        hk += pkglen(hb + hk, sizeof body);
        emit(body, sizeof body);
        n_fail += 4;
        n_inc += 4;
    }
    /* same last segment, another scope: no argument count, so the
     * Increments that follow run */
    E('E', 'X', 'T', '7', INCC, INCC, INCC);
    E(0x5C, 0x2E, '_', 'S', 'B', '_', 'E', 'X', 'T', '2', INCC, INCC);
    E('I', 'N', 'T', 'X', INCC);
    n_fail += 3;
    n_inc += 6;
    /* the execution pass stops at Break; the declaration pass steps over
     * Break and Continue to reach FWD1 */
    E(0xA5, 0x9F);
    M("FWD1", 0, 0xA4, 0x0A, 0x77);
    CHECK_EQ(load_hand(2), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "CNT_", &v), 0);
    CHECK_EQ(v, n_inc);
    CHECK_EQ(ev_int(&c, "RES_", &v), 0);
    CHECK_EQ(v, 0x77);
    CHECK_EQ(aml_stats(&c).errors, n_fail + 1);
    CHECK_EQ(c.n_ext, 5);
    CHECK(aml_lookup(&c, 0, "FLDZ") == 0);
    CHECK(aml_lookup(&c, 0, "ALSZ") == 0);
}

/* Devices Lii_ .. Ldd_, each inside the one before; returns the bytes */
static uint32_t nest_dev(uint8_t *o, uint32_t i, uint32_t depth)
{
    uint8_t body[1024];
    uint32_t n = 0, k = 2;
    body[n++] = 'L';
    body[n++] = (uint8_t) ('0' + i / 10);
    body[n++] = (uint8_t) ('0' + i % 10);
    body[n++] = '_';
    if (i < depth) n += nest_dev(body + n, i + 1, depth);
    o[0] = 0x5B;
    o[1] = 0x82;
    k += pkglen(o + 2, n);
    memcpy(o + k, body, n);
    return k + n;
}

static void test_names_more(void)
{
    uint64_t v;
    hk = 0;
    E(0x08, 'C', 'N', 'T', '_', 0x00);
    /* targets that cannot be stepped over: the loader resynchronises */
    E(0x70, ZZ, 0x5B, 0x33, INCC); /* Store (ZZZZ, Timer) */
    E(0x70, ZZ, 0x0A, 0x05, INCC); /* Store (ZZZZ, 5) */
    /* a name defined twice, a method defined twice */
    E(0x08, 'D', 'U', 'P', '1', 0x01, 0x08, 'D', 'U', 'P', '1', 0x0A, 0x02);
    M("FWD2", 0, 0xA4, 0x01);
    M("FWD2", 0, 0xA4, 0x0A, 0x02);
    /* names created through prefixes and paths */
    E(0x10, 0x16, 0x5C, '_', 'S', 'B', '_', 0x08, '^', 'U', 'P', 'N', '1', 0x0A, 0x03, 0x08, 0x5C,
      'R', 'T', 'N', '1', 0x0A, 0x04);
    E(0x08, 0x5C, 0x2E, '_', 'S', 'B', '_', 'M', 'S', 'N', '1', 0x0A, 0x05);
    E(0x08, '^', '^', 'U', 'P', 'N', '2', 0x0A, 0x06);
    M("NUL1", 0, 0x08, 0x00, 0x0A, 0x05, 0xA4, 0x01);
    /* one External more than the table holds */
    for (int i = 0; i <= AML_MAX_EXTERNAL; i++)
        E(0x15, 'E', (uint8_t) ('0' + i / 100), (uint8_t) ('0' + i / 10 % 10),
          (uint8_t) ('0' + i % 10), 0x08, 0x01);
    E('E', '0', '6', '4', INCC); /* not recorded: no argument to step over */
    E('E', '0', '6', '3', INCC); /* recorded: its argument is stepped over */
    hk += nest_dev(hb + hk, 1, 17);
    CHECK_EQ(load_hand(2), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "CNT_", &v), 0);
    CHECK_EQ(v, 3);
    CHECK_EQ(aml_stats(&c).errors, 9);
    CHECK_EQ(c.n_ext, AML_MAX_EXTERNAL);
    CHECK_EQ(ev_int(&c, "DUP1", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "FWD2", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "\\UPN1", &v), 0);
    CHECK_EQ(v, 3);
    CHECK_EQ(ev_int(&c, "\\RTN1", &v), 0);
    CHECK_EQ(v, 4);
    CHECK_EQ(ev_int(&c, "\\_SB.MSN1", &v), 0);
    CHECK_EQ(v, 5);
    CHECK(aml_lookup(&c, 0, "UPN2") == 0);
    CHECK_EQ(ev_int(&c, "NUL1", &v), AML_EPARSE);
    /* ASCII paths: short segments padded, long or empty ones refused */
    aml_node_t *sb = aml_lookup(&c, 0, "\\_SB_");
    CHECK(sb != 0);
    CHECK(aml_lookup(&c, 0, "_SB") == sb);
    CHECK(aml_lookup(&c, 0, "_SB_X") == 0);
    CHECK(aml_lookup(&c, 0, "\\_SB_..MSN1") == 0);
    CHECK(aml_lookup(&c, 0, "\\_SB_.MSN1") != 0);
    {
        static char p[300];
        memset(p, '^', 256);
        memcpy(p + 256, "_SB_", 5);
        CHECK(aml_lookup(&c, 0, p) == 0);
        /* sixteen segments are looked up, seventeen are refused */
        uint32_t n = 0;
        for (int i = 1; i <= 17; i++) {
            int w_ = snprintf(p + n, sizeof p - n, "%sL%02d_", i == 1 ? "\\" : ".", i);
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof p - n)) abort();
            n += (uint32_t) w_;
        }
        CHECK(aml_lookup(&c, 0, p) == 0);
        p[n - 5] = 0;
        aml_node_t *d16 = aml_lookup(&c, 0, p);
        CHECK(d16 != 0);
        char q[64];
        CHECK_EQ(aml_node_path(d16, q, sizeof q), 63);
        CHECK(strcmp(q, "\\L01_.L02_.L03_.L04_.L05_.L06_.L07_.L08_.L09_.L10_.L11_.L12_.L1") == 0);
    }
}

/* Devices <p>ii_ .. <p>dd_ nested, the innermost holding inner[]; returns the bytes */
static uint32_t nest_in(uint8_t *o, char p, uint32_t i, uint32_t depth, const uint8_t *inner,
                        uint32_t ilen)
{
    static uint8_t bodies[40][1200];
    uint8_t *body = bodies[i];
    uint32_t n = 0, k = 2;
    body[n++] = (uint8_t) p;
    body[n++] = (uint8_t) ('0' + i / 10);
    body[n++] = (uint8_t) ('0' + i % 10);
    body[n++] = '_';
    if (i < depth) {
        n += nest_in(body + n, p, i + 1, depth, inner, ilen);
    } else {
        memcpy(body + n, inner, ilen);
        n += ilen;
    }
    o[0] = 0x5B;
    o[1] = 0x82;
    k += pkglen(o + 2, n);
    memcpy(o + k, body, n);
    return k + n;
}

/* Store (\<p>01_ ... <p>dd_.MDEP (), name): a call through depth segments */
static void call_deep(char p, uint32_t depth, const char *name)
{
    E(0x70, 0x5C, 0x2F, (uint8_t) (depth + 1));
    for (uint32_t i = 1; i <= depth; i++)
        E((uint8_t) p, (uint8_t) ('0' + i / 10), (uint8_t) ('0' + i % 10), '_');
    E('M', 'D', 'E', 'P');
    emit((const uint8_t *) name, 4);
}

static void test_tables_more(void)
{
    uint64_t v;
    aml_node_t *n;

    /* statements: Unload, Fatal, CopyObject and RefOf of Zero, Store's value */
    hk = 0;
    E(0x08, 'C', 'N', 'T', '0', 0x00);
    E(0x5B, 0x82, 0x0B, 'D', 'E', 'V', '0', 0x08, '_', 'A', 'D', 'R', 0x00);
    M("UL00", 0, 0x5B, 0x2A, 0x00, 0xA4, 0x00);
    M("UL01", 0, 0x5B, 0x2A, 'D', 'E', 'V', '0', 0xA4, 0x00);
    M("UL02", 0, 0x5B, 0x2A, ZZ, 0xA4, 0x00);
    M("FT01", 0, 0x5B, 0x32, 0x01, 0x02, 0x00, 0x00, 0x00, 0x75, 'C', 'N', 'T', '0');
    M("CP00", 0, 0x9D, 0x01, 0x00, 0xA4, 0x00);
    M("ST01", 0, 0xA4, 0x70, 0x0A, 0x05, 0x60);
    M("RF00", 0, 0xA4, 0x71, 0x00);
    CHECK_EQ(load_hand(2), 0);
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "UL00", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "UL02", &v), AML_ENOENT);
    CHECK_EQ(f.nlog, 1); /* name not found */
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "UL01", &v), 0);
    CHECK_EQ(f.nlog, 1);
    CHECK(strcmp(f.log[0], "[AML] Unload ignored") == 0);
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "FT01", &v), AML_EFATAL);
    CHECK_EQ(ev_int(&c, "CNT0", &v), 0);
    CHECK_EQ(v, 1);
    CHECK(strcmp(f.log[0], "[AML] Fatal opcode, code 0x2") == 0);
    CHECK_EQ(ev_int(&c, "CP00", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "ST01", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(ev_int(&c, "RF00", &v), AML_EPARSE);
    /* a table loaded from the top resets last_error; nested calls never do */
    {
        static uint8_t t36[36];
        fk_table(t36, sizeof t36, "SSDT", 2, t36, 0);
        c.last_error = AML_EIO;
        CHECK_EQ(aml_load_table(&c, t36, 36), 0);
        CHECK_EQ(c.last_error, 0);
        c.depth = AML_MAX_CALL_DEPTH;
        CHECK_EQ(aml_load_table(&c, t36, 36), AML_ELIMIT);
    }
    c.depth = 0;

    /* the declaration pass: a call before its External, a method in a Scope,
     * a method's exact length, a Scope whose target is missing */
    hk = 0;
    E(0x08, 'C', 'N', 'T', '_', 0x00, 0x08, 'R', 'E', 'S', '1', 0x00);
    E('E', 'X', 'T', '8', INCC, INCC);
    E(0x15, 'E', 'X', 'T', '8', 0x08, 0x02);
    E(0x70, 0x5C, 0x2E, '_', 'S', 'B', '_', 'S', 'C', 'M', '1', 'R', 'E', 'S', '1');
    E(0x10, 0x10, 0x5C, '_', 'S', 'B', '_', 0x14, 0x09, 'S', 'C', 'M', '1', 0x00, 0xA4, 0x0A, 0x04);
    E('F', 'W', 'D', '4');
    M("FWD4", 0, INCC);
    E(INCC);
    E(0x10, 0x0F, 'N', 'O', 'P', 'Z', 0x14, 0x09, 'S', 'M', 'X', '1', 0x00, 0xA4, 0x0A, 0x01);
    CHECK_EQ(load_hand(2), AML_ENOENT);
    CHECK_EQ(aml_stats(&c).errors, 2);
    CHECK_EQ(ev_int(&c, "CNT_", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(ev_int(&c, "RES1", &v), 0);
    CHECK_EQ(v, 4);
    CHECK(aml_lookup(&c, 0, "SMX1") == 0);
    CHECK_EQ(aml_lookup(&c, 0, "FWD4")->x.m.len, 5);

    /* a Processor cut short and a Method without its flags end the
     * declaration pass: what follows them cannot be called early */
    hk = 0;
    E(0x08, 'R', 'E', 'S', '3', 0x00);
    E(0x70, 'F', 'W', 'D', '5', 'R', 'E', 'S', '3');
    E(0x5B, 0x83, 0x08, 'C', 'P', 'U', 'T', 0x01, 0x00, 0x00);
    M("FWD5", 0, 0xA4, 0x0A, 0x05);
    CHECK_EQ(load_hand(2), AML_ENOENT);
    CHECK_EQ(aml_stats(&c).errors, 2);
    CHECK_EQ(ev_int(&c, "RES3", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(ev_int(&c, "FWD5", &v), 0);
    CHECK_EQ(v, 5);
    hk = 0;
    E(0x08, 'R', 'E', 'S', '4', 0x00);
    E(0x70, 'F', 'W', 'D', '6', 'R', 'E', 'S', '4');
    E(0x14, 0x05, 'M', 'B', 'A', 'D');
    M("FWD6", 0, 0xA4, 0x0A, 0x06);
    CHECK_EQ(load_hand(2), AML_ENOENT);
    CHECK_EQ(aml_stats(&c).errors, 2);
    CHECK_EQ(ev_int(&c, "RES4", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(ev_int(&c, "FWD6", &v), 0);
    CHECK_EQ(v, 6);

    /* the declaration pass goes 32 containers deep, not 33 */
    {
        static const uint8_t m32[] = {0x14, 0x09, 'M', 'D', 'E', 'P', 0x00, 0xA4, 0x0A, 0x32};
        static const uint8_t m33[] = {0x14, 0x09, 'M', 'D', 'E', 'P', 0x00, 0xA4, 0x0A, 0x33};
        hk = 0;
        E(0x08, 'R', 'S', '3', '2', 0x00, 0x08, 'R', 'S', '3', '3', 0x00);
        call_deep('K', 32, "RS32");
        call_deep('J', 33, "RS33");
        hk += nest_in(hb + hk, 'K', 1, 32, m32, sizeof m32);
        hk += nest_in(hb + hk, 'J', 1, 33, m33, sizeof m33);
        CHECK_EQ(load_hand(2), AML_ENOENT);
        CHECK_EQ(aml_stats(&c).errors, 1);
        CHECK_EQ(ev_int(&c, "RS32", &v), 0);
        CHECK_EQ(v, 0x32);
        CHECK_EQ(ev_int(&c, "RS33", &v), 0);
        CHECK_EQ(v, 0);
    }

    /* the body starts at byte 36 in both passes: byte 35 is not code */
    hk = 0;
    E(0x08, 'R', 'E', 'S', '5', 0x00);
    E(0x70, 'F', 'W', 'D', '7', 'R', 'E', 'S', '5');
    M("FWD7", 0, 0xA4, 0x0A, 0x07);
    {
        uint32_t len = fk_table(tbl, sizeof tbl, "DSDT", 2, hb, hk);
        tbl[35] = 0x02;
        fk_init(&f);
        CHECK_EQ(aml_init(&c, &f.plat, g_arena, ARENA_SIZE), 0);
        CHECK_EQ(c.int64, 1);
        CHECK_EQ(aml_load_table(&c, tbl, len), 0);
        CHECK_EQ(aml_stats(&c).errors, 0);
        CHECK_EQ(ev_int(&c, "RES5", &v), 0);
        CHECK_EQ(v, 7);
        CHECK(strcmp(f.log[0], "[AML] table checksum mismatch (loaded anyway) 0x2") == 0);
    }

    /* 64 unknown opcodes are stepped over, the 65th ends the table */
    for (int k = 64; k <= 65; k++) {
        hk = 0;
        for (int i = 0; i < k; i++) E(0x02);
        E(0x08, 'A', 'F', 'T', '1', 0x01);
        CHECK_EQ(load_hand(2), AML_EPARSE);
        CHECK_EQ(aml_stats(&c).errors, k == 64 ? 64u : 66u);
        CHECK_EQ(aml_lookup(&c, 0, "AFT1") != 0, k == 64);
    }

    /* a table length with all four bytes set */
    {
        uint32_t len = 0x01010124;
        uint8_t *big = malloc(len);
        CHECK(big != 0);
        if (big) {
            memset(big, 0xA3, len);
            CHECK_EQ(fk_table(big, len, "SSDT", 2, big + 36, 0), 36);
            big[4] = 0x24;
            big[5] = 0x01;
            big[6] = 0x01;
            big[7] = 0x01;
            CHECK_EQ(setup(&f, &c, 0, 0), 0);
            CHECK_EQ(aml_load_table(&c, big, len), 0);
            CHECK_EQ(c.tables[0].len, len);
            CHECK_EQ(aml_load_table(&c, big, len - 1), AML_EPARSE);
            free(big);
        }
    }

    /* regions at table level wait for their base and length; in a method
     * they are evaluated at once */
    hk = 0;
    E(0x5B, 0x80, 'R', 'G', 'D', '1', 0x00, 'B', 'A', 'S', 'X', 0x0A, 0x10);
    E(0x08, 'B', 'A', 'S', 'X', 0x0B, 0x00, 0x10);
    E(0x5B, 0x80, 'R', 'G', 'D', '2', 0x00, 'N', 'O', 'P', 'X', 0x0A, 0x04);
    E(0x10, 0x1B, 0x5C, '_', 'S', 'B', '_', 0x5B, 0x80, 'R', 'G', 'D', '3', 0x00, 'B', 'A', 'S',
      'Y', 0x0A, 0x04, 0x08, 'B', 'A', 'S', 'Y', 0x0B, 0x00, 0x30);
    M("RGM1", 0, 0x5B, 0x80, 'R', 'G', 'M', '0', 0x00, 0x0C, 0x00, 0x00, 0x05, 0x00, 0x0A, 0x04,
      0x5B, 0x81, 0x0B, 'R', 'G', 'M', '0', 0x01, 'F', 'R', 'G', '0', 0x08, 0xA4, 'F', 'R', 'G',
      '0');
    CHECK_EQ(load_hand(2), 0);
    {
        static uint8_t mem[16] = {0x5A};
        fk_window(&f, 0x50000, mem, sizeof mem);
        CHECK_EQ(ev_int(&c, "RGM1", &v), 0);
        CHECK_EQ(v, 0x5A);
    }
    n = aml_lookup(&c, 0, "RGD1");
    CHECK_EQ(n->x.r.ready, 0);
    CHECK_EQ(n->x.r.defer_len, 6);
    CHECK_EQ(aml_region_eval_deferred(&c, n), 0);
    CHECK_EQ(n->x.r.ready, 1);
    CHECK_EQ(n->x.r.base, 0x1000);
    CHECK_EQ(n->x.r.len, 0x10);
    n = aml_lookup(&c, 0, "RGD2");
    CHECK_EQ(aml_region_eval_deferred(&c, n), AML_ENOENT);
    CHECK_EQ(n->x.r.ready, 0);
    n = aml_lookup(&c, 0, "\\_SB.RGD3");
    CHECK_EQ(aml_region_eval_deferred(&c, n), 0);
    CHECK_EQ(n->x.r.base, 0x3000);
    CHECK_EQ(n->x.r.len, 4);

    /* Load from a region: the region and the header length are checked
     * against 36 bytes, the region's size and AML_MAX_OBJ */
    {
        static uint8_t w1[36], w2[40], w3[37], w4[37], w5[AML_MAX_OBJ + 1], w6[AML_MAX_OBJ];
        static uint8_t lb1[37];
        static const uint8_t one = 0x01, noop = 0xA3, bp = 0xCC;
        fk_table(w1, sizeof w1, "SSDT", 2, &noop, 0);
        fk_table(w2, sizeof w2, "SSDT", 2, &noop, 0);
        w2[4] = 35;
        fk_table(w3, sizeof w3, "SSDT", 2, &bp, 1);
        w3[4] = 38;
        fk_table(w4, sizeof w4, "SSDT", 2, &noop, 1);
        memset(w5, 0xA3, sizeof w5);
        fk_table(w5, sizeof w5, "SSDT", 2, w5 + 36, 0);
        w5[4] = 0x01;
        w5[5] = 0x00;
        w5[6] = 0x04;
        memset(w6, 0xA3, sizeof w6);
        fk_table(w6, sizeof w6, "SSDT", 2, w6 + 36, 0);
        w6[4] = 0x00;
        w6[5] = 0x00;
        w6[6] = 0x04;
        fk_table(lb1, sizeof lb1, "SSDT", 2, &one, 1);
        hk = 0;
#define RGN(n0, n1, n2, n3, base, ...)                                                             \
    E(0x5B, 0x80, n0, n1, n2, n3, 0x00, 0x0C, (uint8_t) (base), (uint8_t) ((base) >> 8),           \
      (uint8_t) ((base) >> 16), (uint8_t) ((base) >> 24), __VA_ARGS__)
        RGN('R', 'L', '3', '6', 0x10000, 0x0A, 36);
        RGN('R', 'L', '3', '5', 0x10000, 0x0A, 35);
        RGN('R', 'H', '3', '5', 0x20000, 0x0A, 40);
        RGN('R', 'H', '3', '8', 0x30000, 0x0A, 37);
        RGN('R', 'X', '3', '7', 0x40000, 0x0A, 37);
        RGN('B', 'I', 'G', '1', 0x100000, 0x0C, 0x01, 0x00, 0x04, 0x00);
        RGN('B', 'I', 'G', '0', 0x200000, 0x0C, 0x00, 0x00, 0x04, 0x00);
#undef RGN
        E(0x08, 'L', 'B', '0', '1', 0x11, 0x28, 0x0A, 37);
        emit(lb1, 37);
        lb1[0] = 'T';
        E(0x08, 'L', 'B', '0', '2', 0x11, 0x28, 0x0A, 37);
        emit(lb1, 37);
        static const char *const lds[] = {"RL36", "RL35", "RH35", "RH38", "RX37",
                                          "BIG1", "BIG0", "LB01", "LB02"};
        for (int i = 0; i < 9; i++) {
            uint8_t b[] = {0xA4, 0x5B, 0x20, 0, 0, 0, 0, 0x00};
            char nm[5] = {'L', '0', '0', (char) ('0' + i), 0};
            memcpy(b + 3, lds[i], 4);
            emit_method(nm, 0, b, sizeof b);
        }
        CHECK_EQ(load_hand(2), 0);
        fk_window(&f, 0x10000, w1, sizeof w1);
        fk_window(&f, 0x20000, w2, sizeof w2);
        fk_window(&f, 0x30000, w3, sizeof w3);
        fk_window(&f, 0x40000, w4, sizeof w4);
        fk_window(&f, 0x100000, w5, sizeof w5);
        fk_window(&f, 0x200000, w6, sizeof w6);
        static const int want_err[] = {
            0, AML_EBOUNDS, AML_EBOUNDS, AML_EBOUNDS, 0, AML_EBOUNDS, 0, 0, 0};
        for (int i = 0; i < 9; i++) {
            char nm[5] = {'L', '0', '0', (char) ('0' + i), 0};
            v = 0x55;
            CHECK_EQ(ev_int(&c, nm, &v), want_err[i]);
            if (!want_err[i]) CHECK_EQ(v, ONES);
        }
        CHECK_EQ(c.n_tables, 6);
        CHECK_EQ(c.tables[3].len, AML_MAX_OBJ);
        /* the same bytes again: refused before loading */
        fk_log_clear(&f);
        CHECK_EQ(ev_int(&c, "L007", &v), 0);
        CHECK_EQ(v, 0);
        CHECK(strcmp(f.log[0], "[AML] Load: dynamic table already exists 37") == 0);
        CHECK_EQ(ev_int(&c, "L004", &v), 0);
        CHECK_EQ(v, 0);
        CHECK_EQ(c.n_tables, 6);
    }
}

/* A raw PkgLength in its four-byte form */
static uint32_t raw4(uint8_t *o, uint32_t n)
{
    o[0] = (uint8_t) (0xC0 | (n & 0xF));
    o[1] = (uint8_t) (n >> 4);
    o[2] = (uint8_t) (n >> 12);
    o[3] = (uint8_t) (n >> 20);
    return 4;
}

/* Method name { <op> PkgLength <head> <list> Return (One) } */
static void field_method(const char *name, const uint8_t *op, uint32_t opn, const uint8_t *head,
                         uint32_t hn, const uint8_t *list, uint32_t ln)
{
    static uint8_t b[512], body[480];
    uint32_t n = 0, k = 0;
    memcpy(body, head, hn);
    memcpy(body + hn, list, ln);
    n = hn + ln;
    memcpy(b, op, opn);
    k = opn;
    k += pkglen(b + k, n);
    memcpy(b + k, body, n);
    k += n;
    b[k++] = 0xA4;
    b[k++] = 0x01;
    emit_method(name, 0, b, k);
}

static void test_fields_api(void)
{
    static const uint8_t F81[] = {0x5B, 0x81}, F86[] = {0x5B, 0x86}, F87[] = {0x5B, 0x87};
    static const uint8_t rg1[] = {'R', 'G', 'N', '0', 0x01};
    uint8_t l[200];
    uint32_t n;
    uint64_t v;
    hk = 0;
    E(0x08, 'C', 'N', 'T', '0', 0x00);
    E(0x5B, 0x80, 'R', 'G', 'N', '0', 0x00, 0x0C, 0x00, 0x00, 0x06, 0x00, 0x0A, 0x10);
    E(0x5B, 0x81, 0x0B, 'R', 'G', 'N', '0', 0x01, 'F', 'L', 'D', 'A', 0x08);
    /* bit offsets up to 0x7FFFFFFF: 8 x 0x0FFFFFFF reserved, then 7 or 8 more */
    for (int c4 = 0; c4 < 4; c4++) {
        n = 0;
        for (int i = 0; i < 8; i++) {
            l[n++] = 0x00;
            n += raw4(l + n, 0x0FFFFFF);
            l[n - 1] = 0xFF; /* 0x0FFFFFFF */
        }
        if (c4 < 2) {
            l[n++] = 0x00;
            n += raw4(l + n, 7);
            if (c4 == 1) {
                l[n++] = 0x00;
                n += raw4(l + n, 1);
            }
        } else {
            memcpy(l + n, "FX01", 4);
            n += 4;
            n += raw4(l + n, c4 == 2 ? 7 : 8);
        }
        char nm[5] = {'F', 'B', '0', (char) ('0' + c4), 0};
        field_method(nm, F81, 2, rg1, 5, l, n);
    }
    /* one field of AML_MAX_OBJ bytes, and one bit more */
    for (int c2 = 0; c2 < 2; c2++) {
        n = 0;
        memcpy(l, "FX02", 4);
        n = 4 + raw4(l + 4, AML_MAX_OBJ * 8u + (uint32_t) c2);
        char nm[5] = {'F', 'M', '0', (char) ('0' + c2), 0};
        field_method(nm, F81, 2, rg1, 5, l, n);
    }
    {
        /* access types: 5 (buffer) is the last one */
        static const uint8_t a5[] = {0x01, 0x05, 0x00, 'F', 'X', '0', '3', 0x08};
        static const uint8_t a6[] = {0x01, 0x06, 0x00, 'F', 'X', '0', '3', 0x08};
        static const uint8_t f8[] = {'F', 'X', '0', '4', 0x08};
        static const uint8_t r5[] = {'R', 'G', 'N', '0', 0x05}, r6[] = {'R', 'G', 'N', '0', 0x06};
        static const uint8_t cut[] = {'F', 'X', '5'};
        static const uint8_t dup[] = {'F', 'X', '6', '_', 0x08, 'F', 'X', '6', '_', 0x08};
        field_method("FA05", F81, 2, rg1, 5, a5, sizeof a5);
        field_method("FA06", F81, 2, rg1, 5, a6, sizeof a6);
        field_method("FF05", F81, 2, r5, 5, f8, sizeof f8);
        field_method("FF06", F81, 2, r6, 5, f8, sizeof f8);
        field_method("FCUT", F81, 2, rg1, 5, cut, sizeof cut);
        field_method("FDUP", F81, 2, rg1, 5, dup, sizeof dup);
        /* BankField: a missing bank value, a bank register that is not a
         * field, a good one; IndexField and Field over the wrong objects */
        static const uint8_t bk1[] = {'R', 'G', 'N', '0', 'F', 'L', 'D', 'A', ZZ, 0x01};
        static const uint8_t bk2[] = {'R', 'G', 'N', '0', 'C', 'N', 'T', '0', 0x01, 0x01};
        static const uint8_t bk3[] = {'R', 'G', 'N', '0', 'F', 'L', 'D', 'A', 0x01, 0x01};
        static const uint8_t bk4[] = {'C', 'N', 'T', '0', 'F', 'L', 'D', 'A', 0x01, 0x01};
        static const uint8_t ix1[] = {'F', 'L', 'D', 'A', 'C', 'N', 'T', '0', 0x01};
        static const uint8_t ix2[] = {'F', 'L', 'D', 'A', 'F', 'L', 'D', 'A', 0x01};
        static const uint8_t fd1[] = {'C', 'N', 'T', '0', 0x01};
        field_method("BK01", F87, 2, bk1, sizeof bk1, f8, sizeof f8);
        field_method("BK02", F87, 2, bk2, sizeof bk2, f8, sizeof f8);
        field_method("BK03", F87, 2, bk3, sizeof bk3, f8, sizeof f8);
        field_method("BK04", F87, 2, bk4, sizeof bk4, f8, sizeof f8);
        field_method("IX01", F86, 2, ix1, sizeof ix1, f8, sizeof f8);
        field_method("IX02", F86, 2, ix2, sizeof ix2, f8, sizeof f8);
        field_method("FD01", F81, 2, fd1, sizeof fd1, f8, sizeof f8);
    }
    M("MTH1", 1, 0xA4, 0x68);
    CHECK_EQ(load_hand(2), 0);
    static const struct {
        const char *m;
        int err;
    } fe[] = {
        {"FB00", 0},          {"FB01", AML_EPARSE}, {"FB02", 0},          {"FB03", AML_EPARSE},
        {"FM00", 0},          {"FM01", AML_EPARSE}, {"FA05", 0},          {"FA06", AML_EPARSE},
        {"FF05", 0},          {"FF06", AML_EPARSE}, {"FCUT", AML_EPARSE}, {"FDUP", AML_EEXIST},
        {"BK01", AML_ENOENT}, {"BK02", AML_ETYPE},  {"BK03", 0},          {"BK04", AML_ETYPE},
        {"IX01", AML_ETYPE},  {"IX02", 0},          {"FD01", AML_ETYPE},
    };
    for (uint32_t i = 0; i < sizeof fe / sizeof fe[0]; i++) {
        int e = ev_int(&c, fe[i].m, &v);
        g_checks++;
        if (e != fe[i].err) {
            g_fails++;
            printf("FAIL fields %s: err %d, want %d\n", fe[i].m, e, fe[i].err);
        }
    }
    fk_log_clear(&f);
    CHECK_EQ(ev_int(&c, "FDUP", &v), AML_EEXIST);
    CHECK_EQ(f.nlog, 1);
    CHECK(strcmp(f.log[0], "[AML] duplicate field name skipped") == 0);

    /* the public evaluation API around its arguments */
    {
        aml_val_t a[8], r;
        uint32_t h = heap();
        for (int i = 0; i < 8; i++) a[i] = aml_int(1);
        CHECK_EQ(aml_eval(&c, 0, "MTH1", a, 8, &r), AML_EARG);
        a[0] = aml_str(&c, "x");
        a[1] = nest(49);
        CHECK_EQ(aml_eval(&c, 0, "MTH1", a, 2, &r), AML_ELIMIT);
        aml_val_free(&c, &a[0]);
        aml_val_free(&c, &a[1]);
        CHECK_EQ(heap(), h);
        a[0] = aml_str(&c, "abc");
        CHECK_EQ(aml_eval(&c, 0, "CNT0", a, 1, &r), 0);
        aml_val_free(&c, &a[0]);
        CHECK_EQ(heap(), h);
        r = aml_int(5);
        CHECK_EQ(aml_eval(&c, 0, "NOPE", 0, 0, &r), AML_ENOENT);
        CHECK_EQ(r.type, AML_T_UNINIT);
        aml_node_t *osi = aml_lookup(&c, 0, "_OSI");
        CHECK_EQ(aml_call(&c, osi, 0, 0, &r), AML_ETYPE);
        static const struct {
            const char *s;
            uint64_t want;
        } os[] = {{"Windows 2000", ONES},
                  {"Xindows 2000", 0},
                  {"Windows 200", 0},
                  {"Windows 20000", 0},
                  {"Extended Address Space Descriptor", ONES},
                  {"", 0}};
        for (uint32_t i = 0; i < sizeof os / sizeof os[0]; i++) {
            a[0] = aml_str(&c, os[i].s);
            CHECK_EQ(aml_eval(&c, 0, "_OSI", a, 1, &r), 0);
            CHECK_EQ(r.u.i, os[i].want);
            aml_val_free(&c, &a[0]);
        }
        CHECK_EQ(heap(), h);
    }
}

/* ---- a failing operand in every position ------------------------------------------
 * For each operator: one method with valid operands (it must succeed) and one
 * per operand position with the missing name ZZZZ there instead. Each of those
 * must fail with AML_ENOENT: an evaluator that keeps going after a failed
 * operand, or reads the operands in the wrong order, returns something else. */
typedef struct gop {
    const char *op;     /* opcode bytes, hex */
    const char *kinds;  /* per operand: T S fail-able; anything else fixed */
    const char *ops[7]; /* operand bytes, hex */
    int stmt;           /* no value: the method returns 0 after it */
    int pk;             /* the operands sit inside a PkgLength */
    int want;           /* error of the valid form */
} gop_t;

static const gop_t g_ops[] = {
    {"72", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"74", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"77", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"79", "TTS", {"0A05", "0A02", "60"}, 0, 0, 0},
    {"7A", "TTS", {"0A05", "0A02", "60"}, 0, 0, 0},
    {"7B", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"7C", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"7D", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"7E", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"7F", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"85", "TTS", {"0A05", "0A03", "60"}, 0, 0, 0},
    {"73", "TTS", {"0D61626300", "0D646500", "60"}, 0, 0, 0},
    {"84", "TTS", {"11050A027900", "11050A027900", "60"}, 0, 0, 0},
    {"78", "TTSS", {"0A07", "0A02", "60", "61"}, 0, 0, 0},
    {"80", "TS", {"0A05", "60"}, 0, 0, 0},
    {"81", "TS", {"0A05", "60"}, 0, 0, 0},
    {"82", "TS", {"0A05", "60"}, 0, 0, 0},
    {"96", "TS", {"0A05", "60"}, 0, 0, 0},
    {"97", "TS", {"0A05", "60"}, 0, 0, 0},
    {"98", "TS", {"0A05", "60"}, 0, 0, 0},
    {"99", "TS", {"0D313000", "60"}, 0, 0, 0},
    {"9C", "TTS", {"11060A03010203", "0A02", "60"}, 0, 0, 0},
    {"9D", "TS", {"0A05", "60"}, 0, 0, 0},
    {"9E", "TTTS", {"0D61626300", "0A01", "0A01", "60"}, 0, 0, 0},
    {"83", "T", {"880D6162000000"}, 0, 0, 0},
    {"88", "TTS", {"0D61626300", "0A01", "60"}, 0, 0, 0},
    {"89", "TxTxTT", {"1204020101", "00", "0A01", "00", "0A01", "00"}, 0, 0, 0},
    {"90", "TT", {"0A01", "0A01"}, 0, 0, 0},
    {"91", "TT", {"0A01", "0A01"}, 0, 0, 0},
    {"92", "T", {"0A01"}, 0, 0, 0},
    {"93", "TT", {"0A01", "0A01"}, 0, 0, 0},
    {"94", "TT", {"0A01", "0A01"}, 0, 0, 0},
    {"95", "TT", {"0A01", "0A01"}, 0, 0, 0},
    {"70", "TS", {"0A05", "60"}, 0, 0, 0},
    {"5B28", "TS", {"0A15", "60"}, 0, 0, 0},
    {"5B29", "TS", {"0A15", "60"}, 0, 0, 0},
    {"5B21", "T", {"0A01"}, 1, 0, 0},
    {"5B22", "T", {"0A01"}, 1, 0, 0},
    {"5B25", "ST", {"45563030", "0A00"}, 0, 0, 0},
    {"5B23", "Sx", {"4D583030", "0000"}, 0, 0, 0},
    {"5B27", "S", {"4D583030"}, 1, 0, 0},
    {"5B24", "S", {"45563030"}, 1, 0, 0},
    {"5B26", "S", {"45563030"}, 1, 0, 0},
    {"86", "ST", {"44455630", "0A80"}, 1, 0, 0},
    {"8A", "TTx", {"11070A0401020304", "00", "46444131"}, 1, 0, 0},
    {"8B", "TTx", {"11070A0401020304", "00", "46444132"}, 1, 0, 0},
    {"8C", "TTx", {"11070A0401020304", "00", "46444133"}, 1, 0, 0},
    {"8D", "TTx", {"11070A0401020304", "00", "46444134"}, 1, 0, 0},
    {"8F", "TTx", {"110B0A080102030405060708", "00", "46444135"}, 1, 0, 0},
    {"5B13", "TTTx", {"11070A0401020304", "00", "0A08", "46444136"}, 1, 0, 0},
    {"11", "Tx", {"0A03", "010203"}, 0, 1, 0},
    {"13", "Tx", {"0A02", "0101"}, 0, 1, 0},
    {"A0", "Tx", {"0A01", "A3"}, 1, 1, 0},
    {"A2", "Tx", {"0A01", "A5"}, 1, 1, 0},
    {"08", "xT", {"4E4D3030", "0A05"}, 1, 0, 0},
    {"5B80", "xxTT", {"52473030", "00", "0A00", "0A10"}, 1, 0, 0},
    {"5B32", "xxT", {"01", "02000000", "0A00"}, 1, 0, AML_EFATAL},
    {"5B1F", "TTTTTT", {"0D5858585800", "0D00", "0D00", "0D00", "0D00", "00"}, 0, 0, 0},
    {"5B12", "xS", {"45563030", "60"}, 0, 0, 0},
    {"75", "S", {"434E5430"}, 0, 0, 0},
    {"76", "S", {"434E5430"}, 0, 0, 0},
    {"87", "S", {"53545230"}, 0, 0, 0},
    {"71", "S", {"53545230"}, 0, 0, 0},
    {"A4", "T", {"0A05"}, 0, 0, 0},
    {"4D544843", "TT", {"0D61626300", "0A01"}, 0, 0, 0},
    {"4D544844", "TTT", {"0D61626300", "0D646500", "0A01"}, 0, 0, 0},
};

static uint32_t unhex(const char *h, uint8_t *o)
{
    uint32_t n = 0;
    for (; h[0] && h[1]; h += 2) {
        unsigned v;
        sscanf(h, "%2x", &v);
        o[n++] = (uint8_t) v;
    }
    return n;
}

/* the term of operator i with operand bad (-1: none) replaced by ZZZZ */
static uint32_t g_term(uint32_t i, int bad, uint8_t *o)
{
    const gop_t *g = &g_ops[i];
    uint8_t body[256];
    uint32_t n = 0, k = unhex(g->op, o);
    for (int j = 0; g->kinds[j]; j++)
        n += j == bad ? unhex("5A5A5A5A", body + n) : unhex(g->ops[j], body + n);
    if (g->pk) k += pkglen(o + k, n);
    memcpy(o + k, body, n);
    return k + n;
}

static void test_operand_errors(void)
{
    char name[5];
    uint32_t nm = 0;
    hk = 0;
    E(0x5B, 0x02, 'E', 'V', '0', '0');       /* Event (EV00) */
    E(0x5B, 0x01, 'M', 'X', '0', '0', 0x00); /* Mutex (MX00) */
    E(0x5B, 0x82, 0x0B, 'D', 'E', 'V', '0', 0x08, '_', 'A', 'D', 'R', 0x00);
    E(0x08, 'C', 'N', 'T', '0', 0x00);                 /* Name (CNT0, 0) */
    E(0x08, 'S', 'T', 'R', '0', 0x0D, 'a', 'b', 0x00); /* Name (STR0, "ab") */
    for (uint32_t i = 0; i < sizeof g_ops / sizeof g_ops[0]; i++) {
        for (int bad = -1; g_ops[i].kinds[bad < 0 ? 0 : bad]; bad++) {
            if (bad >= 0 && g_ops[i].kinds[bad] != 'T' && g_ops[i].kinds[bad] != 'S') continue;
            uint8_t b[300];
            uint32_t n = 0;
            if (!g_ops[i].stmt && g_ops[i].op[0] != 'A') b[n++] = 0xA4;
            n += g_term(i, bad, b + n);
            if (g_ops[i].stmt) n += unhex("A40A00", b + n);
            snprintf(name, sizeof name, "G%03X", nm++);
            emit_method(name, 0, b, n);
        }
    }
    /* a method read through a reference runs only when it takes no arguments */
    M("MTHA", 1, 0xA4, 0x68);
    M("MTHB", 0, 0xA4, 0x0A, 0x42);
    M("MTHC", 2, 0xA4, 0x68);
    M("MTHD", 3, 0xA4, 0x68);
    M("RDM1", 0, 0xA4, 0x83, 0x71, 'M', 'T', 'H', 'A');
    M("RDM2", 0, 0xA4, 0x83, 0x71, 'M', 'T', 'H', 'B');
    /* Package (1) {1, "abc"}: the extra element is dropped */
    M("X001", 0, 0xA4, 0x12, 0x09, 0x01, 0x0A, 0x01, 0x0D, 'a', 'b', 'c', 0x00);
    M("X002", 0, 0xA4, 0x72, 0x0A, 0x01, 0x0A, 0x02, 'M', 'X', '0', '0'); /* Add into a mutex */
    {
        /* Package () {NOPE, \_SB.NOPX, ^^NOPY, \AAAA.BBBB, \QQQA...QQQM}: names
         * that resolve to nothing become their text, cut to 63 characters */
        uint8_t pk[128];
        uint32_t n = 0;
        n += unhex("05"
                   "4E4F5045"
                   "5C2E5F53425F4E4F5058"
                   "5E5E4E4F5059"
                   "5C2E4141414142424242"
                   "5C2F0D",
                   pk);
        for (int i = 0; i < 13; i++) {
            pk[n++] = 'Q';
            pk[n++] = 'Q';
            pk[n++] = 'Q';
            pk[n++] = (uint8_t) ('A' + i);
        }
        uint8_t b[160];
        uint32_t k = 0;
        b[k++] = 0xA4;
        b[k++] = 0x12;
        k += pkglen(b + k, n);
        memcpy(b + k, pk, n);
        emit_method("X003", 0, b, k + n);
    }
    M("BRK1", 0, 0xA5);
    M("ARGX", 1, 0xA4, 0x8E, 0x69);       /* ObjectType (Arg1) of a one-argument method */
    M("VPK1", 1, 0xA4, 0x13, 0x02, 0x68); /* VarPackage (Arg0) {} */
    M("RDBG", 0, 0xA4, 0x71, 0x5B, 0x31); /* RefOf (Debug) */
    /* Index on a Local, an Arg and a name starting with '_' */
    M("IXL7", 0, 0x70, 0x11, 0x05, 0x0A, 0x02, 0x01, 0x02, 0x67, 0xA4, 0x83, 0x88, 0x67, 0x01,
      0x00);
    M("IXA6", 7, 0xA4, 0x83, 0x88, 0x6E, 0x01, 0x00);
    E(0x08, '_', 'B', 'F', '0', 0x11, 0x05, 0x0A, 0x02, 0x07, 0x08);
    M("IXUS", 0, 0xA4, 0x83, 0x88, '_', 'B', 'F', '0', 0x01, 0x00);
    E(0x08, 'R', 'B', 'F', '2', 0x11, 0x05, 0x0A, 0x02, 0x00, 0x00);
    M("IXRF", 0, 0x70, 0x71, 'R', 'B', 'F', '2', 0x60, 0x70, 0x0A, 0x55, 0x88, 0x60, 0x00, 0x00,
      0xA4, 'R', 'B', 'F', '2');
    M("IXLA", 1, 0x70, 0x11, 0x05, 0x0A, 0x02, 0x01, 0x02, 0x60, 0xA4, 0x72, 0x83, 0x88, 0x60, 0x01,
      0x00, 0x83, 0x88, 0x68, 0x00, 0x00, 0x00);
    /* Add into a string: converted, not replaced */
    E(0x08, 'N', 'S', 'T', '2', 0x0D, 'z', 'z', 0x00);
    M("FNSC", 0, 0x72, 0x0A, 0x01, 0x0A, 0x02, 'N', 'S', 'T', '2', 0xA4, 'N', 'S', 'T', '2');
    /* RefOf (Index (RBF3, 1)) */
    E(0x08, 'R', 'B', 'F', '3', 0x11, 0x06, 0x0A, 0x03, 0x04, 0x05, 0x06);
    M("RFIX", 0, 0x70, 0x71, 0x88, 'R', 'B', 'F', '3', 0x01, 0x00, 0x60, 0xA4, 0x83, 0x60);
    /* Divide (7, 2, NST3): the remainder is converted into the string */
    E(0x08, 'N', 'S', 'T', '3', 0x0D, 'z', 'z', 0x00);
    M("DVNS", 0, 0x78, 0x0A, 0x07, 0x0A, 0x02, 'N', 'S', 'T', '3', 0x00, 0xA4, 'N', 'S', 'T', '3');
    /* Match with an operator past 5 over a package of no data: still refused */
    M("MTC1", 0, 0xA4, 0x89, 0x12, 0x06, 0x01, 'D', 'E', 'V', '0', 0x00, 0x00, 0x06, 0x00, 0x00);
    M("MTC2", 0, 0xA4, 0x89, 0x12, 0x06, 0x01, 'D', 'E', 'V', '0', 0x06, 0x00, 0x00, 0x00, 0x00);
    M("MTC3", 0, 0xA4, 0x89, 0x12, 0x06, 0x01, 'D', 'E', 'V', '0', 0x00, 0x00, 0x00, 0x00, 0x00);
    M("OCRT", 2, 0xA4, 0x84, 0x68, 0x69, 0x00);
    /* Notify: the objects that take one, by name and through a reference */
    E(0x5B, 0x83, 0x0B, 'C', 'P', 'U', '0', 0x01, 0x00, 0x00, 0x00, 0x00, 0x00);
    E(0x5B, 0x84, 0x08, 'P', 'W', 'R', '0', 0x00, 0x00, 0x00);
    E(0x5B, 0x85, 0x05, 'T', 'Z', '0', '0');
    M("NT01", 0, 0x86, 'C', 'P', 'U', '0', 0x01, 0x86, 'P', 'W', 'R', '0', 0x0A, 0x02, 0x86, 'T',
      'Z', '0', '0', 0x0A, 0x03, 0x86, 0x5C, '_', 'T', 'Z', '_', 0x0A, 0x04, 0x86, 0x71, 'D', 'E',
      'V', '0', 0x0A, 0x06, 0xA4, 0x00);
    M("NT02", 0, 0x86, 'M', 'X', '0', '0', 0x0A, 0x05);
    M("NT03", 0, 0x86, 'S', 'T', 'R', '0', 0x0A, 0x05);
    M("NT04", 0, 0x86, 0x88, 0x12, 0x06, 0x01, 'D', 'E', 'V', '0', 0x00, 0x00, 0x0A, 0x05);
    /* the global lock: the platform hook once each way, however deep */
    M("GLA_", 0, 0xA4, 0x5B, 0x23, 0x5C, '_', 'G', 'L', '_', 0xFF, 0xFF);
    M("GLR_", 0, 0x5B, 0x27, 0x5C, '_', 'G', 'L', '_', 0xA4, 0x00);
    M("MXA_", 0, 0xA4, 0x5B, 0x23, 'M', 'X', '0', '0', 0xFF, 0xFF);
    M("MXR_", 0, 0x5B, 0x27, 'M', 'X', '0', '0', 0xA4, 0x00);
    /* operands of the wrong kind */
    M("SZ00", 0, 0xA4, 0x87, 0x00);
    M("OT00", 0, 0xA4, 0x8E, 0x00);
    M("IN00", 0, 0xA4, 0x75, 0x00);
    M("IN01", 0, 0xA4, 0x75, 'C', 'N', 'T', '0');
    M("AQ00", 0, 0xA4, 0x5B, 0x23, 0x00, 0xFF, 0xFF);
    M("AQ01", 0, 0xA4, 0x5B, 0x23, 'C', 'N', 'T', '0', 0xFF, 0xFF);
    M("WT00", 0, 0xA4, 0x5B, 0x25, 0x00, 0x00);
    M("WT01", 0, 0xA4, 0x5B, 0x25, 'C', 'N', 'T', '0', 0x00);
    M("SG01", 0, 0x5B, 0x24, 'C', 'N', 'T', '0', 0xA4, 0x00);
    M("CR01", 0, 0xA4, 0x5B, 0x12, ZZ, ZZ);
    M("CR02", 0, 0xA4, 0x5B, 0x12, 0x5B, 0x31, 0x60);
    M("CR03", 0, 0xA4, 0x5B, 0x12, ZZ, 0x00);
    M("CR04", 0, 0xA4, 0x5B, 0x12, 'C', 'N', 'T', '0', 0x00);
    M("LT01", 0, 0x70, 0x5B, 0x1F, 0x0D, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x0A,
      0x07, 0x60, 0xA4, 0x60);
    M("NF01", 0, 0xA4, ZZ);
    CHECK_EQ(load_hand(2), 0);
    {
        uint64_t x;
        CHECK_EQ(ev_int(&c, "SZ00", &x), AML_EPARSE);
        CHECK_EQ(ev_int(&c, "OT00", &x), AML_EPARSE);
        CHECK_EQ(ev_int(&c, "IN00", &x), AML_EPARSE);
        CHECK_EQ(ev_int(&c, "IN01", &x), 0);
        CHECK_EQ(x, 1);
        CHECK_EQ(ev_int(&c, "AQ00", &x), AML_EPARSE);
        CHECK_EQ(ev_int(&c, "AQ01", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "WT00", &x), AML_EPARSE);
        CHECK_EQ(ev_int(&c, "WT01", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "SG01", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "CR01", &x), AML_ENOENT);
        CHECK_EQ(ev_int(&c, "CR02", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "CR03", &x), 0);
        CHECK_EQ(x, 0);
        CHECK_EQ(ev_int(&c, "CR04", &x), 0);
        CHECK_EQ(x, ONES);
        fk_log_clear(&f);
        CHECK_EQ(ev_int(&c, "LT01", &x), 0);
        CHECK_EQ(x, 0);
        CHECK_EQ(f.nlog, 1);
        CHECK(strcmp(f.log[0], "[AML] LoadTable not supported; returning Zero") == 0);
        fk_log_clear(&f);
        CHECK_EQ(ev_int(&c, "NF01", &x), AML_ENOENT);
        CHECK(strcmp(f.log[0], "[AML] name not found: ZZZZ") == 0);
    }
    {
        static const char *const nn[] = {"\\CPU0", "\\PWR0", "\\TZ00", "\\_TZ_", "\\DEV0"};
        static const uint32_t nv[] = {1, 2, 3, 4, 6};
        aml_notify_t q;
        uint64_t x;
        fk_log_clear(&f);
        CHECK_EQ(ev_int(&c, "NT01", &x), 0);
        for (int i = 0; i < 5; i++) {
            char p[64];
            CHECK_EQ(aml_notify_pop(&c, &q), 1);
            aml_node_path(q.node, p, sizeof p);
            CHECK(strcmp(p, nn[i]) == 0);
            CHECK_EQ(q.value, nv[i]);
        }
        CHECK_EQ(aml_notify_pop(&c, &q), 0);
        CHECK(strcmp(f.log[0], "[AML] Notify \\CPU0 0x1") == 0);
        CHECK(strcmp(f.log[4], "[AML] Notify \\DEV0 0x6") == 0);
        CHECK_EQ(ev_int(&c, "NT02", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "NT03", &x), AML_ETYPE);
        CHECK_EQ(ev_int(&c, "NT04", &x), AML_ETYPE);
        CHECK_EQ(aml_notify_pop(&c, &q), 0);
        CHECK_EQ(ev_int(&c, "GLA_", &x), 0);
        CHECK_EQ(x, 0);
        CHECK_EQ(ev_int(&c, "GLA_", &x), 0);
        CHECK_EQ(ev_int(&c, "MXA_", &x), 0);
        CHECK_EQ(f.gl_acq, 1);
        CHECK_EQ(c.gl_count, 2);
        CHECK_EQ(ev_int(&c, "MXR_", &x), 0);
        CHECK_EQ(ev_int(&c, "GLR_", &x), 0);
        CHECK_EQ(f.gl_rel, 0);
        CHECK_EQ(ev_int(&c, "GLR_", &x), 0);
        CHECK_EQ(f.gl_rel, 1);
        CHECK_EQ(c.gl_count, 0);
        CHECK_EQ(ev_int(&c, "GLR_", &x), AML_EMUTEX);
        CHECK_EQ(ev_int(&c, "MXR_", &x), AML_EMUTEX);
        f.gl_fail = 1;
        CHECK_EQ(ev_int(&c, "GLA_", &x), 0);
        CHECK_EQ(x, ONES);
        CHECK_EQ(c.gl_count, 0);
        CHECK_EQ(ev_int(&c, "GLR_", &x), AML_EMUTEX);
        CHECK_EQ(f.gl_acq, 1);
        CHECK_EQ(f.gl_rel, 1);
        f.gl_fail = 0;
    }
    uint64_t v;
    aml_val_t r;
    CHECK_EQ(aml_eval(&c, 0, "DVNS", 0, 0, &r), 0);
    CHECK(r.type == AML_T_STR && strcmp((const char *) r.u.bytes, "0000000000000001") == 0);
    aml_val_free(&c, &r);
    CHECK_EQ(ev_int(&c, "MTC1", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "MTC2", &v), AML_EPARSE);
    CHECK_EQ(ev_int(&c, "MTC3", &v), 0);
    CHECK_EQ(v, ONES);
    {
        /* resource templates: a large descriptor of 0x105 bytes, one with an
         * empty body exactly at the end, one cut inside its header */
        static uint8_t big[3 + 0x105 + 2];
        big[0] = 0x86;
        big[1] = 0x05;
        big[2] = 0x01;
        for (uint32_t i = 3; i < 3 + 0x105; i++) big[i] = (uint8_t) i;
        big[3 + 0x105] = 0x79;
        aml_val_t a[2] = {aml_buf(&c, big, sizeof big), aml_buf(&c, "\x79\x00", 2)};
        CHECK_EQ(aml_eval(&c, 0, "OCRT", a, 2, &r), 0);
        CHECK(r.type == AML_T_BUF && r.len == sizeof big &&
              memcmp(r.u.bytes, big, sizeof big) == 0);
        aml_val_free(&c, &r);
        aml_val_free(&c, &a[0]);
        aml_val_free(&c, &a[1]);
        static const struct {
            uint8_t in[4];
            uint32_t n, want;
        } rt[] = {
            {{0x86, 0x00, 0x00}, 3, 3},
            {{0x86, 0x00}, 2, 0},
            {{0x86, 0x01, 0x00}, 3, 0},
            {{0x22, 0x01, 0x02}, 3, 3},
            {{0x22, 0x01}, 2, 0},
            {{0x79, 0x00, 0x22}, 3, 0},
            {{0x0A, 0x01, 0x02}, 3, 3},
            {{0x7F, 0x01, 0x02}, 3, 0},
            {{0x01, 0x05, 0x22, 0x01}, 4, 2},
        };
        for (uint32_t i = 0; i < sizeof rt / sizeof rt[0]; i++) {
            a[0] = aml_buf(&c, rt[i].in, rt[i].n);
            a[1] = aml_buf(&c, "\x79\x00", 2);
            CHECK_EQ(aml_eval(&c, 0, "OCRT", a, 2, &r), 0);
            CHECK_EQ(r.len, rt[i].want + 2);
            CHECK(r.len == rt[i].want + 2 && memcmp(r.u.bytes, rt[i].in, rt[i].want) == 0 &&
                  r.u.bytes[rt[i].want] == 0x79 && r.u.bytes[rt[i].want + 1] == 0);
            aml_val_free(&c, &r);
            aml_val_free(&c, &a[0]);
            aml_val_free(&c, &a[1]);
        }
    }
    CHECK_EQ(ev_int(&c, "RDM1", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "RDM2", &v), 0);
    CHECK_EQ(v, 0x42);
    uint32_t h = heap();
    CHECK_EQ(aml_eval(&c, 0, "X001", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_PKG);
    CHECK_EQ(r.len, 1);
    aml_val_free(&c, &r);
    CHECK_EQ(heap(), h);
    CHECK_EQ(ev_int(&c, "X002", &v), AML_ETYPE);
    CHECK_EQ(aml_eval(&c, 0, "X003", 0, 0, &r), 0);
    {
        static const char *want[] = {
            "NOPE", "\\_SB_.NOPX", "^^NOPY", "\\AAAA.BBBB",
            "\\QQQA.QQQB.QQQC.QQQD.QQQE.QQQF.QQQG.QQQH.QQQI.QQQJ.QQQK.QQQL.QQ"};
        CHECK_EQ(r.len, 5);
        for (uint32_t i = 0; i < 5 && i < r.len; i++) {
            const aml_val_t *e = aml_pkg_elem(&r, i);
            CHECK_EQ(e->type, AML_T_STR);
            CHECK(e->type == AML_T_STR && strcmp((const char *) e->u.bytes, want[i]) == 0);
        }
        CHECK_EQ(strlen(want[4]), 63);
    }
    aml_val_free(&c, &r);
    c.last_error = 0;
    CHECK_EQ(ev_int(&c, "BRK1", &v), AML_EPARSE);
    CHECK_EQ(c.last_error, AML_EPARSE);
    CHECK(c.last_error_where && strcmp(c.last_error_where, "method") == 0);
    {
        aml_val_t a[2] = {aml_int(1), aml_str(&c, "x")};
        CHECK_EQ(aml_eval(&c, 0, "ARGX", a, 2, &r), 0);
        CHECK_EQ(r.u.i, 0);
        aml_val_free(&c, &a[0]);
        aml_val_free(&c, &a[1]);
    }
    {
        aml_val_t a = aml_int(AML_MAX_PKG);
        h = heap();
        CHECK_EQ(aml_eval(&c, 0, "VPK1", &a, 1, &r), 0);
        CHECK_EQ(r.len, AML_MAX_PKG);
        aml_val_free(&c, &r);
        a = aml_int(AML_MAX_PKG + 1);
        CHECK_EQ(aml_eval(&c, 0, "VPK1", &a, 1, &r), AML_ENOMEM);
        CHECK_EQ(heap(), h);
    }
    CHECK_EQ(ev_int(&c, "RDBG", &v), AML_ETYPE);
    CHECK_EQ(ev_int(&c, "IXL7", &v), 0);
    CHECK_EQ(v, 2);
    {
        aml_val_t a[7];
        for (int i = 0; i < 6; i++) a[i] = aml_int(0);
        a[6] = aml_buf(&c, "\x05\x06", 2);
        CHECK_EQ(aml_eval(&c, 0, "IXA6", a, 7, &r), 0);
        CHECK_EQ(r.u.i, 6);
        aml_val_free(&c, &a[6]);
    }
    CHECK_EQ(ev_int(&c, "IXUS", &v), 0);
    CHECK_EQ(v, 8);
    CHECK_EQ(aml_eval(&c, 0, "IXRF", 0, 0, &r), 0);
    CHECK(r.type == AML_T_BUF && r.len == 2 && r.u.bytes[0] == 0x55 && r.u.bytes[1] == 0);
    aml_val_free(&c, &r);
    {
        aml_val_t a = aml_buf(&c, "\x10\x20", 2);
        CHECK_EQ(aml_eval(&c, 0, "IXLA", &a, 1, &r), 0);
        CHECK_EQ(r.u.i, 0x12);
        aml_val_free(&c, &a);
    }
    CHECK_EQ(aml_eval(&c, 0, "FNSC", 0, 0, &r), 0);
    CHECK(r.type == AML_T_STR && strcmp((const char *) r.u.bytes, "0000000000000003") == 0);
    aml_val_free(&c, &r);
    CHECK_EQ(ev_int(&c, "RFIX", &v), 0);
    CHECK_EQ(v, 5);

    /* aml_call takes its arguments: freed on every error path, and only
     * the first argc of them */
    {
        aml_node_t *mc = aml_lookup(&c, 0, "MTHC"), *osi = aml_lookup(&c, 0, "_OSI");
        aml_val_t a[8], keep = aml_str(&c, "keep");
        h = heap();
        for (int i = 0; i < 7; i++) a[i] = aml_str(&c, "abc");
        a[7] = aml_int(1);
        CHECK_EQ(aml_call(&c, mc, a, 8, &r), AML_ETYPE);
        CHECK_EQ(heap(), h);
        a[0] = aml_str(&c, "abc");
        CHECK_EQ(aml_call(&c, aml_lookup(&c, 0, "CNT0"), a, 1, &r), AML_ETYPE);
        CHECK_EQ(heap(), h);
        a[0] = aml_str(&c, "Linux");
        a[1] = keep;
        CHECK_EQ(aml_call(&c, osi, a, 1, &r), 0);
        CHECK_EQ(heap(), h);
        CHECK(strcmp((const char *) keep.u.bytes, "keep") == 0);
        a[0] = aml_str(&c, "abc");
        a[1] = aml_str(&c, "abc");
        c.depth = AML_MAX_CALL_DEPTH;
        CHECK_EQ(aml_call(&c, mc, a, 1, &r), AML_ELIMIT);
        c.depth = 0;
        aml_val_free(&c, &a[1]);
        CHECK_EQ(heap(), h);
        aml_val_free(&c, &keep);
    }
    nm = 0;
    for (uint32_t i = 0; i < sizeof g_ops / sizeof g_ops[0]; i++) {
        for (int bad = -1; g_ops[i].kinds[bad < 0 ? 0 : bad]; bad++) {
            if (bad >= 0 && g_ops[i].kinds[bad] != 'T' && g_ops[i].kinds[bad] != 'S') continue;
            snprintf(name, sizeof name, "G%03X", nm++);
            int e = 0;
            for (int pass = 0; pass < 2; pass++) {
                h = heap();
                e = aml_eval(&c, 0, name, 0, 0, &r);
                if (!e) aml_val_free(&c, &r);
            }
            int want = bad < 0 ? g_ops[i].want : AML_ENOENT;
            g_checks++;
            if (e != want || c.depth || c.nest || heap() != h) {
                g_fails++;
                printf("FAIL operand errors: op %s operand %d: err %d, want %d, heap %d\n",
                       g_ops[i].op, bad, e, want, (int) (heap() - h));
            }
        }
    }
}

/* Every prefix of a table body, each in a heap block of exactly the table's
 * size, so a read one byte past the end is caught by the address sanitizer. */
static void load_cuts(const uint8_t *body, uint32_t blen, uint8_t rev)
{
    uint32_t bad = 0;
    for (uint32_t n = 0; n <= blen; n++) {
        uint8_t *t = malloc(36 + n);
        if (!t || fk_table(t, 36 + n, "DSDT", rev, body, n) != 36 + n) {
            free(t);
            bad++;
            continue;
        }
        fk_init(&f);
        bad += aml_init(&c, &f.plat, g_arena, ARENA_SIZE) != 0;
        int r = aml_load_table(&c, t, 36 + n);
        bad += r > 0 || r < AML_EMUTEX || c.depth || c.nest || c.top;
        free(t);
    }
    CHECK_EQ(bad, 0);
}

/* Every method of the loaded tables run with each prefix of its body, the
 * prefix in a heap block of exactly its size: the evaluator must stop at the
 * end of the method without reading a byte past it, and unwind cleanly. */
static aml_node_t *g_meth[512];
static uint32_t g_nmeth;
static int collect_methods(aml_ctx_t *cc, aml_node_t *n, uint32_t depth, void *user)
{
    (void) cc;
    (void) depth;
    (void) user;
    if (aml_node_type(n) == AML_T_METHOD && n->x.m.code && !n->x.m.native && g_nmeth < 512)
        g_meth[g_nmeth++] = n;
    return 0;
}

static void cut_methods(void)
{
    uint32_t bad = 0, runs = 0;
    g_nmeth = 0;
    aml_walk(&c, 0, collect_methods, 0);
    CHECK(g_nmeth > 20);
    for (uint32_t i = 0; i < g_nmeth; i++) {
        aml_node_t *m = g_meth[i];
        const uint8_t *code = m->x.m.code;
        uint32_t len = m->x.m.len;
        for (uint32_t n = 0; n < len; n++) {
            uint8_t *p = malloc(n ? n : 1);
            if (!p) {
                bad++;
                continue;
            }
            memcpy(p, code, n);
            m->x.m.code = p;
            m->x.m.len = n;
            aml_val_t a[7], out;
            for (int k = 0; k < 7; k++) a[k] = aml_int(1);
            int r = aml_eval_node(&c, m, a, m->x.m.flags & 7u, &out);
            if (!r) aml_val_free(&c, &out);
            bad += r > 0 || r < AML_EMUTEX || c.depth || c.nest;
            runs++;
            m->x.m.code = code;
            m->x.m.len = len;
            free(p);
        }
    }
    CHECK_EQ(bad, 0);
    CHECK(runs > 500);
}

static void test_truncated(void)
{
    hk = 0;
    test_skip_shapes();
    uint32_t n = hk;
    load_cuts(hb, n, 2);
    load_cuts(fx_core + 36, (uint32_t) sizeof fx_core - 36, 2);
    CHECK_EQ(load(1), 0);
    cut_methods();
    CHECK_EQ(load(0), 0);
    cut_methods();
}

/* ---- names, the public evaluation API, predefined objects ---------------------- */
static uint32_t g_maxline;
static void long_log(const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (n > g_maxline) g_maxline = n;
}

static void test_api(void)
{
    uint64_t v;
    char p[64], nm[5];
    aml_val_t r, a;
    CHECK_EQ(load(1), 0);
    /* _OSI matches whole strings only */
    static const struct {
        const char *s;
        int yes;
    } osi[] = {
        {"Windows 2000", 1},
        {"Windows 2001", 1},
        {"Windows 2001 SP1", 1},
        {"Windows 2022", 1},
        {"Extended Address Space Descriptor", 1},
        {"Windows 2001 SP", 0},
        {"Windows 200", 0},
        {"Windows 20000", 0},
        {"Linux", 0},
        {"", 0},
    };
    for (uint32_t i = 0; i < sizeof osi / sizeof osi[0]; i++) {
        a = VS(osi[i].s);
        CHECK_EQ(call("\\_OSI", &a, 1, &r), 0);
        CHECK_EQ(r.u.i, osi[i].yes ? ONES : 0);
        aml_val_free(&c, &r);
    }
    a = VI(1);
    CHECK_EQ(call("\\_OSI", &a, 1, &r), AML_ETYPE);
    CHECK_EQ(aml_eval(&c, 0, "\\_OSI", 0, 0, &r), AML_ETYPE);
    CHECK_EQ(aml_eval(&c, 0, "\\_OS", 0, 0, &r), 0);
    CHECK(val_is_str(&r, "Microsoft Windows NT"));
    aml_val_free(&c, &r);
    CHECK_EQ(ev_int(&c, "\\_REV", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "\\_GL_")), AML_T_MUTEX);
    CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "\\_SB_")), AML_T_DEVICE);
    CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "\\_OSI")), AML_T_METHOD);
    CHECK(aml_lookup(&c, 0, "\\_PR_") != 0);
    CHECK(aml_lookup(&c, 0, "\\_SI_") != 0);
    CHECK(aml_lookup(&c, 0, "\\_TZ_") != 0);
    CHECK(aml_lookup(&c, 0, "\\_GPE") != 0);

    /* public evaluation */
    CHECK_EQ(aml_eval(0, 0, "OADD", 0, 0, &r), AML_EARG);
    CHECK_EQ(aml_eval(&c, 0, 0, 0, 0, &r), AML_EARG);
    CHECK_EQ(aml_eval(&c, 0, "NOPE", 0, 0, &r), AML_ENOENT);
    static aml_ctx_t blank;
    CHECK_EQ(aml_eval(&blank, 0, "OADD", 0, 0, &r), AML_EARG);
    CHECK_EQ(aml_eval_node(&c, 0, 0, 0, &r), AML_ENOENT);
    CHECK_EQ(ev_int(&c, "HSTR", &v), 0);
    CHECK_EQ(v, 0x1F);
    uint32_t h = heap();
    aml_val_t two[2] = {VI(1), VS("x")};
    CHECK_EQ(aml_eval(&c, 0, "RSTR", two, 2, &r), 0); /* a data object ignores arguments */
    CHECK(val_is_str(&r, "xyz"));
    aml_val_free(&c, &r);
    CHECK_EQ(aml_eval(&c, 0, "OCAT", two, 2, 0), 0); /* result dropped */
    aml_val_free(&c, &two[0]);
    aml_val_free(&c, &two[1]);
    CHECK_EQ(heap(), h);
    /* each top-level evaluation starts with a clean error and budget */
    CHECK_EQ(ev_int(&c, "FTL1", &v), AML_EFATAL);
    CHECK_EQ(c.last_error, AML_EFATAL);
    CHECK_EQ(ev_int(&c, "REF1", &v), 0);
    CHECK_EQ(c.last_error, 0);
    CHECK(c.last_error_where == 0);
    CHECK(c.ops > 0 && c.ops < 20);
    CHECK_EQ(c.top, 0);

    /* names */
    aml_node_t *n = aml_lookup(&c, 0, "\\_SB_.SBV0");
    CHECK(n != 0);
    CHECK(aml_lookup(&c, 0, "\\_SB.SBV0") == n);
    CHECK(aml_lookup(&c, aml_lookup(&c, 0, "\\_SB"), "SBV0") == n);
    CHECK(aml_lookup(&c, n, "^SBV0") == n);
    CHECK(aml_lookup(&c, n, "^^SBV0") == 0);
    CHECK(aml_lookup(&c, n, "^^_SB_.SBV0") == n);
    CHECK(aml_lookup(&c, 0, "\\") == c.root);
    CHECK(aml_lookup(&c, 0, "") == c.root);
    CHECK(aml_lookup(&c, 0, "^") == 0);
    CHECK(aml_lookup(&c, 0, "A.B.C.D.E.F.G.H.I.J.K.L.M.N.O.P.Q") == 0);
    CHECK(aml_lookup(&c, 0, "_SB_.SBV0.") == 0);
    CHECK(aml_lookup(&c, 0, "_SB_.SBV0X") == 0);
    CHECK(aml_lookup(&c, 0, "_SB_.S-V0") == 0);
    CHECK(aml_lookup(0, 0, "_SB_") == 0);
    CHECK_EQ(aml_node_path(n, p, sizeof p), 10);
    CHECK(strcmp(p, "\\_SB_.SBV0") == 0);
    CHECK_EQ(aml_node_path(n, p, 7), 6);
    CHECK(strcmp(p, "\\_SB_.") == 0);
    CHECK_EQ(aml_node_path(n, p, 6), 5);
    CHECK(strcmp(p, "\\_SB_") == 0);
    CHECK_EQ(aml_node_path(n, p, 2), 1);
    CHECK(strcmp(p, "\\") == 0);
    CHECK_EQ(aml_node_path(n, p, 1), 0);
    CHECK_EQ(p[0], 0);
    p[0] = 'z';
    CHECK_EQ(aml_node_path(n, p, 0), 0);
    CHECK_EQ(p[0], 'z');
    CHECK_EQ(aml_node_path(c.root, p, sizeof p), 1);
    CHECK(strcmp(p, "\\") == 0);
    aml_node_name(0, nm);
    CHECK(strcmp(nm, "\\") == 0);
    aml_node_name(n, nm);
    CHECK(strcmp(nm, "SBV0") == 0);
    CHECK(aml_node_parent(0) == 0);
    CHECK(aml_node_parent(n) == aml_lookup(&c, 0, "_SB_"));
    CHECK(aml_ns_child(0, 0) == 0);
    CHECK(aml_ns_resolve(&c, aml_lookup(&c, 0, "AL02")) == n);
    CHECK(aml_ns_resolve(&c, 0) == 0);
    aml_stats_t s = aml_stats(&c);
    CHECK_EQ(s.devices, 2);
    CHECK_EQ(s.tables, 1);
    CHECK_EQ(s.errors, 0);
    CHECK_EQ(s.regions, 0);
    CHECK_EQ(s.heap_used, c.heap_used);
    CHECK_EQ(s.heap_peak, c.heap_peak);
    CHECK_EQ(s.heap_size, c.arena_size);
    uint32_t objs = s.objects, meths = s.methods;
    CHECK_EQ(aml_eval(&c, 0, "NTF3", 0, 0, 0), 0);
    CHECK_EQ(aml_stats(&c).objects, objs);
    CHECK_EQ(aml_stats(&c).methods, meths);
    CHECK_EQ(aml_stats(&c).devices, 2);

    /* log lines stop at 159 characters; no log hook, no platform: no output */
    static char longs[400];
    memset(longs, 'L', sizeof longs - 1);
    f.plat.log = long_log;
    g_maxline = 0;
    aml_logf(&c, longs, 0, 0, -1);
    CHECK_EQ(g_maxline, 159);
    g_maxline = 0;
    aml_logf(&c, "a", "b", 0, 0);
    CHECK_EQ(g_maxline, 11);
    g_maxline = 0;
    aml_logf(&c, "a", 0, ONES, 0);
    CHECK_EQ(g_maxline, 8 + 20);
    g_maxline = 0;
    aml_logf(&c, "a", 0, ONES, 1);
    CHECK_EQ(g_maxline, 8 + 18);
    f.plat.log = 0;
    aml_logf(&c, "a", 0, 0, -1);
    fk_init(&f);
    CHECK_EQ(aml_init(&c, 0, g_arena, ARENA_SIZE), 0);
    CHECK_EQ(aml_load_table(&c, fx_core, sizeof fx_core), 0);
    m64 = 1;
    TI("SLPU", 0, 1, VI(5));
    T0("TIMR", 0, 50);
    T0("DBG1", 0, 1);
    T0("MXA1", 0, 0);
    a = VS("Windows 2015");
    CHECK_EQ(call("\\_OSI", &a, 1, &r), 0);
    CHECK_EQ(r.u.i, ONES);
}

/* fk logs keep only the text; check a few exact lines of aml_logf */
static void test_log_format(void)
{
    CHECK_EQ(setup(&f, &c, 0, 0), 0);
    fk_log_clear(&f);
    aml_logf(&c, "x", 0, 0, 0);
    aml_logf(&c, "x", 0, 0x1F, 1);
    aml_logf(&c, "x", "y", 1234567890, 0);
    aml_logf(&c, "x", 0, 7, -1);
    CHECK_EQ(f.nlog, 4);
    CHECK(strcmp(f.log[0], "[AML] x 0") == 0);
    CHECK(strcmp(f.log[1], "[AML] x 0x1F") == 0);
    CHECK(strcmp(f.log[2], "[AML] x y 1234567890") == 0);
    CHECK(strcmp(f.log[3], "[AML] x") == 0);
}

/* ---- DataTableRegion -------------------------------------------------------------
 * DTR0(sig, oem, tid) maps the loaded table that matches and returns its
 * length field; the OEM ID compares at most 6 characters and the table ID 8. */
#define DTRA 'D', 'T', 'R', 'A'
#define SIG4 0x0D, 'D', 'S', 'D', 'T', 0x00
static void test_dtr(void)
{
    hk = 0;
    E(0x5B, 0x88, 'D', 'T', 'R', 'T', SIG4, 0x0D, 0x00, 0x0D, 0x00);
    M("DTR0", 0x03, 0x5B, 0x88, DTRA, 0x68, 0x69, 0x6A, 0x5B, 0x81, 0x10, DTRA, 0x01, 'D', 'S', 'I',
      'G', 0x20, 'D', 'L', 'E', 'N', 0x20, 0xA4, 'D', 'L', 'E', 'N');
    M("DTR1", 0x03, 0x5B, 0x88, DTRA, 0x68, 0x69, 0x6A, 0x5B, 0x81, 0x10, DTRA, 0x01, 'D', 'S', 'I',
      'G', 0x20, 'D', 'L', 'E', 'N', 0x20, 0xA4, 'D', 'S', 'I', 'G');
    /* DerefOf(Zero) in each operand; a name that cannot be one; a taken name */
    M("DTE0", 0x00, 0x5B, 0x88, DTRA, 0x83, 0x00, 0x0D, 0x00, 0x0D, 0x00);
    M("DTE1", 0x00, 0x5B, 0x88, DTRA, SIG4, 0x83, 0x00, 0x0D, 0x00);
    M("DTE2", 0x00, 0x5B, 0x88, DTRA, SIG4, 0x0D, 0x00, 0x83, 0x00);
    M("DTE3", 0x00, 0x5B, 0x88, '0', 'A', 'B', 'C', SIG4, 0x0D, 0x00, 0x0D, 0x00);
    M("DTE4", 0x00, 0x08, DTRA, 0x00, 0x5B, 0x88, DTRA, SIG4, 0x0D, 0x00, 0x0D, 0x00);
    uint32_t dlen = 36 + hk;
    CHECK_EQ(load_hand(2), 0);
    CHECK_EQ(aml_stats(&c).regions, 1);
    CHECK(aml_lookup(&c, 0, "DTRT") != 0);
    static uint8_t ss[64];
    const uint8_t sbody[] = {0x08, 'S', 'S', 'D', '1', 0x0A, 0x05};
    uint32_t slen = fk_table(ss, sizeof ss, "SSDT", 2, sbody, sizeof sbody);
    memcpy(ss + 10, "OEMSIXTABLEID8", 14);
    ss[9] = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < slen; i++) sum = (uint8_t) (sum + ss[i]);
    ss[9] = (uint8_t) (0u - sum);
    CHECK_EQ(aml_load_table(&c, ss, slen), 0);
    aml_val_t out;
    CHECK_EQ(aml_eval(&c, 0, "DTE4", 0, 0, &out), AML_EEXIST);
    aml_val_t w3[3] = {VS("DSDT"), VS(""), VS("")}; /* grows the node pool once */
    CHECK_EQ(call("DTR0", w3, 3, &out), 0);
    aml_val_free(&c, &out);
    TI("DTR0", 0, dlen, VS("DSDT"), VS(""), VS(""));
    TI("DTR1", 0, 0x54445344, VS("DSDT"), VS(""), VS(""));
    TI("DTR0", 0, dlen, VS("DSDT"), VS("ZXVFK "), VS("AMLTEST "));
    TI("DTR0", 0, dlen, VS("DSDT"), VS("ZXVFK"), VS("AMLTEST"));
    TI("DTR0", 0, dlen, VS("DSDT"), VI(5), VI(6));
    TI("DTR0", AML_ENOENT, 0, VS("DSDT"), VS("ZXVFJ"), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("DSDT"), VS(""), VS("AMLTESU"));
    TI("DTR0", AML_ENOENT, 0, VS("DSD"), VS(""), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("DSDTX"), VS(""), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("DSDU"), VS(""), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("XSDT"), VS(""), VS(""));
    TI("DTR0", AML_ENOENT, 0, VI(0x54445344), VS(""), VS(""));
    TI("DTR0", 0, slen, VS("SSDT"), VS(""), VS(""));
    TI("DTR0", 0, slen, VS("SSDT"), VS("OEMSIX"), VS("TABLEID8"));
    TI("DTR0", 0, slen, VS("SSDT"), VS("OEMSIXQ"), VS("TABLEID8Q"));
    TI("DTR0", AML_ENOENT, 0, VS("SSDT"), VS("OEMSIY"), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("SSDT"), VS("OEMSIX"), VS("TABLEID9"));
    TI("DTR0", AML_ENOENT, 0, VS("SSDT"), VS("ZXVFK"), VS(""));
    TI("DTR0", AML_ENOENT, 0, VS("DSDT"), VS("OEMSIX"), VS(""));
    T0("DTE0", AML_ETYPE, 0);
    T0("DTE1", AML_ETYPE, 0);
    T0("DTE2", AML_ETYPE, 0);
    T0("DTE3", AML_EPARSE, 0);
    T0("DTE4", AML_EEXIST, 0);

    /* field lists that end with the table, loaded from a copy of exactly the
     * table's size: nothing past the list may be read */
    static const struct {
        uint8_t tail[6], n;
        int err;
        uint8_t fld2;
    } fl[] = {
        {{0}, 0, 0, 0},                           /* FLD1 alone */
        {{'A', 'B'}, 2, AML_EPARSE, 0},           /* half a name */
        {{0x02}, 1, AML_EPARSE, 0},               /* ConnectField with nothing after */
        {{'F', 'L', 'D', '2'}, 4, AML_EPARSE, 0}, /* a name without its length */
        {{'F', 'L', 'D', '2', 0x08}, 5, 0, 1},
    };
    for (uint32_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        hk = 0;
        E(0x5B, 0x80, 'O', 'P', 'R', '1', 0x00, 0x0B, 0x00, 0x10, 0x0A, 0x10);
        E(0x5B, 0x81, (uint8_t) (11 + fl[i].n), 'O', 'P', 'R', '1', 0x01, 'F', 'L', 'D', '1', 0x08);
        emit(fl[i].tail, fl[i].n);
        uint8_t *t = malloc(36 + hk);
        CHECK(t != 0);
        if (!t) continue;
        uint32_t n = fk_table(t, 36 + hk, "DSDT", 2, hb, hk);
        fk_init(&f);
        CHECK_EQ(aml_init(&c, &f.plat, g_arena, ARENA_SIZE), 0);
        CHECK_EQ(aml_load_table(&c, t, n), fl[i].err);
        CHECK(aml_lookup(&c, 0, "FLD1") != 0);
        CHECK_EQ(aml_lookup(&c, 0, "FLD2") != 0, fl[i].fld2);
        CHECK_EQ(aml_stats(&c).fields, 1u + fl[i].fld2);
        free(t);
    }

    /* an unknown extended opcode is stepped over as one opcode, both bytes */
    hk = 0;
    E(0x5B, 0xFF, 0x08, 'A', 'F', 'T', '2', 0x01);
    CHECK_EQ(load_hand(2), AML_EPARSE);
    CHECK_EQ(aml_stats(&c).errors, 1);
    CHECK(aml_lookup(&c, 0, "AFT2") != 0);
}

/* ---- odd corners of the term list ------------------------------------------------ */
#define LBT(n3, body)                                                                              \
    E(0x08, 'L', 'B', '0', n3, 0x11, 0x28, 0x0A, 37);                                              \
    fk_table(lbt, sizeof lbt, "SSDT", 2, (const uint8_t[]){body}, 1);                              \
    emit(lbt, 37)
static void test_corners(void)
{
    static uint8_t lbt[37], tt[31][43];
    hk = 0;
    E(0x08, 'S', 'C', 'I', '1', 0x01);
    E(0x08, 'S', 'C', 'B', '0', 0x11, 0x04, 0x0A, 0x01, 0x00);
    E(0x8C, 'S', 'C', 'B', '0', 0x00, 'S', 'C', 'B', '1');
    E(0x5B, 0x80, 'S', 'C', 'R', '0', 0x00, 0x0A, 0x00, 0x0A, 0x04);
    E(0x5B, 0x81, 0x0B, 'S', 'C', 'R', '0', 0x01, 'S', 'C', 'F', '1', 0x08);
    E(0x08, 'L', 'D', 'I', '1', 0x0A, 0x05);
    LBT('3', 0x01);
    LBT('5', 0xA3);
    /* CreateQWordField at a byte index past 0 */
    M("QW01", 0x00, 0x70, 0x11, 0x0C, 0x0A, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x88, 0x60, 0x8F, 0x60, 0x01, 'Q', 'W', '0', 'X', 0xA4, 'Q', 'W', '0', 'X');
    /* Return inside a Device returns; Break inside one is an error */
    M("DVR1", 0x00, 0x5B, 0x82, 0x08, 'D', 'V', 'R', 'A', 0xA4, 0x0A, 0x05, 0xA4, 0x0A, 0x07);
    M("DVB1", 0x00, 0xA2, 0x0D, 0x01, 0x5B, 0x82, 0x06, 'D', 'V', 'B', 'A', 0xA5, 0xA4, 0x0A, 0x03,
      0xA4, 0x0A, 0x04);
    /* Scope() of data, a buffer field, a field */
    M("SCE1", 0x00, 0x10, 0x05, 'S', 'C', 'I', '1');
    M("SCE2", 0x00, 0x10, 0x05, 'S', 'C', 'B', '1');
    M("SCE3", 0x00, 0x10, 0x05, 'S', 'C', 'F', '1');
    /* a region in a method takes its base from Arg0 at once */
    M("RGA1", 0x01, 0x5B, 0x80, 'R', 'G', 'A', 'X', 0x00, 0x68, 0x0A, 0x04, 0x5B, 0x81, 0x0B, 'R',
      'G', 'A', 'X', 0x03, 'R', 'G', 'A', 'F', 0x20, 0xA4, 'R', 'G', 'A', 'F');
    M("RGA2", 0x01, 0x5B, 0x80, 'R', 'G', 'A', 'X', 0x00, 0x68, 0x0A, 0x04, 0x70, 0x0C, 0x00, 0x00,
      0x07, 0x00, 0x68, 0x5B, 0x81, 0x0B, 'R', 'G', 'A', 'X', 0x03, 'R', 'G', 'A', 'F', 0x20, 0xA4,
      'R', 'G', 'A', 'F');
    M("RGE1", 0x00, 0x5B, 0x80, '0', 'G', 'A', 'X', 0x00, 0x0A, 0x00, 0x0A, 0x04);
    /* Return(RefOf(Local0)) of an empty Local */
    M("RRF1", 0x00, 0xA4, 0x71, 0x60);
    /* Load() of an Integer, of no name, into a missing target, with no slot */
    M("LDE1", 0x00, 0xA4, 0x5B, 0x20, 'L', 'D', 'I', '1', 0x00);
    M("LDE2", 0x00, 0xA4, 0x5B, 0x20, '0', 'B', 'C', 'D', 0x00);
    M("LDE3", 0x00, 0xA4, 0x5B, 0x20, 'L', 'B', '0', '3', 'Z', 'Z', 'Z', 'Z');
    M("LDE4", 0x00, 0xA4, 0x5B, 0x20, 'L', 'B', '0', '5', 0x00);
    E(0x08, 'S', 'C', 'B', '4', 0x11, 0x05, 0x0A, 0x02, 0x00, 0x00);
    E(0x8C, 'S', 'C', 'B', '4', 0x01, 'S', 'C', 'B', '5');
    E(0x08, 'S', 'C', 'N', '1', 0x00);
    /* a method read through a reference is called, unless it takes arguments */
    M("M1A1", 0x01, 0xA4, 0x68);
    M("M0A0", 0x00, 0xA4, 0x0A, 0x09);
    M("MDR1", 0x00, 0xA4, 0x83, 0x71, 'M', '1', 'A', '1');
    M("MDR0", 0x00, 0xA4, 0x83, 0x71, 'M', '0', 'A', '0');
    /* a buffer field whose buffer shrank under it */
    M("BFW1", 0x00, 0x9D, 0x11, 0x04, 0x0A, 0x01, 0x00, 'S', 'C', 'B', '4', 0x70, 0x0A, 0x05, 'S',
      'C', 'B', '5');
    /* Store through an Arg holding a reference converts to the target's type */
    M("STA1", 0x01, 0x70, 0x0D, '1', '2', 0x00, 0x68);
    M("STA0", 0x00, 0x70, 0x00, 'S', 'C', 'N', '1', 'S', 'T', 'A', '1', 0x71, 'S', 'C', 'N', '1',
      0xA4, 0x8E, 'S', 'C', 'N', '1');
    /* targets and sources that are not one: 0x6F, \ and a digit, DerefOf a
     * missing name or path */
    M("TGX1", 0x00, 0x70, 0x01, 0x6F);
    M("TGB1", 0x00, 0x70, 0x01, 0x5C, 0x30);
    M("TGD1", 0x00, 0x70, 0x01, 0x83, 'Z', 'Z', 'Z', 'Z');
    M("TGD2", 0x00, 0x70, 0x01, 0x83, 0x0D, 'Z', 'Z', 'Z', 'Z', 0x00);
    M("IXX1", 0x00, 0xA4, 0x83, 0x88, 0x6F, 0x00, 0x00);
    M("IXB1", 0x00, 0xA4, 0x83, 0x88, 0x5C, 0x30, 0x00, 0x00);
    /* Index(Arg6) names the argument in place */
    M("IA6_", 0x07, 0x70, 0x0A, 0x09, 0x88, 0x6E, 0x00, 0x00, 0xA4, 0x83, 0x88, 0x6E, 0x00, 0x00);
    /* dual and multi-segment names (relative to the root, so at table level)
     * as a target, a package element and an Index source; the root by its
     * NullName, and a Name() of it */
    E(0x5B, 0x82, 0x23, 'D', 'V', 'X', '0', 0x08, 'D', 'N', 'I', '1', 0x00, 0x08, 'D', 'N', 'B',
      '1', 0x11, 0x05, 0x0A, 0x02, 0x01, 0x02, 0x5B, 0x82, 0x0B, 'D', 'V', 'X', '1', 0x08, 'D', 'N',
      'I', '2', 0x00);
    E(0x70, 0x0A, 0x05, 0x2E, 'D', 'V', 'X', '0', 'D', 'N', 'I', '1');
    E(0x70, 0x0A, 0x06, 0x2F, 0x03, 'D', 'V', 'X', '0', 'D', 'V', 'X', '1', 'D', 'N', 'I', '2');
    E(0x08, 'D', 'N', 'P', 'K', 0x12, 0x0B, 0x01, 0x2E, 'D', 'V', 'X', '0', 'D', 'N', 'I', '1');
    E(0x08, 'D', 'N', 'R', '1', 0x00);
    E(0x70, 0x83, 0x88, 0x2E, 'D', 'V', 'X', '0', 'D', 'N', 'B', '1', 0x01, 0x00, 'D', 'N', 'R',
      '1');
    M("NUL2", 0x00, 0xA4, 0x8E, 0x5C, 0x00);
    M("NUL3", 0x00, 0x08, 0x5C, 0x00, 0x01);
    /* ToString stops at its limit; a Buffer may be exactly AML_MAX_OBJ bytes */
    M("TSL1", 0x00, 0xA4, 0x9C, 0x11, 0x06, 0x0A, 0x03, 0x41, 0x42, 0x43, 0x0A, 0x02, 0x00);
    M("BMX1", 0x00, 0x70, 0x11, 0x06, 0x0C, 0x00, 0x00, 0x04, 0x00, 0x60, 0xA4, 0x87, 0x60);
    CHECK_EQ(load_hand(2), 0);
    CHECK_EQ(aml_stats(&c).errors, 0);
    T0("MDR1", AML_ETYPE, 0);
    T0("MDR0", 0, 9);
    resized = 1;
    T0("BFW1", AML_EBOUNDS, 0);
    resized = 0;
    T0("STA0", 0, AML_T_INT);
    uint64_t v;
    CHECK_EQ(ev_int(&c, "SCN1", &v), 0);
    CHECK_EQ(v, 0x12);
    T0("TGX1", AML_EPARSE, 0);
    T0("TGB1", AML_EPARSE, 0);
    T0("TGD1", AML_ENOENT, 0);
    T0("TGD2", AML_ENOENT, 0);
    T0("IXX1", AML_EPARSE, 0);
    T0("IXB1", AML_EPARSE, 0);
    TI("IA6_", 0, 9, VI(0), VI(0), VI(0), VI(0), VI(0), VI(0), B(1, 2));
    t_val(__LINE__, "TSL1", 0, 0, AML_T_STR, "AB", 2, heap());
    T0("BMX1", 0, AML_MAX_OBJ);
    CHECK_EQ(ev_int(&c, "DVX0.DNI1", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(ev_int(&c, "DVX0.DVX1.DNI2", &v), 0);
    CHECK_EQ(v, 6);
    CHECK_EQ(ev_int(&c, "DNR1", &v), 0);
    CHECK_EQ(v, 2);
    aml_val_t pk;
    CHECK_EQ(aml_eval(&c, 0, "DNPK", 0, 0, &pk), 0);
    CHECK(pk.type == AML_T_PKG && pk.len == 1 && pk.u.elems[0].type == AML_T_INT &&
          pk.u.elems[0].u.i == 5);
    aml_val_free(&c, &pk);
    T0("NUL2", 0, AML_T_UNINIT);
    T0("NUL3", AML_EPARSE, 0);
    static uint8_t win[4] = {0x78, 0x56, 0x34, 0x12};
    fk_window(&f, 0x60000, win, sizeof win);
    aml_val_t out;
    CHECK_EQ(aml_eval(&c, 0, "DVR1", 0, 0, &out), 0); /* grows the node pool once */
    T0("QW01", 0, 0x8807060504030201ull);
    T0("DVR1", 0, 5);
    T0("DVB1", AML_EPARSE, 0);
    T0("SCE1", AML_ETYPE, 0);
    T0("SCE2", AML_ETYPE, 0);
    T0("SCE3", AML_ETYPE, 0);
    aml_val_t ga = VI(0x60000);
    CHECK_EQ(aml_eval(&c, 0, "RGA1", &ga, 1, &out), 0);
    TI("RGA1", 0, 0x12345678, VI(0x60000));
    TI("RGA2", 0, 0x12345678, VI(0x60000));
    T0("RGE1", AML_EPARSE, 0);
    /* the empty Local is returned as a value, not a reference */
    CHECK_EQ(aml_eval(&c, 0, "RRF1", 0, 0, &out), 0);
    CHECK_EQ(out.type, AML_T_UNINIT);
    T0("LDE1", AML_ETYPE, 0);
    T0("LDE2", AML_EPARSE, 0);
    CHECK_EQ(c.n_tables, 1);
    resized = 1; /* the loaded table's bytes are kept */
    T0("LDE3", AML_ENOENT, 0);
    resized = 0;
    CHECK_EQ(c.n_tables, 2);
    for (uint32_t i = 0; i < 30; i++) {
        const uint8_t b[] = {
            0x08, 'T', (uint8_t) ('0' + i / 10), (uint8_t) ('0' + i % 10), '_', 0x0A, (uint8_t) i};
        uint32_t n = fk_table(tt[i], sizeof tt[i], "SSDT", 2, b, sizeof b);
        CHECK_EQ(aml_load_table(&c, tt[i], n), 0);
    }
    CHECK_EQ(c.n_tables, AML_MAX_TABLES);
    resized = 1; /* bytes a failed Load() read are kept too */
    T0("LDE4", 0, 0);
    resized = 0;
    CHECK_EQ(c.n_tables, AML_MAX_TABLES);
}

/* ---- an exhausted arena ----------------------------------------------------------
 * Every fixture method again, from a snapshot of the loaded namespace, with
 * the free lists emptied and the arena cut short at a series of budgets below
 * what the method needs: each allocation in turn is the one that fails. The
 * method must stop with ENOMEM, unwind every frame and give back every
 * temporary. Loading the table is cut the same way. */
static uint8_t g_snap[ARENA_SIZE];
static aml_ctx_t g_snapc;

static int oom_run(aml_node_t *m, uint32_t budget, aml_val_t *out, uint32_t *used)
{
    c = g_snapc;
    memcpy(g_arena, g_snap, g_snapc.arena_top);
    memset(c.freelist, 0, sizeof c.freelist);
    if (budget != ~0u) c.arena_size = c.arena_top + budget;
    aml_val_t a[7];
    for (int k = 0; k < 7; k++) a[k] = aml_int(1);
    int r = aml_eval_node(&c, m, a, m->x.m.flags & 7u, out);
    *used = c.arena_top - g_snapc.arena_top;
    return r;
}

static void oom_methods(int wide)
{
    CHECK_EQ(load(wide), 0);
    g_nmeth = 0;
    aml_walk(&c, 0, collect_methods, 0);
    g_snapc = c;
    memcpy(g_snap, g_arena, c.arena_top);
    uint32_t bad = 0, other = 0, leak = 0, runs = 0, nomem = 0;
    int dbg = getenv("AML_OOM_DEBUG") != 0;
    for (uint32_t i = 0; i < g_nmeth; i++) {
        aml_node_t *m = g_meth[i];
        aml_val_t out;
        uint32_t need, used;
        int r0 = oom_run(m, ~0u, &out, &need);
        uint32_t h_ok = c.heap_used;
        if (!r0) aml_val_free(&c, &out);
        uint32_t step = need / 256 > 8 ? need / 256 & ~7u : 8;
        for (uint32_t b = 0; b < need; b += step) {
            uint32_t h0 = g_snapc.heap_used;
            int r = oom_run(m, b, &out, &used);
            runs++;
            if (!r) aml_val_free(&c, &out);
            bad += r > 0 || r < AML_EMUTEX || c.depth || c.nest || c.top || used > b;
            if (r == AML_ENOMEM) {
                nomem++;
                if (c.heap_used > h0 && c.heap_used > h_ok) {
                    leak++;
                    if (dbg) {
                        char p[64];
                        aml_node_path(m, p, sizeof p);
                        printf("leak %s b=%u +%u\n", p, b, c.heap_used - h0);
                    }
                }
            } else if (r != r0) {
                other++;
                if (dbg) {
                    char p[64];
                    aml_node_path(m, p, sizeof p);
                    printf("other %s b=%u r=%d r0=%d\n", p, b, r, r0);
                }
            }
        }
    }
    if (dbg)
        printf("oom: methods %u runs %u nomem %u other %u leak %u bad %u\n", g_nmeth, runs, nomem,
               other, leak, bad);
    CHECK_EQ(bad, 0);
    CHECK_EQ(leak, 0);
    CHECK_EQ(other, 0);
    CHECK(nomem > 900);
}

static void oom_load(const uint8_t *t, uint32_t len)
{
    fk_init(&f);
    CHECK_EQ(aml_init(&c, &f.plat, g_arena, ARENA_SIZE), 0);
    uint32_t base = c.arena_top;
    CHECK_EQ(aml_load_table(&c, t, len), 0);
    uint32_t need = c.arena_top - base, bad = 0, nomem = 0, runs = 0;
    int dbg = getenv("AML_OOM_DEBUG") != 0;
    for (uint32_t b = 0; b < need; b += need / 1024 > 8 ? need / 1024 & ~7u : 8) {
        fk_init(&f);
        bad += aml_init(&c, &f.plat, g_arena, ARENA_SIZE) != 0 || c.arena_top != base;
        c.arena_size = base + b;
        int r = aml_load_table(&c, t, len);
        runs++;
        nomem += r == AML_ENOMEM;
        bad += r >= 0 || r < AML_EMUTEX || c.depth || c.nest || c.top || c.loading ||
               c.arena_top > base + b;
        if (dbg && r != AML_ENOMEM) printf("load b=%u r=%d\n", b, r);
    }
    if (dbg) printf("oom load: need %u runs %u nomem %u bad %u\n", need, runs, nomem, bad);
    CHECK_EQ(bad, 0);
    CHECK_EQ(nomem, runs);
}

/* the caller's seven arguments are copied in turn; when copy k fails, the
 * k copies before it are given back */
static void oom_args(void)
{
    CHECK_EQ(load(1), 0);
    aml_val_t a[7];
    for (int k = 0; k < 7; k++) a[k] = VS("arg");
    uint32_t h0 = heap(), size = c.arena_size, fails = 0;
    void *fl[40];
    memcpy(fl, c.freelist, sizeof fl);
    for (uint32_t b = 0; b <= 7 * 16; b += 16) {
        memset(c.freelist, 0, sizeof c.freelist);
        uint32_t top = c.arena_top;
        c.arena_size = top + b;
        aml_val_t out;
        int r = aml_eval(&c, 0, "SRA6", a, 7, &out);
        c.arena_size = size;
        if (!r) aml_val_free(&c, &out);
        fails += r == AML_ENOMEM;
        CHECK(r == AML_ENOMEM || b >= 7 * 16);
        CHECK_EQ(heap(), h0);
        c.arena_top = top; /* drop whatever this round took fresh */
    }
    CHECK_EQ(fails, 8);
    memcpy(c.freelist, fl, sizeof fl);
    for (int k = 0; k < 7; k++) aml_val_free(&c, &a[k]);
}

static void test_oom(void)
{
    oom_args();
    oom_methods(1);
    oom_methods(0);
    oom_load(fx_core, sizeof fx_core);
    oom_load(core32, sizeof core32);
}

int main(void)
{
    memcpy(core32, fx_core, sizeof fx_core);
    core32[8] = 1;
    run_table(1);
    run_table(0);
    test_heap();
    test_values();
    test_hand_methods();
    test_hand_tables();
    test_truncated();
    test_operand_errors();
    test_names_more();
    test_tables_more();
    test_fields_api();
    test_api();
    test_log_format();
    test_dtr();
    test_corners();
    test_oom();
    return finish_tests("test_aml_core");
}
