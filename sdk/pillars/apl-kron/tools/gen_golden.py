# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Golden cases for apl-kron, with expected values computed here, in Python.

The expected value of every case comes from Python's fractions module,
never from libzkron:

  * hand-written cases (test/golden/*.kg) give each program in APL and/or
    K spelling together with a Python expression for its value, written
    by hand from the APL/K meaning of the program;
  * random cases are drawn from a small grammar and evaluated by an
    independent Python model of KRON's semantics (right folds for APL /,
    left folds for K's non-associative /, exact int64/int64 refusal after
    every step, division by zero refused).

The four-valued and/or/not tables are read from the conformance vectors
(sdk/conformance/vectors.json), the same tables every ZXV SDK is held to.

usage: python3 tools/gen_golden.py --out DIR [--seed N] [--random N] FILE.kg...

.kg format (one case):
    === name
    apl: <APL line>          repeatable; the lines form one program
    k: <K line>              repeatable
    canon: <canonical line>  repeatable
    bind: NAME = <python>    a value bound to a free name
    py: <python>             the expected value
    refuse: STATUS           or: the expected refusal
    pyk: / refusek:          the K spelling's own expectation (with differ)
    work: N                  a work limit for the run
    differ: yes              the spellings are different programs
                             (otherwise they must share one digest)

In py: and bind: expressions, numbers are exact: F(1, 3), vec(1, 2),
mat([[1, 2], [3, 4]]), tvec(T, B), and the truth values T, Fa, N, B.
"""
import argparse
import json
import os
import random
import sys
from fractions import Fraction

I64 = (1 << 63) - 1

HERE = os.path.dirname(os.path.abspath(__file__))
VECTORS = os.path.join(HERE, "..", "..", "..", "conformance", "vectors.json")


class Refuse(Exception):
    def __init__(self, status):
        super().__init__(status)
        self.status = status


# ---------------------------------------------------------------- values

class Truth:
    __slots__ = ("code",)
    NAMES = {0: "neither", 1: "true", 2: "false", 3: "both"}

    def __init__(self, code):
        self.code = code

    def __eq__(self, o):
        return isinstance(o, Truth) and o.code == self.code

    def __hash__(self):
        return hash(("t", self.code))

    def __repr__(self):
        return self.NAMES[self.code]


N, T, Fa, B = Truth(0), Truth(1), Truth(2), Truth(3)
F = Fraction


class V:
    """An array: shape () scalar, (n,) vector, (r, c) matrix; items row-major."""
    __slots__ = ("shape", "items")

    def __init__(self, shape, items):
        self.shape = tuple(shape)
        self.items = list(items)
        n = 1
        for s in self.shape:
            n *= s
        assert n == len(self.items), (shape, items)

    @property
    def rank(self):
        return len(self.shape)

    def kind(self):
        if self.items:
            return "truth" if isinstance(self.items[0], Truth) else "num"
        return "num"


def num(x):
    return Fraction(x)


def vec(*xs):
    return V((len(xs),), [x if isinstance(x, Truth) else num(x) for x in xs])


def tvec(*xs):
    return V((len(xs),), list(xs))


def mat(rows):
    r = len(rows)
    c = len(rows[0]) if r else 0
    return V((r, c), [x if isinstance(x, Truth) else num(x) for row in rows for x in row])


def scal(x):
    return V((), [x if isinstance(x, Truth) else num(x)])


def as_v(x):
    if isinstance(x, V):
        return x
    if isinstance(x, (list, tuple)):
        return vec(*x)
    return scal(x)


def fits(q):
    return -I64 <= q.numerator <= I64 and 1 <= q.denominator <= I64


def chk(q):
    if not fits(q):
        raise Refuse("ZXV_E_RANGE")
    return q


def rat_text(q):
    q = Fraction(q)
    return str(q.numerator) if q.denominator == 1 else "%dr%d" % (q.numerator, q.denominator)


def elem_text(x):
    return repr(x) if isinstance(x, Truth) else rat_text(x)


def value_text(v):
    v = as_v(v)
    for x in v.items:
        if not isinstance(x, Truth) and not fits(Fraction(x)):
            raise Refuse("ZXV_E_RANGE")
    if v.rank == 0:
        return elem_text(v.items[0])
    truth = v.kind() == "truth"
    head = ("tvec" if truth else "vec") if v.rank == 1 else (
        "tmat %d %d" % v.shape if truth else "mat %d %d" % v.shape)
    return "(" + " ".join([head] + [elem_text(x) for x in v.items]) + ")"


# ------------------------------------------------- four-valued tables

def load_lp():
    with open(VECTORS, encoding="utf-8") as f:
        lp = json.load(f)["lpres"]
    return lp["and"], lp["or"], lp["not"]


LP_AND, LP_OR, LP_NOT = load_lp()


# ------------------------------------------------ the scalar functions

def f2(fn, a, b):
    ta, tb = isinstance(a, Truth), isinstance(b, Truth)
    if fn in ("eq", "ne"):
        if ta != tb:
            raise Refuse("KR_E_DOMAIN")
        return T if ((a == b) == (fn == "eq")) else Fa
    if fn in ("and", "or") or (fn in ("max", "min") and ta and tb):
        if not (ta and tb):
            raise Refuse("KR_E_DOMAIN")
        tab = LP_AND if fn in ("and", "min") else LP_OR
        return Truth(tab[a.code][b.code])
    if ta or tb:
        raise Refuse("KR_E_DOMAIN")
    if fn == "add":
        return chk(a + b)
    if fn == "sub":
        return chk(a - b)
    if fn == "mul":
        return chk(a * b)
    if fn == "div":
        if b == 0:
            raise Refuse("KR_E_DIVZERO")
        return chk(a / b)
    if fn == "max":
        return a if a >= b else b
    if fn == "min":
        return a if a <= b else b
    if fn in ("lt", "le", "ge", "gt"):
        r = {"lt": a < b, "le": a <= b, "ge": a >= b, "gt": a > b}[fn]
        return T if r else Fa
    raise ValueError(fn)


ASSOC = {"add", "mul", "max", "min", "and", "or"}
IDENT = {"add": F(0), "sub": F(0), "mul": F(1), "div": F(1), "and": T, "or": Fa}


def foldr(fn, xs):
    if not xs:
        if fn not in IDENT:
            raise Refuse("KR_E_DOMAIN")
        return IDENT[fn]
    acc = xs[-1]
    for x in reversed(xs[:-1]):
        acc = f2(fn, x, acc)
    return acc


def foldl(fn, xs):
    if not xs:
        if fn not in IDENT:
            raise Refuse("KR_E_DOMAIN")
        return IDENT[fn]
    acc = xs[0]
    for x in xs[1:]:
        acc = f2(fn, acc, x)
    return acc


def lines(v, last):
    """The lines of v along its last axis (or first), as lists of items."""
    if v.rank == 1:
        return [v.items]
    r, c = v.shape
    if last:
        return [v.items[i * c:(i + 1) * c] for i in range(r)]
    return [[v.items[i * c + j] for i in range(r)] for j in range(c)]


def m_reduce(fn, v, variant):
    """variant: 'last' (APL /), 'first' (APL ⌿), 'left' (K /)."""
    v = as_v(v)
    if v.rank == 0:
        return v
    if variant == "left" and fn in ASSOC:
        variant = "first"
    out = [(foldl if variant == "left" else foldr)(fn, ln) for ln in lines(v, variant == "last")]
    return V((len(out),), out) if v.rank == 2 else V((), out)


def m_scan(fn, v, variant):
    v = as_v(v)
    if v.rank == 0:
        return v
    if variant == "left" and fn in ASSOC:
        variant = "first"
    running = variant == "left" or fn in ASSOC
    res = [None] * len(v.items)
    last = variant == "last" or v.rank == 1
    for li, ln in enumerate(lines(v, last)):
        acc = None
        for k, x in enumerate(ln):
            if k == 0:
                acc = x
            elif running:
                acc = f2(fn, acc, x)
            else:
                acc = foldr(fn, ln[:k + 1])
            if v.rank == 1:
                pos = k
            elif last:
                pos = li * v.shape[1] + k
            else:
                pos = k * v.shape[1] + li
            res[pos] = acc
    return V(v.shape, res)


def m_elementwise(fn, a, b):
    a, b = as_v(a), as_v(b)
    if a.rank and b.rank:
        if a.rank != b.rank:
            raise Refuse("KR_E_RANK")
        if a.shape != b.shape:
            raise Refuse("KR_E_LENGTH")
    shape = a.shape if a.rank >= b.rank else b.shape
    n = len(a.items) if a.rank >= b.rank else len(b.items)
    out = [f2(fn, a.items[i if a.rank else 0], b.items[i if b.rank else 0]) for i in range(n)]
    return V(shape, out)


def m_outer(fn, a, b):
    a, b = as_v(a), as_v(b)
    if a.rank + b.rank > 2:
        raise Refuse("KR_E_RANK")
    out = [f2(fn, x, y) for x in a.items for y in b.items]
    return V(a.shape + b.shape, out)


def m_inner(f, g, a, b):
    a, b = as_v(a), as_v(b)
    k = a.shape[-1]
    if k != b.shape[0]:
        raise Refuse("KR_E_LENGTH")
    rows = a.shape[0] if a.rank == 2 else 1
    cols = b.shape[1] if b.rank == 2 else 1
    out = []
    for i in range(rows):
        for j in range(cols):
            t = [f2(g, a.items[i * k + m], b.items[m * cols + j]) for m in range(k)]
            out.append(foldr(f, t))
    return V(a.shape[:-1] + b.shape[1:], out)


def iota(n):
    return vec(*range(n))


def reshape(shape, v):
    v = as_v(v)
    shape = tuple(shape) if isinstance(shape, (list, tuple)) else (shape,)
    n = 1
    for s in shape:
        n *= s
    items = [v.items[i % len(v.items)] for i in range(n)] if v.items else [F(0)] * n
    return V(shape, items)


def grade(v, down=False):
    v = as_v(v)
    keys = lines(v, True) if v.rank == 2 else [[x] for x in v.items]
    idx = sorted(range(len(keys)), key=lambda i: keys[i], reverse=down)
    if down:  # stable: equal keys keep their order
        idx = sorted(range(len(keys)), key=lambda i: tuple(-x for x in keys[i]))
    return vec(*idx)


# ---------------------------------------------------------- .kg cases

def py_env():
    env = {"F": F, "vec": vec, "tvec": tvec, "mat": mat, "scal": scal, "T": T, "Fa": Fa, "N": N,
           "B": B, "iota": iota, "reshape": reshape, "foldr": foldr, "foldl": foldl,
           "m_reduce": m_reduce, "m_scan": m_scan, "m_outer": m_outer, "m_inner": m_inner,
           "m_elementwise": m_elementwise, "grade": grade, "Refuse": Refuse, "I64": I64,
           "Truth_and": lambda a, b: f2("and", a, b), "Truth_or": lambda a, b: f2("or", a, b), "Truth": Truth}
    return env


def read_kg(path):
    cases, cur = [], None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("==="):
                cur = {"name": line[3:].strip(), "apl": [], "k": [], "canon": [], "bind": [],
                       "py": None, "refuse": None, "differ": False, "file": path, "pyk": None,
                       "refusek": None, "work": None}
                cases.append(cur)
                continue
            if cur is None or not line.strip() or line.lstrip().startswith("#"):
                continue
            key, _, val = line.partition(":")
            key = key.strip()
            val = val[1:] if val.startswith(" ") else val
            if key in ("apl", "k", "canon"):
                cur[key].append(val)
            elif key == "bind":
                cur["bind"].append(val)
            elif key == "py":
                cur["py"] = val
            elif key == "pyk":
                cur["pyk"] = val
            elif key == "refusek":
                cur["refusek"] = val.strip()
            elif key == "work":
                cur["work"] = int(val)
            elif key == "refuse":
                cur["refuse"] = val.strip()
            elif key == "differ":
                cur["differ"] = val.strip() == "yes"
            else:
                raise SystemExit("%s: case %s: unknown key %r" % (path, cur["name"], key))
    return cases


def expect_of(case, spell):
    py, refuse = case["py"], case["refuse"]
    if spell == "k" and (case["pyk"] or case["refusek"]):
        py, refuse = case["pyk"], case["refusek"]
    if refuse:
        return ("refuse", refuse)
    try:
        return ("expect", value_text(eval(py, py_env())))
    except Refuse as r:
        return ("refuse", r.status)


def emit(out, name, spell, binds, exp, prog, same=None, differ=None, work=None):
    out.append("=== %s" % name)
    out.append("spell: %s" % spell)
    if work:
        out.append("work: %d" % work)
    for b in binds:
        out.append("bind: %s" % b)
    out.append("%s: %s" % exp)
    if same:
        out.append("same: %s" % same)
    if differ:
        out.append("differ: %s" % differ)
    out.append("program:")
    if spell == "canon":
        out.append("kron")
    out.extend(prog)


def gen_kg(path, out):
    n = 0
    for c in read_kg(path):
        binds = []
        for b in c["bind"]:
            nm, _, ex = b.partition("=")
            binds.append("%s = %s" % (nm.strip(), value_text(eval(ex, py_env()))))
        group = "%s:%s" % (os.path.basename(path), c["name"])
        first = True
        for spell in ("apl", "k", "canon"):
            if not c[spell]:
                continue
            same = group if (first or not c["differ"]) else None
            differ = group if (not first and c["differ"]) else None
            emit(out, "%s/%s" % (c["name"], spell), spell, binds, expect_of(c, spell), c[spell], same,
                 differ, c["work"])
            first = False
            n += 1
    return n


# ------------------------------------------------------- random cases

SC = [  # (word, APL glyph, K spelling)
    ("add", "+", "+"), ("sub", "-", "-"), ("mul", "×", "*"), ("div", "÷", "%"),
    ("max", "⌈", "|"), ("min", "⌊", "&"), ("lt", "<", "<"), ("le", "≤", " le "),
    ("eq", "=", "="), ("ge", "≥", " ge "), ("gt", ">", ">"), ("ne", "≠", " ne "),
]  # K has no <= >= <> (q's digraphs are two functions in ngn/k): the words
RED = [("add", "+", "+"), ("mul", "×", "*"), ("max", "⌈", "|"), ("min", "⌊", "&"),
       ("sub", "-", "-"), ("div", "÷", "%")]

BIG = [I64, I64 - 1, 1 << 62, 3037000499, 3037000500, 4294967296, 999999999999]


def rnd_rat(rng):
    r = rng.random()
    if r < 0.55:
        return F(rng.randint(-9, 9))
    if r < 0.85:
        return F(rng.randint(-20, 20), rng.randint(1, 12))
    if r < 0.95:
        return F(rng.choice(BIG) * rng.choice([1, -1]))
    return F(rng.choice([1, -1]), rng.choice(BIG))


def apl_num(q):
    s = rat_text(q)
    return s.replace("-", "¯")


def k_num(q):
    return rat_text(q)


def lit(v, apl):
    """Literal text for a scalar or vector (parenthesized)."""
    f = apl_num if apl else k_num
    if v.rank == 0:
        return "(" + f(v.items[0]) + ")"
    if len(v.items) == 0:
        return "⍬" if apl else "()"
    if len(v.items) == 1:  # one element: 1⍴x in both spellings (K has no ,x here)
        return "(1" + ("⍴" if apl else "#") + f(v.items[0]) + ")"
    return "(" + " ".join(f(x) for x in v.items) + ")"


def rnd_vec(rng, n):
    return V((n,), [rnd_rat(rng) for _ in range(n)])


def gen_random(rng, count, out):
    made = 0
    i = 0
    while made < count:
        i += 1
        kind = rng.choice(["elem", "elem", "red", "red", "scan", "outer", "dot", "struct", "bound"])
        n = rng.randint(1, 6)
        name = "r%05d" % i
        binds = []
        try_differ = False
        progs = {}
        try:
            if kind == "elem":
                fn, ag, kg = rng.choice(SC)
                a = rnd_vec(rng, n)
                b = rnd_vec(rng, n) if rng.random() < 0.7 else scal(rnd_rat(rng))
                if rng.random() < 0.3:
                    a, b = b, a
                if a.rank == 0 and b.rank == 0:
                    b = rnd_vec(rng, n)
                progs["apl"] = lit(a, True) + ag + lit(b, True)
                if len(a.items) != 1 or a.rank == 0:
                    if len(b.items) != 1 or b.rank == 0:
                        progs["k"] = lit(a, False) + kg + lit(b, False)
                exp = lambda: m_elementwise(fn, a, b)
            elif kind == "red":
                fn, ag, kg = rng.choice(RED)
                a = rnd_vec(rng, rng.randint(0, 7))
                if fn not in ASSOC:
                    try_differ = True
                progs["apl"] = ag + "/" + lit(a, True)
                if len(a.items) != 1:
                    progs["k"] = kg + "/" + lit(a, False)
                expa = lambda: m_reduce(fn, a, "last")
                expk = lambda: m_reduce(fn, a, "left")
                exp = None
            elif kind == "scan":
                fn, ag, kg = rng.choice(RED)
                a = rnd_vec(rng, rng.randint(1, 6))
                if fn not in ASSOC:
                    try_differ = True
                progs["apl"] = ag + "\\" + lit(a, True)
                if len(a.items) != 1:
                    progs["k"] = kg + "\\" + lit(a, False)
                expa = lambda: m_scan(fn, a, "last")
                expk = lambda: m_scan(fn, a, "left")
                exp = None
            elif kind == "outer":
                fn, ag, kg = rng.choice(SC)
                a = rnd_vec(rng, rng.randint(2, 4))
                b = rnd_vec(rng, rng.randint(2, 4))
                progs["apl"] = lit(a, True) + "∘." + ag + lit(b, True)
                progs["k"] = lit(a, False) + kg.rstrip() + "\\:" + lit(b, False)
                exp = lambda: m_outer(fn, a, b)
            elif kind == "dot":
                a = rnd_vec(rng, n)
                b = rnd_vec(rng, n)
                if rng.random() < 0.5:
                    progs["apl"] = "+/" + lit(a, True) + "×" + lit(b, True)
                    if n != 1:
                        progs["k"] = "+/" + lit(a, False) + "*" + lit(b, False)
                    exp = lambda: m_reduce("add", m_elementwise("mul", a, b), "last")
                else:
                    r, c = rng.randint(1, 3), rng.randint(1, 3)
                    m1 = V((r, n), [rnd_rat(rng) for _ in range(r * n)])
                    m2 = V((n, c), [rnd_rat(rng) for _ in range(n * c)])
                    progs["apl"] = "(%d %d⍴%s)+.×(%d %d⍴%s)" % (
                        r, n, lit(V((r * n,), m1.items), True), n, c, lit(V((n * c,), m2.items), True))
                    exp = lambda: m_inner("add", "mul", m1, m2)
            elif kind == "struct":
                op = rng.choice(["rev", "rot", "take", "drop", "grade", "sort", "index", "find",
                                 "cat", "reshape", "flip", "colsum"])
                a = rnd_vec(rng, rng.randint(1, 6))
                la, lk = lit(a, True), lit(a, False)
                L = len(a.items)
                if op == "rev":
                    progs["apl"], progs["k"] = "⌽" + la, "|" + lk
                    exp = lambda: V(a.shape, a.items[::-1])
                elif op == "rot":
                    k = rng.randint(-8, 8)
                    progs["apl"] = "%s⌽%s" % (apl_num(F(k)), la)
                    progs["k"] = "%d rotate %s" % (k, lk)
                    exp = lambda: V(a.shape, [a.items[(j + k) % L] for j in range(L)])
                elif op in ("take", "drop"):
                    k = rng.randint(-8, 8)
                    progs["apl"] = "%s%s%s" % (apl_num(F(k)), "↑" if op == "take" else "↓", la)
                    if op == "drop":
                        progs["k"] = "%s_%s" % (("(%d)" % k), lk)

                        def exp(k=k):
                            return V((max(0, L - abs(k)),), a.items[k:] if k >= 0 else a.items[:max(0, L + k)])
                    else:
                        progs["k"] = "%d take %s" % (k, lk)

                        def exp(k=k):
                            m = abs(k)
                            src = a.items + [F(0)] * m if k >= 0 else [F(0)] * m + a.items
                            return V((m,), src[:m] if k >= 0 else src[len(src) - m:])
                elif op in ("grade", "sort"):
                    if op == "grade":
                        down = rng.random() < 0.5
                        progs["apl"] = ("⍒" if down else "⍋") + la
                        progs["k"] = (">" if down else "<") + lk
                        exp = lambda: grade(a, down)
                    else:
                        progs["apl"] = "x[⍋x←%s]" % la
                        progs["k"] = "x@<x:%s" % lk
                        exp = lambda: V(a.shape, sorted(a.items))
                elif op == "index":
                    idx = [rng.randint(0, L) for _ in range(rng.randint(1, 4))]
                    iv = vec(*idx)
                    progs["apl"] = la + "[" + lit(iv, True) + "]"
                    progs["k"] = lk + "@" + lit(iv, False)
                    if len(idx) == 1:
                        progs.pop("k")

                    def exp(idx=idx):
                        if any(j >= L for j in idx):
                            raise Refuse("KR_E_INDEX")
                        return V((len(idx),), [a.items[j] for j in idx])
                elif op == "find":
                    b = V((3,), [rng.choice(a.items + [rnd_rat(rng)]) for _ in range(3)])
                    progs["apl"] = la + "⍳" + lit(b, True)
                    progs["k"] = lk + "?" + lit(b, False)
                    exp = lambda: V((3,), [a.items.index(y) if y in a.items else L for y in b.items])
                elif op == "cat":
                    b = rnd_vec(rng, rng.randint(1, 4))
                    progs["apl"] = la + "," + lit(b, True)
                    progs["k"] = lk + "," + lit(b, False)
                    exp = lambda: V((L + len(b.items),), a.items + b.items)
                elif op == "reshape":
                    r, c = rng.randint(0, 3), rng.randint(0, 4)
                    progs["apl"] = "%d %d⍴%s" % (r, c, la)
                    progs["k"] = "%d %d#%s" % (r, c, lk)
                    exp = lambda: reshape((r, c), a)
                elif op == "flip":
                    r, c = rng.randint(1, 3), rng.randint(1, 4)
                    progs["apl"] = "⍉%d %d⍴%s" % (r, c, la)
                    progs["k"] = "+%d %d#%s" % (r, c, lk)

                    def exp(r=r, c=c):
                        m = reshape((r, c), a)
                        return V((c, r), [m.items[i * c + j] for j in range(c) for i in range(r)])
                else:  # colsum: APL +⌿ = K +/ on a matrix
                    r, c = rng.randint(1, 3), rng.randint(1, 4)
                    progs["apl"] = "+⌿%d %d⍴%s" % (r, c, la)
                    progs["k"] = "+/%d %d#%s" % (r, c, lk)
                    exp = lambda: m_reduce("add", reshape((r, c), a), "first")
            else:  # bound: a free x of unknown rank
                fn, ag, kg = rng.choice(RED[:4])
                rows = rng.randint(1, 3)
                cols = rng.randint(0, 4)
                xv = V((rows, cols), [rnd_rat(rng) for _ in range(rows * cols)])
                if rng.random() < 0.5:
                    xv = V((cols,), xv.items[:cols])
                binds.append("x = " + value_text(xv))
                progs["apl"] = ag + "⌿x"
                progs["k"] = kg + "/x"
                exp = lambda: m_reduce(fn, xv, "first")
        except Refuse:
            continue  # a literal that does not fit: draw again

        def result(f):
            try:
                return ("expect", value_text(f()))
            except Refuse as r:
                return ("refuse", r.status)

        group = "random:" + name
        if exp is None:
            ea, ek = result(expa), result(expk)
            emit(out, name + "/apl", "apl", binds, ea, [progs["apl"]], same=group)
            if "k" in progs:
                emit(out, name + "/k", "k", binds, ek, [progs["k"]],
                     same=None if try_differ else group, differ=group if try_differ else None)
        else:
            e = result(exp)
            emit(out, name + "/apl", "apl", binds, e, [progs["apl"]], same=group)
            if "k" in progs:
                emit(out, name + "/k", "k", binds, e, [progs["k"]], same=group)
        made += 1
    return made


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--random", type=int, default=1000)
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    total = 0
    for path in a.files:
        out = ["# generated by tools/gen_golden.py from %s; do not edit" % os.path.basename(path)]
        total += gen_kg(path, out)
        dst = os.path.join(a.out, os.path.splitext(os.path.basename(path))[0] + ".kgold")
        with open(dst, "w", encoding="utf-8") as f:
            f.write("\n".join(out) + "\n")
    out = ["# generated by tools/gen_golden.py --seed %d; do not edit" % a.seed]
    made = gen_random(random.Random(a.seed), a.random, out)
    with open(os.path.join(a.out, "random.kgold"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("gen_golden: %d hand-written cases, %d random programs" % (total, made))


if __name__ == "__main__":
    sys.exit(main())
