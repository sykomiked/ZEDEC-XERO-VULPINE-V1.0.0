# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Loading ``libzxv`` and calling it (internal).

The library is opened lazily, on the first call that needs it, so the pure
Python parts of the SDK (UBH-168 framing, :class:`zxv.Truth`) work without
the native library. Search order (SDK_DESIGN.md section 1):

1. an explicit path given to :func:`zxv.load_library`;
2. the ``ZXV_LIBRARY_PATH`` environment variable (a file, or a directory
   holding the library);
3. the copy bundled in the wheel under ``zxv/_lib/``;
4. the system loader path (``LD_LIBRARY_PATH``, ``DYLD_LIBRARY_PATH``,
   ``PATH``, the ld cache).

The loaded library must report ABI major 1 and minor >= 4 (the ``_cap``
forms added in 1.2, the hexagram tag added in 1.3 and the hardware
emulation matrix added in 1.4).
"""

from __future__ import annotations

import ctypes.util
import os
import sys
import threading
from pathlib import Path
from typing import Any, Callable, List, NoReturn, Optional, Tuple

import cffi

from . import errors
from ._cdef import CDEF

ffi = cffi.FFI()
ffi.cdef(CDEF)

ABI_MAJOR = 1
ABI_MIN_MINOR = 4

_lib: Any = None
_lib_path: Optional[str] = None
_load_lock = threading.Lock()


def _file_names() -> List[str]:
    if sys.platform == "win32":
        return ["zxv.dll", "libzxv.dll"]
    if sys.platform == "darwin":
        return ["libzxv.dylib", "libzxv.so"]
    return ["libzxv.so"]


def _candidates(explicit: Optional[str]) -> List[Tuple[str, str]]:
    """(source, path-or-name) pairs in search order."""
    out: List[Tuple[str, str]] = []

    def add_path(source: str, p: str) -> None:
        path = Path(p)
        if path.is_dir():
            for name in _file_names():
                out.append((source, str(path / name)))
        else:
            out.append((source, str(path)))

    if explicit:
        add_path("explicit path", explicit)
        return out
    env = os.environ.get("ZXV_LIBRARY_PATH")
    if env:
        add_path("ZXV_LIBRARY_PATH", env)
    bundled = Path(__file__).resolve().parent / "_lib"
    for name in _file_names():
        if (bundled / name).exists():
            out.append(("bundled", str(bundled / name)))
    found = ctypes.util.find_library("zxv")
    if found:
        out.append(("system", found))
    for name in _file_names():
        out.append(("system", name))
    return out


def load_library(path: Optional[str] = None) -> Any:
    """Load ``libzxv`` now and return the cffi library object.

    Args:
        path: an explicit library file or directory. When omitted, the search
            order in this module's docstring is used.

    Raises:
        ZxvLibraryError: no library was found, or its ABI version is not 1.x
            with x >= 4, or a different library is already loaded.
    """
    global _lib, _lib_path
    with _load_lock:
        if _lib is not None:
            loaded = os.path.realpath(_lib_path or "")
            if path and os.path.realpath(path) not in (loaded, os.path.dirname(loaded)):
                raise errors.ZxvLibraryError(
                        f"libzxv is already loaded from {_lib_path}; load_library({path!r}) "
                        "must be called before any other ZXV call"
                    )
            return _lib
        tried: List[str] = []
        for source, cand in _candidates(path):
            if os.sep in cand and not os.path.exists(cand):
                tried.append(f"{source}: {cand} (missing)")
                continue
            try:
                lib = ffi.dlopen(cand)
            except OSError as exc:
                tried.append(f"{source}: {cand} ({exc})")
                continue
            _check_version(lib, cand)
            _lib = lib
            _lib_path = cand
            return lib
        raise errors.ZxvLibraryError(
            "libzxv not found. Pass a path to zxv.load_library(), set ZXV_LIBRARY_PATH, "
            "or install a wheel that bundles it. Tried:\n  " + "\n  ".join(tried)
        )


def _check_version(lib: Any, where: str) -> None:
    v = ffi.new("uint32_t[3]")
    rc = lib.zxv_version(v, v + 1, v + 2)
    if rc != 0 or v[0] != ABI_MAJOR or v[1] < ABI_MIN_MINOR:
        raise errors.ZxvLibraryError(
            f"{where} reports ABI {v[0]}.{v[1]}.{v[2]}; this SDK needs "
            f"{ABI_MAJOR}.x with x >= {ABI_MIN_MINOR}"
        )


def lib() -> Any:
    """The loaded library, loading it on first use."""
    return _lib if _lib is not None else load_library()


def library_path() -> Optional[str]:
    """Where the library was loaded from (``None`` before the first load)."""
    return _lib_path


# ---------------------------------------------------------------- errors


def last_error() -> str:
    """The calling thread's ``zxv_last_error`` text (R7)."""
    L = lib()
    n = ffi.new("size_t *")
    L.zxv_last_error(ffi.NULL, 0, n)
    buf = ffi.new("char[]", n[0] + 1)
    if L.zxv_last_error(buf, n[0] + 1, n) != 0:
        return ""
    return str(ffi.string(buf).decode("utf-8", "replace"))


