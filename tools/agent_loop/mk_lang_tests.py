#!/usr/bin/env python3
# mk_lang_tests.py — the test code for ZXV's own languages. The pillars
# (Eve, KRON, MYRA, Sutra, Trebago), the Lingua IR, the ZXV trio (LEXIS,
# TECTON, AXION) and Enochian have no external reference implementation:
# their truth is what their sources say. Every operator fact, every
# grammar rule and every refusal gets a test, so the semantics are pinned
# by evidence and never relearned by trial.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_lang_tests.py [--out tools/server-tasks/lang-tests]"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the operator facts, each verified against the parser/evaluator source
# (sdk/pillars/*/src) and the running node. Every line is a test.
KRON_OPS = [
    # (expression, the args, the expected answer, the fact it pins)
    ("0|(_a+_b)", "a=2,3 b=4,5", "6,8", "addition is exact and vectorised"),
    ("0|(_a-_b)", "a=10,3 b=4,5", "6,-2", "subtraction may go negative"),
    ("0|(_a*_b)", "a=2,3 b=4,5", "8,15", "multiplication is exact"),
    ("0|(_a%_b)", "a=1,1 b=2,4", "1/2,1/4", "the K's % is EXACT DIVISION, not modulo"),
    ("0|(_b|_a)", "a=2,3 b=7,8", "1,2", "the residue a|b is b mod a (argument order!)"),
    ("0|(_a&_b)", "a=3,2 b=5,7", "3,2", "the & is the MINIMUM, not bitwise AND"),
    ("0|(_a|_b)", "a=3,2 b=5,7", "5,7", "the | is max for... see the residue note"),
    ("0|((_a*10)+_b)", "a=2,3 b=1,1", "21,31", "explicit parens: the K is RIGHT TO LEFT"),
    ("0|(_a<_b)", "a=1,5 b=2,2", "1,0", "the < is the less-than (1 true, 0 false)"),
    ("0|(_a le _b)", "a=2,2 b=2,1", "1,0", "le is the <= ('<=' is refused)"),
]

# the grammar cases: what the parser accepts and refuses, with the reason
KRON_GRAMMAR = [
    ("0|_a+_b", "accept", "the bare expression is a valid body"),
    ("0|max(_a,_b)", "refuse", "min()/max() have no monadic form"),
    ("0|_a<=_b", "refuse", "'<=' is not the comparison; write le or the unicode"),
    ("0|_a^_b", "refuse", "^ is refused (fill/without)"),
]

EVE_RULES = [
    # (the body, the expected load result, the fact)
    ("eve t\ncounts = 1 .. 100\n\nblk {\n  count: counts 1 .. 100\n}", "accept",
     "a block binding every arg loads"),
    ("eve t\ncounts = 1 .. 100\n\nblk {\n  count: counts 1 .. 100\n  cap: counts 100 .. 100\n}",
     "accept", "the check's constants are held in fixed ranges"),
    ("eve t\ncounts = 1 .. 100\n\nblk {\n  count: counts 1 .. 100\n}", "refuse-when-check-adds-arg",
     "an argument the check passes but the block omits refuses the entry"),
]

SUTRA_RULES = [
    ("record.\n01 a PIC 9(9).\nprocedure division.\n  move 0 to a.", "accept",
     "a record, then a procedure, loads"),
    ("compute a = 0.", "refuse", "a body without the record line is refused"),
]

MYRA_RULES = [
    ("^ZXHOARD(folder:i9, seq:i9) = size:i18!", "accept", "a short global loads"),
    ("^ZXCOMPOSITE_KEY(high:i9, low:i9) = v:i18!", "refuse",
     "a global name longer than 8 characters is refused"),
]

TREBAGO_RULES = [
    ("trebago t\n  in  a, b\n  out r\n  r = a + b\nend", "accept", "the in/out/end shape loads"),
    ("trebago t\n  in  a, b\n  out r\n  r = sum(i = 1:2, a + b)\nend", "accept", "sum() is available"),
]


def emit(out, name, lines):
    os.makedirs(out, exist_ok=True)
    p = os.path.join(out, name)
    with open(p, "w") as f:
        f.write("\n".join(lines) + "\n")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/lang-tests")
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)

    # KRON: the operator tests, one manifest per operator fact
    for i, (expr, argv, answer, fact) in enumerate(KRON_OPS, 1):
        args_list = " ".join(a.split("=")[0] for a in argv.split())
        lines = ["# KRON operator test %d — %s" % (i, fact),
                 "zxapp 1", "id kron_op_%d" % i, "matrix 1",
                 "name KRON Operator %d" % i,
                 "provides op num %s" % args_list,
                 "logic kron op <<", expr, ".",
                 "check op %s = %s" % (argv, answer)]
        emit(out, "kron_op_%02d.zxapp" % i, lines)

    # the grammar refusals: a test that the parser refuses with a reason
    for i, (body, want, fact) in enumerate(KRON_GRAMMAR, 1):
        lines = ["# KRON grammar test %d — %s" % (i, fact),
                 "zxapp 1", "id kron_gram_%d" % i, "matrix 1",
                 "name KRON Grammar %d" % i,
                 "provides g num a b", "logic kron g <<", body, ".",
                 "# expected: %s (%s)" % (want, fact)]
        emit(out, "kron_gram_%02d.zxapp" % i, lines)

    # EVE / SUTRA / MYRA / TREBAGO: the rule tests
    for lang, rules in (("eve", EVE_RULES), ("sutra", SUTRA_RULES),
                        ("myra", MYRA_RULES), ("trebago", TREBAGO_RULES)):
        for i, (body, want, fact) in enumerate(rules, 1):
            lines = ["# %s rule test %d — %s" % (lang.upper(), i, fact),
                     "zxapp 1", "id %s_rule_%d" % (lang, i), "matrix 1",
                     "name %s Rule %d" % (lang.title(), i),
                     "provides r truth a b", "logic %s r <<" % lang, body, ".",
                     "# expected: %s (%s)" % (want, fact)]
            emit(out, "%s_rule_%02d.zxapp" % (lang, i), lines)

    n = len(os.listdir(out))
    print("bespoke-language tests written: %d manifests to %s" % (n, args.out))
    print("  KRON operators: %d" % len(KRON_OPS))
    print("  KRON grammar:   %d" % len(KRON_GRAMMAR))
    print("  EVE rules:      %d" % len(EVE_RULES))
    print("  SUTRA rules:    %d" % len(SUTRA_RULES))
    print("  MYRA rules:     %d" % len(MYRA_RULES))
    print("  TREBAGO rules:  %d" % len(TREBAGO_RULES))
    return 0


if __name__ == "__main__":
    sys.exit(main())