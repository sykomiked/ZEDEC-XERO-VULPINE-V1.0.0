#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_lint_icons.py — tests for lint_icons.py (LI1..LI7), stdlib unittest.

Run: python3 tools/zxvg/test_lint_icons.py
"""
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lint_icons as L  # noqa: E402

HEAD = '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" ' \
       'viewBox="0 0 1024 1024">'


def icon(chassis='<rect width="1024" height="1024" fill="#002911"/>',
         core='<path d="M100 100L900 100L500 900Z" fill="#F2C230"/>',
         kinetic='<circle cx="512" cy="512" r="64" fill="#F2C230"/>', extra='', head=HEAD,
         defs=''):
    return (head + '<title>t</title>' + defs + '<g id="chassis">' + chassis +
            '</g><g id="core-artifact">' + core + '</g><g id="kinetic-element">' + kinetic +
            '</g>' + extra + '</svg>')


def failed(text, **kw):
    """The set of checks that failed."""
    r = L.lint_text(text, **kw)
    return {c for c in L.CHECKS if r.counts[c]}


class Good(unittest.TestCase):
    def test_minimal_icon_passes(self):
        r = L.lint_text(icon())
        self.assertTrue(r.ok, r.to_json())
        # rect 4 + path 3 + circle 4
        self.assertEqual(r.knots, 11)

    def test_allowed_extras(self):
        t = icon(defs='<defs><linearGradient id="g" x1="0" y1="0" x2="1024.5" y2="0">'
                      '<stop offset="0.25" stop-color="#fff"/></linearGradient></defs>',
                 core='<g id="inner"><path d="M0 0H10V10Z" fill="url(#g)"/></g>',
                 head=HEAD + '<metadata><x:blob xmlns:x="urn:x">anything data: 1.23456</x:blob>'
                             '</metadata><desc>d</desc>')
        self.assertEqual(failed(t), set())

    def test_wide_stroke_ok(self):
        self.assertEqual(failed(icon(kinetic='<path d="M0 0L10 10" stroke="#fff" '
                                             'stroke-width="8"/>')), set())

    def test_scaled_stroke_measured_at_1024(self):
        k = '<g transform="scale(2)"><path d="M0 0L10 10" stroke="#fff" stroke-width="4"/></g>'
        self.assertEqual(failed(icon(kinetic=k)), set())
        k = '<g transform="scale(0.5)"><path d="M0 0L10 10" stroke="#fff" ' \
            'stroke-width="12"/></g>'
        self.assertEqual(failed(icon(kinetic=k)), {"stroke-width"})

    def test_stroke_none_is_fine(self):
        self.assertEqual(failed(icon(core='<path d="M0 0L9 9Z" stroke="none"/>')), set())


class Knots(unittest.TestCase):
    def k(self, d):
        return L.parse_path(d)

    def test_lines_and_close(self):
        self.assertEqual(self.k("M0 0 L10 0 L10 10 Z"), (3, 0))
        self.assertEqual(self.k("M0 0H10V10h-10z"), (4, 0))

    def test_implicit_repeats(self):
        self.assertEqual(self.k("M0 0 10 10 20 20"), (3, 0))
        self.assertEqual(self.k("M0,0L1,1,2,2,3,3"), (4, 0))

    def test_curves_count_on_curve_points_only(self):
        self.assertEqual(self.k("M0 0C1 1 2 2 3 3"), (2, 2))
        self.assertEqual(self.k("M0 0c1 1 2 2 3 3 4 4 5 5 6 6"), (3, 4))
        self.assertEqual(self.k("M0 0Q5 5 10 0T20 0"), (3, 1))
        self.assertEqual(self.k("M0 0S5 5 10 0"), (2, 1))

    def test_arcs_with_compact_flags(self):
        self.assertEqual(self.k("M0 0a10 10 0 0110 10"), (2, 0))
        self.assertEqual(self.k("M0 0A10 10 0 1 0 20 20"), (2, 0))

    def test_numbers_without_separators(self):
        self.assertEqual(self.k("M-1-2L.5.5"), (2, 0))

    def test_bad_paths(self):
        for d in ("L0 0", "M0", "M0 0 X1 1", "M0 0a1 1 0 2 0 1 1", "M0 0Z 5 5"):
            with self.assertRaises(L.PathError, msg=d):
                self.k(d)
        self.assertEqual(failed(icon(core='<path d="M0"/>')), {"vertex-budget"})

    def test_shapes(self):
        t = icon(chassis='<rect width="10" height="10"/><rect width="9" height="9" rx="2"/>',
                 core='<line x1="0" y1="0" x2="5" y2="5"/><ellipse rx="3" ry="4"/>',
                 kinetic='<polygon points="0,0 10,0 10,10"/><polyline points="0 0 1 1"/>')
        self.assertEqual(L.lint_text(t).knots, 4 + 8 + 2 + 4 + 3 + 2)

    def test_use_counts_where_drawn(self):
        defs = '<defs><path id="p" d="M0 0L1 1L2 2Z"/></defs>'
        k = '<use href="#p"/><use xlink:href="#p"/>'
        r = L.lint_text(icon(defs=defs, kinetic=k, chassis='', core=''))
        self.assertEqual(r.knots, 6)

    def test_budget_boundary(self):
        def poly(n):
            pts = " ".join("%d,%d" % (i, i) for i in range(n))
            return icon(chassis='', core='', kinetic='<polygon points="%s"/>' % pts)
        self.assertEqual(failed(poly(400)), set())
        self.assertEqual(failed(poly(401)), {"vertex-budget"})
        self.assertEqual(failed(poly(30), max_knots=29), {"vertex-budget"})


class Bad(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(failed("<svg"), {"parse"})
        self.assertEqual(failed('<html xmlns="x"/>'), {"parse"})
        self.assertEqual(failed('<!DOCTYPE svg [<!ENTITY a "b">]>' + icon()), {"parse"})

    def test_viewbox(self):
        for vb in ("0 0 512 512", "0,0,1024,1024", " 0 0 1024 1024", None):
            head = HEAD.replace(' viewBox="0 0 1024 1024"',
                                '' if vb is None else ' viewBox="%s"' % vb)
            self.assertEqual(failed(icon(head=head)), {"viewbox"}, vb)

    def test_raster_and_forbidden_elements(self):
        cases = [
            '<image href="#x"/>',
            '<foreignObject/>',
            '<text>hi</text>',
            '<path d="M0 0L1 1Z" filter="url(#f)"/>',
            '<path d="M0 0L1 1Z" style="mask:url(#m)"/>',
            '<path d="M0 0L1 1Z" mask="url(#m)"/>',
            '<use href="pic.PNG"/>',
            '<use xlink:href="other.svg#a"/>',
            '<path d="M0 0L1 1Z" fill="url(data:image/png;base64,AAAA)"/>',
            '<style>.a{fill:url( data:image/png;base64,AAAA)}</style>',
        ]
        for c in cases:
            self.assertIn("no-raster", failed(icon(kinetic=c)), c)
        self.assertIn("no-raster", failed(icon(defs='<filter id="f"><feGaussianBlur/></filter>')))
        self.assertIn("no-raster", failed(icon(defs='<mask id="m"/>')))

    def test_precision(self):
        self.assertEqual(failed(icon(core='<path d="M0.123 0L1 1Z"/>')), {"precision"})
        self.assertEqual(failed(icon(core='<path d="M1e2 0L1 1Z"/>')), {"precision"})
        self.assertEqual(failed(icon(core='<circle cx="1.005" cy="1" r="2"/>')), {"precision"})
        self.assertEqual(failed(icon(core='<g transform="translate(0.333 1)"/>')),
                         {"precision"})
        self.assertEqual(failed(icon(core='<path d="M1.25 -0.5L.75 1Z"/>')), set())
        # non-geometry attributes are not checked
        self.assertEqual(failed(icon(core='<g opacity="0.12345"/>')), set())
        r = L.lint_text(icon(core='<path d="%s"/>' % " ".join("M0.001 0" for _ in range(20))))
        self.assertEqual(r.counts["precision"], 20)
        self.assertEqual(len(r.checks["precision"]), L.MAX_MESSAGES)

    def test_thin_strokes(self):
        for k in ('<path d="M0 0L9 9" stroke="#fff"/>',  # no width: 1
                  '<path d="M0 0L9 9" stroke="#fff" stroke-width="7.99"/>',
                  '<path d="M0 0L9 9" style="stroke:#fff;stroke-width:2px"/>',
                  '<path d="M0 0L9 9" stroke="#fff" stroke-width="10%"/>'):
            self.assertEqual(failed(icon(kinetic=k)), {"stroke-width"}, k)
        # inherited from a group
        k = '<g stroke="#fff" stroke-width="2"><g><path d="M0 0L9 9"/></g></g>'
        self.assertEqual(failed(icon(kinetic=k)), {"stroke-width"})
        k = '<g stroke="#fff" stroke-width="2"><path d="M0 0L9 9" stroke="none"/></g>'
        self.assertEqual(failed(icon(kinetic=k)), set())

    def test_groups(self):
        no_kin = HEAD + '<g id="chassis"/><g id="core-artifact"/></svg>'
        self.assertEqual(failed(no_kin), {"groups"})
        self.assertEqual(failed(icon(extra='<path d="M0 0L1 1Z"/>')), {"groups"})
        self.assertEqual(failed(icon(extra='<g id="chassis"/>')), {"groups"})
        self.assertEqual(failed(icon(extra='<g id="halo"/>')), {"groups"})
        self.assertEqual(failed(icon(core='<g id="kinetic-element"/>')), {"groups"})
        wrapped = HEAD + '<g><g id="chassis"/><g id="core-artifact"/>' \
                         '<g id="kinetic-element"/></g></svg>'
        self.assertEqual(failed(wrapped), {"groups"})
        self.assertEqual(failed(icon(extra='<rect id="chassis" width="1" height="1"/>')),
                         {"groups"})

    def test_several_failures_reported_together(self):
        t = icon(head=HEAD.replace("1024 1024", "100 100"),
                 kinetic='<image href="a.png"/><path d="M0.555 0L9 9" stroke="red"/>',
                 extra='<rect width="1" height="1"/>')
        self.assertEqual(failed(t), {"viewbox", "no-raster", "precision", "stroke-width",
                                     "groups"})


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.tmp.name
        os.makedirs(os.path.join(d, "a", "b"))
        with open(os.path.join(d, "a", "good.svg"), "w") as f:
            f.write(icon())
        with open(os.path.join(d, "a", "b", "bad.SVG"), "w") as f:
            f.write(icon(core='<path d="M0 0L9 9" stroke="#fff"/>'))
        with open(os.path.join(d, "a", "notes.txt"), "w") as f:
            f.write("not an svg")

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = L.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_file_pass(self):
        code, out, _ = self.run_main(os.path.join(self.tmp.name, "a", "good.svg"))
        self.assertEqual(code, 0)
        self.assertIn("PASS", out)
        self.assertIn("1 files: 1 passed, 0 failed", out)

    def test_tree_fails_and_names_the_check(self):
        code, out, _ = self.run_main(self.tmp.name + os.sep)
        self.assertEqual(code, 1)
        self.assertIn("[stroke-width]", out)
        self.assertIn("2 files: 1 passed, 1 failed", out)

    def test_quiet(self):
        code, out, _ = self.run_main("--quiet", self.tmp.name)
        self.assertNotIn("PASS", out)
        self.assertIn("FAIL", out)

    def test_json(self):
        code, out, _ = self.run_main("--json", self.tmp.name)
        self.assertEqual(code, 1)
        d = json.loads(out)
        self.assertEqual(d["summary"]["files"], 2)
        self.assertEqual(d["summary"]["failures_by_check"]["stroke-width"], 1)
        bad = [f for f in d["files"] if not f["ok"]][0]
        self.assertFalse(bad["checks"]["stroke-width"]["ok"])
        self.assertTrue(bad["checks"]["groups"]["ok"])
        self.assertEqual(set(bad["checks"]), set(L.CHECKS))

    def test_missing_and_empty(self):
        code, _, err = self.run_main(os.path.join(self.tmp.name, "nope"))
        self.assertEqual(code, 2)
        self.assertIn("no such file", err)
        empty = os.path.join(self.tmp.name, "empty")
        os.makedirs(empty)
        self.assertEqual(self.run_main(empty)[0], 2)

    def test_symlinked_tree(self):
        link = os.path.join(self.tmp.name, "link")
        os.symlink(os.path.join(self.tmp.name, "a"), link)
        code, out, _ = self.run_main(link + os.sep)
        self.assertIn("2 files", out)

    def test_script_runs_standalone(self):
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lint_icons.py")
        p = subprocess.run([sys.executable, "-I", here, "--json",
                            os.path.join(self.tmp.name, "a", "good.svg")],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertTrue(json.loads(p.stdout)["files"][0]["ok"])


if __name__ == "__main__":
    unittest.main()
