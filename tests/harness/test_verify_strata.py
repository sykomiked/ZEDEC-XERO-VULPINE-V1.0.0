#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Tests for build_system/verify_strata.py, the seven-strata check (gap B1).

Each test builds a small kernel tree in a temporary directory, runs the check
on it and asserts the exit status and the exact problem lines. The last tests
run the real tree and verify_layers.sh itself.

    python3 tests/harness/test_verify_strata.py
"""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "build_system"))
try:
    import verify_strata as vs  # noqa: E402
except SystemExit:
    raise AssertionError("importing verify_strata ran its main()")


class Fixture(unittest.TestCase):
    """A scratch tree: kernel/src, kernel/include, kernel/arch, a map."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="strata.")
        self.addCleanup(shutil.rmtree, self.root)
        self.map_lines = []

    def put(self, rel, text=""):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)

    def src(self, rel, *includes, body=""):
        text = "".join('#include "%s"\n' % i for i in includes) + body
        self.put("kernel/src/" + rel, text)

    def map(self, *lines):
        self.map_lines.extend(lines)

    def run_check(self, graph=False):
        mp = os.path.join(self.root, "strata.txt")
        with open(mp, "w", encoding="utf-8") as fh:
            fh.write("\n".join(self.map_lines) + "\n")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = vs.check(self.root, mp, graph=graph)
        return rc, out.getvalue()

    def problems(self, out):
        return [ln.strip() for ln in out.splitlines()
                if ln.startswith("  ") and not ln.startswith("  strata:")]


class Assignment(Fixture):
    def test_clean_tree_passes(self):
        self.src("a/a.c", "a.h")
        self.src("a/a.h")
        self.src("b/b.c", "../a/a.h")
        self.map("a base", "b kernel")
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.problems(out), [])
        self.assertIn("PASS:", out)
        self.assertIn("2 sources, 1 headers, 2 map lines", out)

    def test_unassigned_source_fails(self):
        self.src("a/a.c")
        self.src("b/b.c")
        self.map("a base")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "UNASSIGNED  kernel/src/b/b.c has no stratum in strata.txt"])
        self.assertIn("FAIL: 1 strata problem(s).", out)

    def test_unassigned_header_only_module_fails(self):
        self.src("a/a.c")
        self.src("h/only.h")
        self.map("a base")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "UNASSIGNED  kernel/src/h/only.h has no stratum in strata.txt"])

    def test_tests_and_debug_drivers_are_not_modules(self):
        self.src("a/a.c")
        self.src("a/b/test_x.c")
        self.src("z/test_z.c")
        self.src("z/debug_z.c")
        self.map("a base")
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)

    def test_stale_line_fails(self):
        self.src("a/a.c")
        self.map("a base", "gone kernel")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "STALE       gone matches nothing under kernel/src"])

    def test_longest_match_and_stem_pin(self):
        self.src("m/hash.c", "hash.h")
        self.src("m/hash.h")
        self.src("m/vault.c", "hash.h")
        self.src("low/low.c", "../m/hash.h")
        self.map("m trust", "m/hash base", "low kernel")
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)
        self.map_lines.remove("m/hash base")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "STRATUM     kernel/src/low/low.c (S1 kernel) includes "
            "kernel/src/m/hash.h (S3 trust)"])

    def test_exact_file_pin_beats_stem(self):
        self.src("m/x.c")
        self.src("m/x.h")
        self.map("m/x above", "m/x.c base")
        self.assertEqual(vs.stratum_of("m/x.c", {"m/x": 8, "m/x.c": 0}),
                         (8, "m/x"))
        self.assertEqual(vs.stratum_of("m/x.c", {"m": 8, "m/x.c": 0}),
                         (0, "m/x.c"))
        self.assertEqual(vs.stratum_of("m/n/x.c", {"m": 8}), (8, "m"))
        self.assertEqual(vs.stratum_of("q/x.c", {"m": 8}), (None, None))
        self.assertEqual(vs.stratum_of("x.c", {"m": 8}), (None, None))


