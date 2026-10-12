#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Unit tests for gen/zfab_gen.py and tools/zfab_netsim.py (stdlib unittest)."""
import copy
import json
import os
import random
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "gen"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import zfab_gen as G  # noqa: E402
import zfab_netsim as N  # noqa: E402

TABLES = os.path.join(ROOT, "tables", "zxv_tables.json")


def table_eval(eqs, nout, i):
    return sum(G.evaluate(eqs[b], i) << b for b in range(nout))


class Minimiser(unittest.TestCase):
    def test_every_function_of_two_and_three_inputs(self):
        for n in (1, 2, 3):
            for f in range(1 << (1 << n)):
                table = [(f >> i) & 1 for i in range(1 << n)]
                eqs = G.minimise(table, n, 0)
                for i in range(1 << n):
                    self.assertEqual(G.evaluate(eqs, i), table[i], (n, f, i))

    def test_random_multi_bit_tables(self):
        rng = random.Random(20261012)
        for _ in range(300):
            n = rng.randint(1, 7)
            nout = rng.randint(1, 4)
            table = [rng.randrange(1 << nout) for _ in range(1 << n)]
            eqs = [G.minimise(table, n, b) for b in range(nout)]
            for i in range(1 << n):
                self.assertEqual(table_eval(eqs, nout, i), table[i])

    def test_constants_and_support(self):
        self.assertEqual(G.minimise([0, 0, 0, 0], 2, 0), [])
        self.assertEqual(G.minimise([1, 1, 1, 1], 2, 0), [[]])
        # y = input bit 1 only: one single-literal term
        self.assertEqual(G.minimise([0, 0, 1, 1], 2, 0), [[(1, 1)]])
        self.assertEqual(G.support([0, 1, 0, 1, 0, 1, 0, 1], 3, 0), [0])

    def test_deterministic(self):
        rng = random.Random(7)
        table = [rng.randrange(8) for _ in range(64)]
        a = [G.minimise(table, 6, b) for b in range(3)]
        b = [G.minimise(list(table), 6, b) for b in range(3)]
        self.assertEqual(a, b)


class Loader(unittest.TestCase):
    def setUp(self):
        with open(TABLES, encoding="utf-8") as f:
            self.t = json.load(f)
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, t):
        p = os.path.join(self.dir, "t.json")
        with open(p, "w", encoding="utf-8") as f:
            json.dump(t, f)
        return p

    def test_committed_tables_load(self):
        G.load_tables(TABLES)

    def bad(self, mutate):
        t = copy.deepcopy(self.t)
        mutate(t)
        with self.assertRaises(G.GenError):
            G.load_tables(self.write(t))

    def test_rejects(self):
        self.bad(lambda t: t.update(format="zxv-fabric-tables/2"))
        self.bad(lambda t: t["hex_action"].pop())
        self.bad(lambda t: t["hex_action"].append(0))
        self.bad(lambda t: t["hex_action"].__setitem__(5, 8))
        self.bad(lambda t: t["hex_action"].__setitem__(5, -1))
        self.bad(lambda t: t["lp_and"].__setitem__(0, True))
        self.bad(lambda t: t["lp_and"].__setitem__(0, "0"))
        self.bad(lambda t: t["hex_or"].__setitem__(4095, 64))
        self.bad(lambda t: t.pop("hex_not"))


class EndToEnd(unittest.TestCase):
    def test_generated_hdl_reproduces_every_table(self):
        tables = G.load_tables(TABLES)
        mods = G.build(tables)
        d = tempfile.mkdtemp()
        try:
            for name, text in (("f.v", G.emit_verilog(mods)), ("f.vhd", G.emit_vhdl(mods))):
                p = os.path.join(d, name)
                with open(p, "w", encoding="utf-8") as f:
                    f.write(text)
                parsed = N.load(p)
                self.assertEqual(sorted(parsed), sorted(m["name"] for m in mods))
                for m in mods:
                    vp = os.path.join(d, m["name"] + ".hex")
                    with open(vp, "w", encoding="utf-8") as f:
                        f.write(G.emit_vectors(m))
                    ports, eqs = parsed[m["name"]]
                    n, bad = N.simulate(m["name"], ports, eqs, vp)
                    self.assertEqual((n, bad), (len(m["table"]), 0), m["name"])
        finally:
            shutil.rmtree(d)

    def test_wrong_table_is_caught_by_netsim(self):
        mods = G.build(G.load_tables(TABLES))
        m = next(x for x in mods if x["name"] == "zxv_hex_action")
        d = tempfile.mkdtemp()
        try:
            p = os.path.join(d, "f.v")
            with open(p, "w", encoding="utf-8") as f:
                f.write(G.emit_verilog(mods))
            vp = os.path.join(d, "zxv_hex_action.hex")
            bad = dict(m, table=[(v + 1) % 5 if i == 63 else v for i, v in enumerate(m["table"])])
            with open(vp, "w", encoding="utf-8") as f:
                f.write(G.emit_vectors(bad))
            ports, eqs = N.load(p)["zxv_hex_action"]
            self.assertEqual(N.simulate("zxv_hex_action", ports, eqs, vp), (64, 1))
        finally:
            shutil.rmtree(d)

    def test_netsim_refuses_malformed_netlists(self):
        d = tempfile.mkdtemp()
        try:
            cases = [
                "module m (input wire [1:0] a, output wire [0:0] y);\n"
                "assign y[0] = a[0] ^ a[1];\nendmodule\n",                     # XOR not allowed
                "module m (input wire [1:0] a, output wire [0:0] y);\n"
                "assign y[0] = a[2];\nendmodule\n",                            # no such bit
                "module m (input wire [1:0] a, output wire [1:0] y);\n"
                "assign y[0] = a[0];\nendmodule\n",                            # y[1] undriven
                "module m (input wire [1:0] a, output wire [0:0] y);\n"
                "assign y[0] = a[0];\nassign y[0] = a[1];\nendmodule\n",      # driven twice
                "module m (input wire [1:0] a, output wire [0:0] y);\n"
                "assign y[0] = (a[0] & a[1];\nendmodule\n",                    # unbalanced
            ]
            for i, src in enumerate(cases):
                p = os.path.join(d, "c%d.v" % i)
                with open(p, "w", encoding="utf-8") as f:
                    f.write(src)
                with self.assertRaises(N.NetError, msg=src):
                    N.load(p)
        finally:
            shutil.rmtree(d)

    def test_semantics_spot_checks(self):
        # The documented rules, read back from the committed tables.
        t = G.load_tables(TABLES)
        for tag in range(64):
            base, pol = tag >> 4, (tag >> 2) & 3
            if pol == 2:
                self.assertEqual(t["hex_action"][tag], 4)  # forbidden -> rollback
            # the phase never changes the action
            self.assertEqual(t["hex_action"][tag], t["hex_action"][tag & ~3])
        self.assertEqual([t["hex_phase_next"][p] for p in (0, 1, 3, 2)], [1, 3, 2, 0])


if __name__ == "__main__":
    unittest.main(verbosity=1)
