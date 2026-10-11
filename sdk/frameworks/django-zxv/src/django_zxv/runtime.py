# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The process's one ZXV context, its root and its named capabilities."""

from __future__ import annotations

import atexit
import threading
from typing import Literal, Optional

import zxv
from zxv import Cap, Capability, Context

from .conf import ZxvSettings

State = Literal["live", "halted", "closed", "unknown"]


class ZxvRuntime:
    """Owns the context. Created by :class:`django_zxv.apps.ZxvConfig` at start-up.

    The root capability is kept in a name-mangled attribute and never handed
    out; :meth:`capability` returns only the named ones. :meth:`close` tears
    down in ABI.md R6 order: named capabilities (newest first), the root,
    then the context.
    """

    def __init__(self, settings: ZxvSettings) -> None:
        if settings.library_path is not None:
            zxv.load_library(settings.library_path)
        self.context = Context(fatal_policy=settings.fatal_policy)
        try:
            if settings.currencies or settings.private_units:
                # Configuration needs LEDGER_POST whatever the root holds: a
                # short-lived capability of its own, closed before anything else exists.
                with self.context.root_capability(Cap.LEDGER_POST) as setup:
                    for code in settings.currencies:
                        self.context.currencies.enable(code, cap=setup)
                    for code, mu in settings.private_units:
                        self.context.currencies.register_private(code, mu, cap=setup)
            self.__root = self.context.root_capability(settings.root_rights)
            self.__named: dict[str, Capability] = {
                name: self.__root.attenuate(bits) for name, bits in settings.capabilities.items()
            }
        except BaseException:
            self.context.close()
            raise
        self._lock = threading.Lock()

    @property
    def names(self) -> list[str]:
        """The declared capability names, in declaration order."""
        return list(self.__named)

    def capability(self, name: str) -> Capability:
        """The capability declared as ``name`` in ``ZXV["CAPABILITIES"]``; never the root.

        Raises:
            KeyError: no such name.
        """
        try:
            return self.__named[name]
        except KeyError:
            raise KeyError(f"no ZXV capability named {name!r} is declared in settings.ZXV['CAPABILITIES']") from None

    def state(self) -> State:
        """The context's state, without raising. Fail-closed: anything unexpected is ``unknown``."""
        try:
            if self.context.closed:
                return "closed"
            return "halted" if self.context.dead or self.context.is_poisoned else "live"
        except BaseException:  # noqa: BLE001 - a health probe must never raise
            return "unknown"

    def close(self) -> None:
        """Closes named capabilities (newest first), the root, then the context. Idempotent."""
        with self._lock:
            for cap in reversed(list(self.__named.values())):
                cap.close()
            self.__root.close()
            self.context.close()


_runtime: Optional[ZxvRuntime] = None
_guard = threading.Lock()
_atexit = False


def install(settings: ZxvSettings) -> ZxvRuntime:
    """Creates the runtime once per process (the app config calls this)."""
    global _runtime, _atexit
    with _guard:
        if _runtime is None or _runtime.context.closed:
            _runtime = ZxvRuntime(settings)
        if not _atexit:
            atexit.register(shutdown)
            _atexit = True
        return _runtime


def current() -> ZxvRuntime:
    """The runtime. Raises ``RuntimeError`` before ``django_zxv`` is ready or after :func:`shutdown`."""
    rt = _runtime
    if rt is None:
        raise RuntimeError("django_zxv is not ready: add 'django_zxv' to INSTALLED_APPS and call django.setup()")
    return rt


def shutdown() -> None:
    """Closes the runtime (R6 order). Safe to call twice; registered with ``atexit``."""
    global _runtime
    with _guard:
        rt, _runtime = _runtime, None
    if rt is not None:
        rt.close()