class Strata(Fixture):
    def test_every_upward_edge_is_reported(self):
        self.src("hi/hi.h")
        self.src("lo/lo.c", "../hi/hi.h")
        self.src("lo/lo.h", "../hi/hi.h")
        self.map("hi settlement", "lo transport")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "STRATUM     kernel/src/lo/lo.c (S2 transport) includes "
            "kernel/src/hi/hi.h (S6 settlement)",
            "STRATUM     kernel/src/lo/lo.h (S2 transport) includes "
            "kernel/src/hi/hi.h (S6 settlement)"])
        self.assertIn("FAIL: 2 strata problem(s).", out)

    def test_adjacent_strata_upward_fails_and_same_passes(self):
        self.src("a/a.h")
        self.src("b/b.c", "../a/a.h")
        self.map("a ai", "b logic")
        self.assertEqual(self.run_check()[0], 1)
        self.map_lines[:] = ["a ai", "b ai"]
        self.assertEqual(self.run_check()[0], 0)
        self.map_lines[:] = ["a logic", "b ai"]
        self.assertEqual(self.run_check()[0], 0)

    def test_commented_include_is_ignored(self):
        self.src("hi/hi.h")
        self.src("lo/lo.c", body='/* #include "../hi/hi.h" */\n'
                 '// #include "../hi/hi.h"\n')
        self.map("hi above", "lo base")
        self.assertEqual(self.run_check()[0], 0)

    def test_include_resolution_order(self):
        # kernel/include shadows kernel/src, as -Ikernel/include comes first
        self.put("kernel/include/top.h")
        self.src("hi/top.h")
        self.src("lo/a.c", "top.h")
        # bare name found through an -I directory
        self.src("hi/deep.h")
        self.src("lo/b.c", "deep.h")
        self.map("hi above", "lo base")
        rc, out = self.run_check()
        self.assertEqual(self.problems(out), [
            "STRATUM     kernel/src/lo/b.c (S0 base) includes "
            "kernel/src/hi/deep.h (S8 above)"])

    def test_own_directory_wins_for_a_shared_name(self):
        self.src("aa/fips.h")
        self.src("zz/fips.h")
        self.src("zz/sub/k.c", "fips.h")
        self.map("aa above", "zz base")
        self.assertEqual(self.run_check()[0], 0)
        self.assertEqual(vs.Tree(self.root).resolve("zz/sub/k.c", "fips.h"),
                         "zz/fips.h")
        self.assertEqual(vs.Tree(self.root).resolve("qq/k.c", "fips.h"),
                         "aa/fips.h")

    def test_relative_path_and_src_rooted_path(self):
        self.src("a/x.h")
        self.src("b/y.h")
        t = vs.Tree(self.root)
        self.assertEqual(t.resolve("a/m.c", "x.h"), "a/x.h")
        self.assertEqual(t.resolve("c/m.c", "b/y.h"), "b/y.h")
        self.assertEqual(t.resolve("c/m.c", "../b/y.h"), "b/y.h")
        self.assertIsNone(t.resolve("c/m.c", "nope.h"))
        self.assertIsNone(t.resolve("c/m.c", "q/y.h"))

    def test_include_of_unmapped_header_is_left_to_unassigned(self):
        self.src("lo/lo.c", "../hi/hi.h")
        self.src("hi/hi.h")
        self.map("lo base")
        rc, out = self.run_check()
        self.assertEqual(self.problems(out), [
            "UNASSIGNED  kernel/src/hi/hi.h has no stratum in strata.txt"])


class Requires(Fixture):
    def setUp(self):
        super().setUp()
        self.src("a/a.c")
        self.map("a base")

    def test_unprovided_requires_fails(self):
        self.src("a/a.c", body="ZXV_DECLARE(a, ZXV_PROVIDES(a_ready),\n"
                 "    ZXV_REQUIRES(mm_ready,\n      net_ready));\n")
        self.put("kernel/arch/x/mm.c", "ZXV_DECLARE(mm, ZXV_PROVIDES(mm_ready),"
                 " ZXV_REQUIRES_NONE);\n")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertEqual(self.problems(out), [
            "REQUIRES    net_ready is required by kernel/src/a/a.c and "
            "provided by nothing"])
        self.assertIn("2 capabilities required, 2 provided", out)
        self.assertIn("FAIL: 1 strata problem(s).", out)

    def test_every_requirer_is_named_once(self):
        self.src("a/a.c", body="ZXV_REQUIRES(x_ready) ZXV_REQUIRES(x_ready)")
        self.src("a/b.c", body="ZXV_REQUIRES(x_ready)")
        rc, out = self.run_check()
        self.assertEqual(self.problems(out), [
            "REQUIRES    x_ready is required by kernel/src/a/a.c, "
            "kernel/src/a/b.c and provided by nothing"])

    def test_at_and_phased_forms_count_their_first_argument(self):
        self.src("a/a.c", body="ZXV_REQUIRES_AT(p_ready, 2)\n"
                 "ZXV_REQUIRES_PHASED(q_ready, 1, 3, 4, 0)\n"
                 "ZXV_PROVIDES_PHASED(p_ready, 2, 0, 1, 0)\n"
                 "ZXV_PROVIDES_AT(q_ready, 1)\n")
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)
        self.assertIn("2 capabilities required, 2 provided", out)

    def test_a_symbolic_version_is_not_a_capability(self):
        self.src("a/a.c", body="ZXV_REQUIRES_AT(p_ready, P_VER)\n"
                 "ZXV_PROVIDES_AT(p_ready, P_VER)\n")
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)
        self.assertIn("1 capabilities required, 1 provided", out)

    def test_negtest_fixture_defines_and_comments_are_ignored(self):
        self.src("a/a.c", body=(
            "#if defined(ZXV_DECL_NEGTEST) && ((ZXV_DECL_NEGTEST) & 1)\n"
            "#if 1\nZXV_REQUIRES(nested_absent)\n#endif\n"
            "ZXV_REQUIRES(negtest_absent)\n#endif\n"
            "#define ZXV_REQUIRES(...) .requires = { x }, \\\n"
            "    .n = ZXV_REQUIRES(macro_absent)\n"
            "/* ZXV_REQUIRES(comment_absent) */\n"
            "// ZXV_REQUIRES(line_absent)\n"
            "#if 1\nZXV_REQUIRES(kept_ready)\n#endif\n"
            "ZXV_PROVIDES(kept_ready)\n"))
        rc, out = self.run_check()
        self.assertEqual(rc, 0, out)
        self.assertIn("1 capabilities required, 1 provided", out)

    def test_negtest_block_ends_and_later_requires_count(self):
        self.src("a/a.c", body=(
            "#ifdef ZXV_DECL_NEGTEST\nZXV_REQUIRES(gone)\n#endif\n"
            "ZXV_REQUIRES(after_absent)\n"))
        rc, out = self.run_check()
        self.assertEqual(self.problems(out), [
            "REQUIRES    after_absent is required by kernel/src/a/a.c and "
            "provided by nothing"])

    def test_third_party_and_tests_are_not_declarations(self):
        self.put("kernel/third_party/x/x.c", "ZXV_REQUIRES(tp_absent)")
        self.src("a/test_a.c", body="ZXV_REQUIRES(t_absent)")
        self.assertEqual(self.run_check()[0], 0)


