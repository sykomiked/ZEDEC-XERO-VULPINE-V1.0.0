# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The ``ZXV`` setting, validated and normalised.

.. code-block:: python

    ZXV = {
        "CURRENCIES": ["NGN"],
        "PRIVATE_UNITS": {"VFV": 2},
        "ROOT_RIGHTS": ["LEDGER_READ", "LEDGER_POST"],
        "CAPABILITIES": {
            "teller": ["LEDGER_READ", "LEDGER_POST"],
            "auditor": "LEDGER_READ",
        },
        "FATAL_POLICY": "raise",          # or "abort"
        "LIBRARY_PATH": None,             # else ZXV_LIBRARY_PATH, the bundled copy, the system loader
    }
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any, Mapping, Optional

from zxv import Cap, FatalPolicy

_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")
_CCY = re.compile(r"^[A-Z]{3}$")
_PRIVATE = re.compile(r"^[A-Z0-9]{3,8}$")
_KNOWN = frozenset({"CURRENCIES", "PRIVATE_UNITS", "ROOT_RIGHTS", "CAPABILITIES", "FATAL_POLICY", "LIBRARY_PATH"})
_RIGHTS = {m.name: m for m in Cap if m.name is not None and m.name != "NONE"}


class ZxvSettingsError(ValueError):
    """The ``ZXV`` setting cannot work. ``problems`` lists every issue found."""

    def __init__(self, problems: list[str]) -> None:
        super().__init__("Invalid ZXV setting:\n- " + "\n- ".join(problems))
        self.problems = problems


@dataclass(frozen=True)
class ZxvSettings:
    """``settings.ZXV`` after validation; rights are :class:`zxv.Cap` bits."""

    currencies: tuple[str, ...] = ()
    private_units: tuple[tuple[str, int], ...] = ()
    root_rights: Cap = Cap.ALL
    capabilities: Mapping[str, Cap] = field(default_factory=dict)
    fatal_policy: FatalPolicy = FatalPolicy.RAISE
    library_path: Optional[str] = None


def right_names(bits: Cap) -> list[str]:
    """The names of the single rights set in ``bits`` (never ``ALL``)."""
    return [n for n, m in _RIGHTS.items() if n != "ALL" and bits & m]


def _rights(spec: Any, what: str, problems: list[str]) -> Cap:
    if isinstance(spec, bool) or isinstance(spec, (int, float)):
        problems.append(f"{what}: rights are names such as 'LEDGER_READ', not numbers")
        return Cap.NONE
    if isinstance(spec, str):
        names: list[Any] = [s.strip() for s in re.split(r"[,|]", spec) if s.strip()]
    elif isinstance(spec, (list, tuple)):
        names = list(spec)
    else:
        problems.append(f"{what}: rights must be a list of names or a comma-separated string")
        return Cap.NONE
    bits = Cap.NONE
    for n in names:
        key = n.strip().upper() if isinstance(n, str) else None
        if key is None or key not in _RIGHTS:
            problems.append(f"{what}: unknown right {n!r} (known: {', '.join(_RIGHTS)})")
            continue
        bits |= _RIGHTS[key]
    if bits == Cap.NONE and not any(p.startswith(what) for p in problems):
        problems.append(f"{what}: holds no right")
    return bits


def parse_settings(raw: Any) -> ZxvSettings:
    """Validates a ``ZXV`` setting (``None`` means all defaults). Raises :class:`ZxvSettingsError`."""
    if raw is None:
        raw = {}
    if not isinstance(raw, Mapping):
        raise ZxvSettingsError(["ZXV must be a dict"])
    problems: list[str] = []
    for key in raw:
        if key not in _KNOWN:
            problems.append(f"{key}: unknown key (known: {', '.join(sorted(_KNOWN))})")

    currencies = raw.get("CURRENCIES", ())
    if isinstance(currencies, str) or not isinstance(currencies, (list, tuple)):
        problems.append("CURRENCIES: must be a list of ISO 4217 codes")
        currencies = ()
    for c in currencies:
        if not isinstance(c, str) or not _CCY.match(c):
            problems.append(f"CURRENCIES: {c!r} is not an ISO 4217 alpha-3 code")

    units_raw = raw.get("PRIVATE_UNITS", {})
    units: list[tuple[str, int]] = []
    if not isinstance(units_raw, Mapping):
        problems.append("PRIVATE_UNITS: must be a dict of code -> minor units, e.g. {'VFV': 2}")
    else:
        for code, mu in units_raw.items():
            if not isinstance(code, str) or not _PRIVATE.match(code):
                problems.append(f"PRIVATE_UNITS: {code!r} is not 3 to 8 upper-case letters and digits")
            if isinstance(mu, bool) or not isinstance(mu, int) or not 0 <= mu <= 8:
                problems.append(f"PRIVATE_UNITS: minor units of {code!r} must be an int 0..8")
            units.append((str(code), mu if isinstance(mu, int) else 0))

    root = Cap.ALL if "ROOT_RIGHTS" not in raw else _rights(raw["ROOT_RIGHTS"], "ROOT_RIGHTS", problems)

    caps: dict[str, Cap] = {}
    caps_raw = raw.get("CAPABILITIES", {})
    if not isinstance(caps_raw, Mapping):
        problems.append("CAPABILITIES: must be a dict of name -> rights")
        caps_raw = {}
    for name, spec in caps_raw.items():
        if not isinstance(name, str) or not _NAME.match(name):
            problems.append(f"CAPABILITIES: {name!r} is not a valid name (a letter, then letters, digits, '-' or '_')")
            continue
        if name.lower() == "root":
            problems.append("CAPABILITIES: 'root' is reserved; the root capability is never handed out")
            continue
        bits = _rights(spec, f"CAPABILITIES.{name}", problems)
        if bits and bits & ~root:
            problems.append(f"CAPABILITIES.{name}: asks for {', '.join(right_names(bits & ~root))}, which ROOT_RIGHTS does not hold")
        caps[name] = bits

    policy_raw = raw.get("FATAL_POLICY", "raise")
    try:
        policy = FatalPolicy(policy_raw)
    except ValueError:
        problems.append(f"FATAL_POLICY: {policy_raw!r} is not 'raise' or 'abort'")
        policy = FatalPolicy.RAISE

    path = raw.get("LIBRARY_PATH")
    if path is not None and (not isinstance(path, str) or not path):
        problems.append("LIBRARY_PATH: must be a non-empty string or None")

    if problems:
        raise ZxvSettingsError(problems)
    return ZxvSettings(
        currencies=tuple(currencies),
        private_units=tuple(units),
        root_rights=root,
        capabilities=caps,
        fatal_policy=policy,
        library_path=path,
    )