def status_name(status: int) -> str:
    """Symbolic name of a status, e.g. ``"ZXV_E_FUNDS"``."""
    return str(ffi.string(lib().zxv_status_name(status)).decode("ascii"))


def status_class(status: int) -> errors.StatusClass:
    """The :class:`StatusClass` of a status code."""
    c = lib().zxv_status_class(status)
    try:
        return errors.StatusClass(c)
    except ValueError:  # R8: unknown is an error
        return errors.StatusClass.SYSTEM


def make_error(status: int, detail: Optional[str] = None) -> BaseException:
    """Build (do not raise) the exception for a negative status."""
    if detail is None:
        detail = last_error()
    cls = errors.error_type_for(status, status_class(status))
    if cls is errors.FloatRejectedError:
        return errors.FloatRejectedError(detail=detail or None)
    exc: BaseException = cls(status, status_name(status), detail)
    return exc


class _FatalSink:
    """Something that is told when a FATAL status is seen (a Context)."""

    def _on_fatal(self, exc: errors.ZxvFatal) -> None:  # pragma: no cover - interface
        raise NotImplementedError


_default_policy = errors.FatalPolicy.RAISE


def set_default_fatal_policy(policy: errors.FatalPolicy) -> None:
    """Set the process-wide default :class:`FatalPolicy` for new contexts."""
    global _default_policy
    if not isinstance(policy, errors.FatalPolicy):
        raise TypeError("policy must be a FatalPolicy")
    _default_policy = policy


def default_fatal_policy() -> errors.FatalPolicy:
    """The process-wide default :class:`FatalPolicy`."""
    return _default_policy


def check(rc: int, owner: Any = None) -> None:
    """Raise the mapped exception for a negative status; no-op on 0.

    ``owner`` is the Context (or an object with ``_on_fatal``) whose handle
    made the call; on FATAL it is marked dead (and may abort the process).
    """
    if rc >= 0:
        return
    exc = make_error(rc)
    if isinstance(exc, errors.ZxvFatal):
        raise_fatal(exc, owner)
    raise exc


def raise_fatal(exc: errors.ZxvFatal, owner: Any = None) -> NoReturn:
    """Mark the owner dead, apply the fatal policy, raise ``exc``."""
    policy = _default_policy
    if owner is not None:
        policy = owner._on_fatal(exc)
    if policy is errors.FatalPolicy.ABORT:
        try:
            sys.stderr.write(f"zxv: FATAL {exc}; FatalPolicy.ABORT -> os.abort()\n")
            sys.stderr.flush()
        finally:
            os.abort()
    raise exc


# ---------------------------------------------------------------- R4 helpers


def read_text(call: Callable[..., int], owner: Any = None) -> str:
    """Run an R4 two-call text function ``call(buf, cap, out_len)``."""
    n = ffi.new("size_t *")
    rc = call(ffi.NULL, 0, n)
    if rc == 0:
        return ""
    if rc != -2:
        check(rc, owner)
    while True:
        buf = ffi.new("char[]", n[0] + 1)
        rc = call(buf, n[0] + 1, n)
        if rc == 0:
            return str(ffi.unpack(buf, n[0]).decode("utf-8"))
        if rc != -2:
            check(rc, owner)


def to_cstr(s: str, what: str = "text") -> bytes:
    """Encode a Python str to NUL-free UTF-8 for a ``const char *``."""
    if not isinstance(s, str):
        if isinstance(s, float):
            raise errors.FloatRejectedError(what)
        raise TypeError(f"{what} must be str, not {type(s).__name__}")
    b = s.encode("utf-8")
    if b"\0" in b:
        raise errors.ZxvUsageError(-1, "ZXV_E_ARG", f"{what} contains a NUL character")
    return b


def to_bytes(b: Any, what: str = "data") -> bytes:
    """Accept bytes-like input (bytes, bytearray, memoryview) only."""
    if isinstance(b, (bytes, bytearray, memoryview)):
        return bytes(b)
    if isinstance(b, float):
        raise errors.FloatRejectedError(what)
    raise TypeError(f"{what} must be bytes-like, not {type(b).__name__}")


def exact_int(v: Any, what: str, lo: int, hi: int) -> int:
    """Check an exact integer in [lo, hi]; floats raise FloatRejectedError."""
    if isinstance(v, float):
        raise errors.FloatRejectedError(what)
    if isinstance(v, bool) or not isinstance(v, int):
        # Allow int subclasses like IntEnum, refuse bool (a likely mistake).
        if isinstance(v, bool) or not hasattr(v, "__index__"):
            raise TypeError(f"{what} must be an int, not {type(v).__name__}")
        v = v.__index__()
    if not lo <= v <= hi:
        raise errors.ZxvUsageError(-8, "ZXV_E_RANGE", f"{what}={v} is outside {lo}..{hi}")
    return int(v)


U32 = (0, 2**32 - 1)
U64 = (0, 2**64 - 1)
I32 = (-(2**31), 2**31 - 1)
I64 = (-(2**63), 2**63 - 1)