class MapFile(Fixture):
    def test_malformed_unknown_and_duplicate_lines_fail(self):
        self.src("a/a.c")
        self.map("# comment", "", "a base   # note", "b", "c nowhere",
                 "a kernel")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        mp = os.path.join(self.root, "strata.txt")
        self.assertEqual(self.problems(out), [
            "MAP       %s:4: expected '<path> <stratum>'" % mp,
            "MAP       %s:5: unknown stratum nowhere" % mp,
            "MAP       %s:6: a mapped twice" % mp])
        self.assertIn("FAIL: 3 strata problem(s).", out)
        self.assertEqual(vs.load_map(mp)[0], {"a": 0})  # first line wins

    def test_trailing_slash_is_the_same_path(self):
        self.src("a/a.c")
        self.map("a/ adapters")
        self.assertEqual(self.run_check()[0], 0)
        entries, errors = vs.load_map(os.path.join(self.root, "strata.txt"))
        self.assertEqual((entries, errors), ({"a": 7}, []))

    def test_strata_are_ordered(self):
        self.assertEqual(vs.STRATA, ["base", "kernel", "transport", "trust",
                                     "logic", "ai", "settlement", "adapters",
                                     "above"])


class Graph(Fixture):
    def test_graph_lists_cross_directory_edges_only(self):
        self.src("a/a.c", "a.h", "../b/b.h", "../c/c.h")
        self.src("a/a.h")
        self.src("b/b.h", "../c/c.h")
        self.src("c/c.h")
        rc, out = self.run_check(graph=True)
        self.assertEqual(rc, 0)
        self.assertEqual(out, "a -> b c\nb -> c\n")


class RealTree(unittest.TestCase):
    def test_repository_passes(self):
        r = subprocess.run([sys.executable,
                            os.path.join(ROOT, "build_system",
                                         "verify_strata.py")],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS:", r.stdout)

    def script(self, *args):
        return subprocess.run([sys.executable, os.path.join(
            ROOT, "build_system", "verify_strata.py")] + list(args),
            capture_output=True, text=True, cwd=ROOT)

    def test_quiet_prints_nothing_on_pass(self):
        r = self.script("--quiet")
        self.assertEqual((r.returncode, r.stdout), (0, ""))

    def test_help_and_graph(self):
        r = self.script("--help")
        self.assertIn("the seven-strata layering check", r.stdout)
        r = self.script("--graph")
        self.assertEqual(r.returncode, 0)
        self.assertIn("tensor -> e8 robin_debanks\n", r.stdout)

    def test_verify_layers_fails_on_a_strata_problem(self):
        tmp = tempfile.mkdtemp(prefix="strata-vl.")
        self.addCleanup(shutil.rmtree, tmp)
        os.makedirs(os.path.join(tmp, "build_system"))
        os.makedirs(os.path.join(tmp, "kernel", "src", "a"))
        for f in ("verify_layers.sh", "verify_strata.py"):
            shutil.copy(os.path.join(ROOT, "build_system", f),
                        os.path.join(tmp, "build_system", f))
        with open(os.path.join(tmp, "kernel", "src", "a", "a.c"), "w") as fh:
            fh.write("int a;\n")
        mp = os.path.join(tmp, "build_system", "strata.txt")
        script = os.path.join(tmp, "build_system", "verify_layers.sh")
        with open(mp, "w") as fh:
            fh.write("a base\n")
        r = subprocess.run(["bash", script], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)
        with open(mp, "w") as fh:
            fh.write("b base\n")
        r = subprocess.run(["bash", script], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("UNASSIGNED  kernel/src/a/a.c", r.stdout)
        self.assertIn("FAIL: strata check.", r.stdout)


if __name__ == "__main__":
    unittest.main()
