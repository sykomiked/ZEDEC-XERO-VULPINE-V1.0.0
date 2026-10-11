# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""django-zxv: Django integration over the ZXV Python SDK.

.. code-block:: python

    INSTALLED_APPS = [..., "django_zxv"]
    ZXV = {"CURRENCIES": ["NGN"], "CAPABILITIES": {"teller": ["LEDGER_READ", "LEDGER_POST"]}}

    import django_zxv, zxv
    ledger = zxv.Ledger(django_zxv.get_capability("teller"))
"""

from __future__ import annotations

from zxv import Capability, Context

from . import runtime as _runtime

__version__ = "0.1.0"


def get_context() -> Context:
    """The process's ZXV context (owned by the app config; do not close it)."""
    return _runtime.current().context


def get_capability(name: str) -> Capability:
    """The capability declared as ``name`` in ``ZXV["CAPABILITIES"]``. The root has no name.

    Raises:
        KeyError: no capability of that name is declared.
    """
    return _runtime.current().capability(name)


def capability_names() -> list[str]:
    """The declared capability names, in declaration order."""
    return _runtime.current().names


def shutdown() -> None:
    """Closes the context now, in ABI.md R6 order. Idempotent; also runs at exit."""
    _runtime.shutdown()


__all__ = ["get_context", "get_capability", "capability_names", "shutdown", "__version__"]
