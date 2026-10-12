# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""R11 for Python: the core modules of zxv_vfx contain no float.

The C kits scan their sources for float types (check_nofloat.py). Here the
same idea runs on the Python syntax tree of every core module: no float or
complex literal, no ``float()`` / ``complex()`` call, no ``__float__``, no
``math`` / ``cmath`` / ``random`` / ``statistics`` import, no ``**`` (an int
to a negative power is a float), no import of the float edge (``_edge``)
or the DCC adapters, and the ``/`` operator only inside ``_exact.rdiv``,
which divides two libzxv rationals (``int / int`` would be a float).

Only ``maya.py``, ``houdini.py`` and ``_edge.py`` may use floats; they are
the edge and are excluded by name.
"""

from __future__ import annotations

import ast
from pathlib import Path
from typing import List

PKG = Path(__file__).resolve().parents[1] / "src" / "zxv_vfx"
EDGE = {"maya.py", "houdini.py", "_edge.py"}
CORE = sorted(p for p in PKG.glob("*.py") if p.name not in EDGE)
BANNED_MODULES = {"math", "cmath", "random", "statistics", "numpy"}
BANNED_LOCAL = {"_edge", "maya", "houdini"}


def violations(source: str, filename: str) -> List[str]:
    tree = ast.parse(source, filename)
    out: List[str] = []
    allowed_div = set()
    if filename == "_exact.py":
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef) and node.name == "rdiv":
                allowed_div.update(id(n) for n in ast.walk(node))

    def bad(node: ast.AST, why: str) -> None:
        out.append(f"{filename}:{getattr(node, 'lineno', '?')}: {why}")

    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, (float, complex)):
            bad(node, f"float literal {node.value!r}")
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and \
                node.func.id in ("float", "complex"):
            bad(node, f"{node.func.id}() call")
        elif isinstance(node, ast.Attribute) and node.attr == "__float__":
            bad(node, "__float__")
        elif isinstance(node, ast.Name) and node.id in ("float", "complex"):
            # a bare reference (not a call) is fine only in isinstance(..., float) refusals
            pass
        elif isinstance(node, (ast.BinOp, ast.AugAssign)) and isinstance(node.op, ast.Pow):
            bad(node, "** operator")
        elif isinstance(node, (ast.BinOp, ast.AugAssign)) and isinstance(node.op, ast.Div) and \
                id(node) not in allowed_div:
            bad(node, "/ operator outside _exact.rdiv")
        elif isinstance(node, ast.Import):
            for a in node.names:
                if a.name.split(".")[0] in BANNED_MODULES:
                    bad(node, f"import {a.name}")
        elif isinstance(node, ast.ImportFrom):
            mod = node.module or ""
            if mod.split(".")[0] in BANNED_MODULES:
                bad(node, f"from {mod} import")
            if node.level > 0 and (mod in BANNED_LOCAL or any(a.name in BANNED_LOCAL for a in node.names)):
                bad(node, f"core imports the edge ({mod or '.'})")
            if mod.startswith("zxv_vfx.") and mod.split(".")[1] in BANNED_LOCAL:
                bad(node, f"core imports the edge ({mod})")
    return out


def test_core_list() -> None:
    names = {p.name for p in CORE}
    assert {"__init__.py", "_exact.py", "frametime.py", "transform.py", "seeds.py", "farm.py"} <= names
    assert not names & EDGE


def test_core_has_no_float() -> None:
    problems = [v for p in CORE for v in violations(p.read_text(encoding="utf-8"), p.name)]
    assert problems == [], "\n".join(problems)


def test_bare_float_name_only_in_refusals() -> None:
    # `float` may appear only as isinstance(x, float), to refuse floats
    for p in CORE:
        tree = ast.parse(p.read_text(encoding="utf-8"), p.name)
        parents = {}
        for node in ast.walk(tree):
            for child in ast.iter_child_nodes(node):
                parents[id(child)] = node
        for node in ast.walk(tree):
            if isinstance(node, ast.Name) and node.id == "float":
                up = parents.get(id(node))
                call = parents.get(id(up)) if isinstance(up, ast.Tuple) else up
                ok = isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and \
                    call.func.id == "isinstance"
                assert ok, f"{p.name}:{node.lineno}: `float` used outside an isinstance() refusal"


def test_checker_catches_floats() -> None:
    # the checker itself, on sources that must fail
    cases = {
        "x = 0.5": "float literal",
        "x = 1e3": "float literal",
        "x = 2j": "float literal",
        "y = float(3)": "float() call",
        "y = (3).__float__()": "__float__",
        "z = 1 / 3": "/ operator",
        "z = 2 ** -1": "** operator",
        "import math": "import math",
        "from random import random": "from random import",
        "from ._edge import lossy_float": "core imports the edge",
        "from . import maya": "core imports the edge",
        "from zxv_vfx.houdini import set_frame": "core imports the edge",
        "def f(a):\n    a /= 2\n": "/ operator",
    }
    for src, why in cases.items():
        v = violations(src, "probe.py")
        assert v and why in v[0], (src, v)
    assert violations("def rdiv(a, b):\n    return a / b\n", "_exact.py") == []
    assert violations("def rdiv(a, b):\n    return a / b\n", "frametime.py") != []
    assert violations("ok = isinstance(v, float)\nn = 7 // 2\n", "probe.py") == []
