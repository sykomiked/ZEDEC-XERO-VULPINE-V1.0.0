# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Base class for objects that own a native handle (internal)."""

from __future__ import annotations

import threading
import weakref
from typing import Any, List, Optional

from . import _native
from .errors import ZxvUsageError

__all__ = ["NativeHandle"]


class NativeHandle:
    """Owns one C handle; closes its dependents first, then itself.

    * ``close()`` is idempotent and also runs from ``__exit__`` and, as a
      backstop, from ``__del__``.
    * A handle keeps a strong reference to its parent, so the garbage
      collector can never free a parent (a context, a capability, a model)
      before the children that point into it (R6).
    * Closing a parent closes every dependent that is still open, newest
      first, so explicit ``close()`` order never matters.
    """

    _h: Any = None
    _closed: bool = True

    def __init__(self, handle: Any, parent: Optional["NativeHandle"], context: Any) -> None:
        self._h = handle
        self._parent = parent
        self._context = context
        self._closed = False
        self._close_lock = threading.Lock()
        self._dependents: List["weakref.ref[NativeHandle]"] = []
        if parent is not None:
            parent._add_dependent(self)

    # subclasses implement
    def _destroy(self, handle: Any) -> None:  # pragma: no cover - abstract
        raise NotImplementedError

    def _add_dependent(self, child: "NativeHandle") -> None:
        with self._close_lock:
            self._dependents = [r for r in self._dependents if r() is not None]
            self._dependents.append(weakref.ref(child))

    @property
    def closed(self) -> bool:
        """True after :meth:`close`."""
        return self._closed

    def _handle(self) -> Any:
        """The live handle, after checking it is open and its context is alive."""
        if self._closed:
            raise ZxvUsageError(-1, "ZXV_E_ARG", f"{type(self).__name__} is closed")
        if self._context is not None:
            self._context._require_alive()
        return self._h

    def _check(self, rc: int) -> None:
        _native.check(rc, self._context)

    def close(self) -> None:
        """Release the native handle (and any dependents still open). Idempotent."""
        with self._close_lock:
            if self._closed:
                return
            self._closed = True
            deps = [r() for r in reversed(self._dependents)]
            self._dependents = []
        for d in deps:
            if d is not None:
                d.close()
        h, self._h = self._h, None
        if h is not None:
            self._destroy(h)

    def __enter__(self) -> Any:
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            if not self._closed:
                self.close()
        except Exception:  # pragma: no cover - interpreter shutdown
            pass
