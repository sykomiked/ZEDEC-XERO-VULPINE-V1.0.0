#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Z-Fabric reference netlist simulator (Python 3 standard library only).

usage: zfab_netsim.py --vectors DIR FILE.v|FILE.vhd ...

Parses the generated HDL itself (not the generator's data): every module or
entity, its ports, and one equation per output bit built from NOT, AND, OR,
parentheses, input bits and the constants 0 and 1. Anything else in an
equation is an error, as is an output bit assigned twice or never. Then it
drives every input of every module and compares the outputs with
DIR/<module>.hex (one expected value per line, inputs concatenated in port
order, first port most significant). This is a third, independent check
next to Icarus Verilog and GHDL, and the one that runs where neither is
installed. Exit 0 only if every vector of every module matches.
"""
import argparse
import os
import re
import sys


class NetError(Exception):
    pass


V_MODULE = re.compile(r"\bmodule\s+(\w+)\s*\((.*?)\);(.*?)\bendmodule\b", re.S)
V_PORT = re.compile(r"(input|output)\s+wire\s+\[(\d+):0\]\s+(\w+)")
V_ASSIGN = re.compile(r"\bassign\s+(\w+)\[(\d+)\]\s*=\s*(.*?);", re.S)
VHD_ENTITY = re.compile(r"\bentity\s+(\w+)\s+is\s+port\s*\((.*?)\);\s*end\s+entity", re.S | re.I)
VHD_PORT = re.compile(r"(\w+)\s*:\s*(in|out)\s+std_logic_vector\((\d+)\s+downto\s+0\)", re.I)
VHD_ARCH = re.compile(r"\barchitecture\s+\w+\s+of\s+(\w+)\s+is\s+begin(.*?)\bend\s+architecture", re.S | re.I)
VHD_ASSIGN = re.compile(r"(\w+)\((\d+)\)\s*<=\s*(.*?);", re.S)


def strip_comments(text, lead):
    return "\n".join(l.split(lead, 1)[0] for l in text.splitlines())


def tokens(expr, vhdl):
    if vhdl:
        pat = re.compile(r"\s*(?:(\()|(\))|(not\b|and\b|or\b)|'([01])'|(\w+)\((\d+)\))", re.I)
    else:
        pat = re.compile(r"\s*(?:(\()|(\))|([~&|])|1'b([01])|(\w+)\[(\d+)\])")
    out, i = [], 0
    expr = expr.strip()
    while i < len(expr):
        m = pat.match(expr, i)
        if not m or m.end() == i:
            raise NetError("cannot parse equation at %r" % expr[i:i + 20])
        lp, rp, op, const, name, idx = m.groups()
        if lp:
            out.append(("(",))
        elif rp:
            out.append((")",))
        elif op:
            out.append(({"not": "~", "and": "&", "or": "|"}.get(op.lower(), op),))
        elif const is not None:
            out.append(("c", int(const)))
        else:
            out.append(("b", name, int(idx)))
        i = m.end()
        while i < len(expr) and expr[i].isspace():
            i += 1
    return out


def parse(toks, ins):
    """expr := term ('|' term)*; term := factor ('&' factor)*;
    factor := '~' factor | '(' expr ')' | bit | const. Returns an AST."""
    pos = [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else ("$",)

    def take(kind):
        t = peek()
        if t[0] != kind:
            raise NetError("expected %s, got %s" % (kind, t[0]))
        pos[0] += 1
        return t

    def factor():
        t = peek()
        if t[0] == "~":
            take("~")
            return ("not", factor())
        if t[0] == "(":
            take("(")
            e = expr()
            take(")")
            return e
        if t[0] == "c":
            take("c")
            return ("c", t[1])
        if t[0] == "b":
            take("b")
            if t[1] not in ins or t[2] >= ins[t[1]]:
                raise NetError("unknown input bit %s[%d]" % (t[1], t[2]))
            return ("b", t[1], t[2])
        raise NetError("unexpected %s" % t[0])

    def term():
        n = [factor()]
        while peek()[0] == "&":
            take("&")
            n.append(factor())
        return n[0] if len(n) == 1 else ("and", n)

    def expr():
        n = [term()]
        while peek()[0] == "|":
            take("|")
            n.append(term())
        return n[0] if len(n) == 1 else ("or", n)

    e = expr()
    if pos[0] != len(toks):
        raise NetError("trailing tokens in equation")
    return e


def ev(node, env):
    k = node[0]
    if k == "c":
        return node[1]
    if k == "b":
        return (env[node[1]] >> node[2]) & 1
    if k == "not":
        return 1 - ev(node[1], env)
    if k == "and":
        return int(all(ev(n, env) for n in node[1]))
    return int(any(ev(n, env) for n in node[1]))


def load(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    vhdl = path.endswith((".vhd", ".vhdl"))
    mods = {}
    if vhdl:
        text = strip_comments(text, "--")
        archs = {m.group(1): m.group(2) for m in VHD_ARCH.finditer(text)}
        for m in VHD_ENTITY.finditer(text):
            ports = [(p, d.lower(), int(w) + 1) for p, d, w in VHD_PORT.findall(m.group(2))]
            body = archs.get(m.group(1))
            if body is None:
                raise NetError("%s: entity %s has no architecture" % (path, m.group(1)))
            mods[m.group(1)] = (ports, VHD_ASSIGN.findall(body))
    else:
        text = strip_comments(text, "//")
        for m in V_MODULE.finditer(text):
            ports = [(p, "in" if d == "input" else "out", int(w) + 1)
                     for d, w, p in V_PORT.findall(m.group(2))]
            mods[m.group(1)] = (ports, V_ASSIGN.findall(m.group(3)))
    if not mods:
        raise NetError("%s: no modules found" % path)
    out = {}
    for name, (ports, assigns) in mods.items():
        ins = {p: w for p, d, w in ports if d == "in"}
        outs = {p: w for p, d, w in ports if d == "out"}
        eqs = {}
        for p, j, e in assigns:
            j = int(j)
            if p not in outs or j >= outs[p]:
                raise NetError("%s: %s assigns unknown output %s[%d]" % (path, name, p, j))
            if (p, j) in eqs:
                raise NetError("%s: %s assigns %s[%d] twice" % (path, name, p, j))
            eqs[(p, j)] = parse(tokens(e, vhdl), ins)
        for p, w in outs.items():
            for j in range(w):
                if (p, j) not in eqs:
                    raise NetError("%s: %s never assigns %s[%d]" % (path, name, p, j))
        out[name] = (ports, eqs)
    return out


def simulate(name, ports, eqs, vec_path):
    with open(vec_path, encoding="utf-8") as f:
        want = [int(l, 16) for l in f.read().split()]
    ins = [(p, w) for p, d, w in ports if d == "in"]
    outs = [(p, w) for p, d, w in ports if d == "out"]
    nin = sum(w for _, w in ins)
    if len(want) != 1 << nin:
        raise NetError("%s: %d vectors, need %d" % (vec_path, len(want), 1 << nin))
    bad = 0
    for i in range(1 << nin):
        env, pos = {}, nin
        for p, w in ins:
            pos -= w
            env[p] = (i >> pos) & ((1 << w) - 1)
        got = 0
        for p, w in outs:
            v = sum(ev(eqs[(p, j)], env) << j for j in range(w))
            got = (got << w) | v
        if got != want[i]:
            bad += 1
            if bad <= 10:
                print("FAIL %s input %d: got %d, libzxv says %d" % (name, i, got, want[i]))
    return 1 << nin, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--vectors", required=True)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args(argv)
    total = errors = 0
    try:
        for path in a.files:
            mods = load(path)
            n = 0
            for name in sorted(mods):
                ports, eqs = mods[name]
                c, b = simulate(name, ports, eqs, os.path.join(a.vectors, name + ".hex"))
                total, errors, n = total + c, errors + b, n + c
            print("netsim %s: %d modules, %d vectors" % (os.path.basename(path), len(mods), n))
    except (NetError, OSError, ValueError) as e:
        print("netsim: %s" % e)
        return 1
    if errors:
        print("netsim: FAIL, %d of %d vectors differ from libzxv" % (errors, total))
        return 1
    print("netsim: ALL PASS, %d vectors" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
