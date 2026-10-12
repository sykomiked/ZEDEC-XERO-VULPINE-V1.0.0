# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The float edge. ONLY the DCC adapters (``maya``, ``houdini``) import this.

Maya and Houdini take times, matrices and key values as binary64 floats.
``lossy_float`` is the one place an exact value becomes a float: the
nearest double to the rational (Python's int / int true division is
correctly rounded). The result is LOSSY and ONE-WAY: it is written to the
DCC and never read back into ``zxv_vfx``'s core, which has no function that
accepts a float. The no-float test checks that no core module imports this
one.
"""

from __future__ import annotations

from typing import Any, Iterable, List

from zxv import Rational

__all__ = ["DccUnavailableError", "lossy_float", "lossy_floats"]


class DccUnavailableError(ImportError):
    """The DCC's Python module (``maya.cmds`` or ``hou``) cannot be imported here."""


def lossy_float(value: Any) -> float:
    """LOSSY: the nearest float to an exact value, for writing to a DCC only."""
    r = Rational.coerce(value, "value")
    return r.numerator / r.denominator


def lossy_floats(values: Iterable[Any]) -> List[float]:
    """LOSSY: :func:`lossy_float` of each value."""
    return [lossy_float(v) for v in values]
