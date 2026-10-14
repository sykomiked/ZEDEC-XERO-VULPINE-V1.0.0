# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Content identifiers: IPFS CIDs, byte for byte what Kubo makes (ABI 1.5).

A :class:`Cid` names bytes by what they *are* (a hash), not where they live.
It is an immutable value holding one canonical CIDv0 or CIDv1 in binary form
(version, codec, multihash); anything else is refused with
:class:`~zxv.CidMismatchError` (``ZXV_E_CID``) when it is made.

Kubo identity (checked in the C tests against Kubo 0.32.1):

* :meth:`Cid.compute` is ``ipfs block put``: CIDv1, sha2-256 of the bytes as
  ONE block under the given codec;
* :meth:`Cid.unixfs` is ``ipfs add --cid-version=1``: 256 KiB chunks, raw
  leaves, a balanced DAG of 174 links. The two agree for files of at most
  262144 bytes;
* ``str(cid)`` prints what Kubo prints (``b...`` base32 for CIDv1, ``Qm...``
  for CIDv0); :meth:`Cid.to_text` with :attr:`CidBase.BASE58BTC` prints the
  ``z...`` AIPI form.

Everything here is pure: no context, no capability. Honest limits: a CID
proves which bytes are meant, not that they are available (it resolves only
while someone holds them) and not that a function's claimed output is right.
There is no network transport in libzxv.
"""

from __future__ import annotations

import enum
from typing import Any, Union, final

from . import _native
from ._native import ffi

__all__ = ["CID_MAX", "CID_TEXT_MAX", "CID_DIGEST_MAX", "CidCodec", "CidHash", "CidBase", "Cid",
           "CidLike"]

CID_MAX = 80
"""Longest binary CID accepted or produced, in bytes."""
CID_TEXT_MAX = 139
"""Longest CID text, in characters."""
CID_DIGEST_MAX = 64
"""Longest multihash digest (identity hashes), in bytes."""


class CidCodec(enum.IntEnum):
    """Well-known multicodec codes (any codec number is allowed in :meth:`Cid.compute`)."""

    RAW = 0x55
    """Plain bytes."""
    DAG_PB = 0x70
    """A UnixFS / dag-pb node (CIDv0 is always dag-pb)."""
    AIPI_FN = 0x3A1000
    """``zxv-fn``: an AIPI function record, intent 0 (private-use, unregistered)."""
    AIPI_TENSOR_INFER = 0x3A1001
    AIPI_SETTLE_LEDGER = 0x3A1002
    AIPI_SWARM_ROUTE = 0x3A1003
    AIPI_CALL = 0x3A10C0
    """``zxv-aipi-call``: an AIPI call record."""


class CidHash(enum.IntEnum):
    """Multihash function codes."""

    IDENTITY = 0x00
    """The bytes themselves (at most :data:`CID_DIGEST_MAX`)."""
    SHA2_256 = 0x12


class CidBase(enum.IntEnum):
    """Text forms of a CID (``ZXV_CID_BASE_*``)."""

    DEFAULT = 0
    """Kubo's form: CIDv1 ``b`` + base32, CIDv0 ``Qm`` base58."""
    BASE58BTC = 1
    """``z`` + base58btc, the AIPI form. CIDv1 only."""


def _cid_out(fn: Any, *args: Any) -> "Cid":
    out = ffi.new("uint8_t[]", CID_MAX)
    n = ffi.new("size_t *")
    _native.check(fn(*args, out, n))
    return Cid._trusted(bytes(ffi.buffer(out, n[0])))


@final
class Cid:
    """One canonical CID in binary form. Immutable, hashable, compared by bytes.

    Make one with :meth:`compute`, :meth:`unixfs`, :meth:`parse` (text) or
    ``Cid(binary)``. ``bytes(cid)`` is the binary form; ``str(cid)`` is the
    Kubo text form.

    Raises:
        CidMismatchError: (``ZXV_E_CID``) ``binary`` is not exactly one
            canonical CIDv0 or CIDv1 (sha2-256 or identity multihash).
    """

    __slots__ = ("_b", "_version", "_codec", "_hash", "_digest")
    _b: bytes
    _version: int
    _codec: int
    _hash: int
    _digest: bytes

    def __init__(self, binary: bytes) -> None:
        b = _native.to_bytes(binary, "binary")
        ver, codec, hsh = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        dig, dn = ffi.new("uint8_t[]", CID_DIGEST_MAX), ffi.new("size_t *")
        _native.check(_native.lib().zxv_cid_info(_native.cbuf(b), len(b), ver, codec, hsh, dig, dn))
        object.__setattr__(self, "_b", b)
        object.__setattr__(self, "_version", int(ver[0]))
        object.__setattr__(self, "_codec", int(codec[0]))
        object.__setattr__(self, "_hash", int(hsh[0]))
        object.__setattr__(self, "_digest", bytes(ffi.buffer(dig, dn[0])))

    @classmethod
    def _trusted(cls, b: bytes) -> "Cid":
        return cls(b)

    # ------------------------------------------------------------ makers

    @classmethod
    def compute(cls, data: bytes, codec: int = CidCodec.RAW) -> "Cid":
        """The CIDv1 of ``data`` as one block: sha2-256 under ``codec`` (Kubo ``ipfs block put``).

        Raises:
            ZxvUsageError: ``ZXV_E_TOO_LONG`` for more than 2**32 - 1 bytes.
        """
        d = _native.to_bytes(data, "data")
        c = _native.exact_int(codec, "codec", *_native.U32)
        return _cid_out(_native.lib().zxv_cid_compute, c, _native.cbuf(d), len(d))

    @classmethod
    def unixfs(cls, data: bytes) -> "Cid":
        """The root CID Kubo gives ``data`` as a file (``ipfs add --cid-version=1``):
        a raw CID up to 262144 bytes, a dag-pb root above. Releases the GIL."""
        d = _native.to_bytes(data, "data")
        return _cid_out(_native.lib().zxv_cid_unixfs, _native.cbuf(d), len(d))

    @classmethod
    def parse(cls, text: str) -> "Cid":
        """Parse ``b...`` (CIDv1 base32), ``Qm...`` (CIDv0) or ``z...`` (CIDv1 base58btc).

        Only the canonical text is accepted: no upper case, padding, other
        multibases or extra leading zeros.

        Raises:
            CidMismatchError: anything else (``ZXV_E_CID``).
            ZxvUsageError: empty text (``ZXV_E_ARG``) or over :data:`CID_TEXT_MAX`.
        """
        return _cid_out(_native.lib().zxv_cid_from_text, _native.to_cstr(text, "text"))

    # ------------------------------------------------------------ parts

    @property
    def binary(self) -> bytes:
        """The binary CID (1..:data:`CID_MAX` bytes)."""
        return self._b

    @property
    def version(self) -> int:
        """0 or 1."""
        return self._version

    @property
    def codec(self) -> int:
        """The multicodec (a :class:`CidCodec` value when it is a known one)."""
        return self._codec

    @property
    def hash_function(self) -> int:
        """The multihash function code (a :class:`CidHash` value)."""
        return self._hash

    @property
    def digest(self) -> bytes:
        """The multihash digest."""
        return self._digest

    def to_text(self, base: CidBase = CidBase.DEFAULT) -> str:
        """The CID as text.

        Raises:
            CidMismatchError: a CIDv0 in :attr:`CidBase.BASE58BTC` (it has no multibase form).
        """
        if not isinstance(base, CidBase):
            raise TypeError("base must be a CidBase")
        L = _native.lib()
        b = self._b
        cb = _native.cbuf(b)
        return _native.read_text(lambda buf, c, n: L.zxv_cid_to_text(cb, len(b), base.value, buf, c, n))

    def to_v1(self) -> "Cid":
        """The CIDv1 of a CIDv0 (dag-pb, same hash; Kubo ``ipfs cid base32``); a CIDv1 is returned as is."""
        return _cid_out(_native.lib().zxv_cid_to_v1, _native.cbuf(self._b), len(self._b))

    def verify(self, data: bytes) -> None:
        """Check that ``data`` is exactly the block this CID names (re-hashed).

        A UnixFS root names its root *block*, not the file: verify a file by
        comparing :meth:`unixfs` instead.

        Raises:
            CidMismatchError: the bytes do not match.
        """
        d = _native.to_bytes(data, "data")
        _native.check(_native.lib().zxv_cid_verify(_native.cbuf(self._b), len(self._b), _native.cbuf(d), len(d)))

    def matches(self, data: bytes) -> bool:
        """True if ``data`` is exactly the block this CID names."""
        d = _native.to_bytes(data, "data")
        rc = _native.lib().zxv_cid_verify(_native.cbuf(self._b), len(self._b), _native.cbuf(d), len(d))
        if rc == -34:
            return False
        _native.check(rc)
        return True

    def provider_key(self) -> bytes:
        """The 32-byte Kademlia key under which providers of this CID are announced
        on Vinea: SHA-256(``"zxv/aipi/v1/provider"`` || binary CID). Computing it
        announces nothing."""
        out = ffi.new("uint8_t[32]")
        _native.check(_native.lib().zxv_aipi_provider_key(_native.cbuf(self._b), len(self._b), out))
        return bytes(ffi.buffer(out, 32))

    # ------------------------------------------------------------ value semantics

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Cid is immutable")

    def __bytes__(self) -> bytes:
        return self._b

    def __len__(self) -> int:
        return len(self._b)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Cid) and other._b == self._b

    def __hash__(self) -> int:
        return hash(("Cid", self._b))

    def __str__(self) -> str:
        return self.to_text()

    def __repr__(self) -> str:
        return f"Cid({self.to_text()!r})"


CidLike = Union[Cid, bytes, str]
"""A :class:`Cid`, its binary form, or its text."""


def as_cid(c: CidLike, what: str = "cid") -> Cid:
    """Coerce a :data:`CidLike` (internal helper)."""
    if isinstance(c, Cid):
        return c
    if isinstance(c, str):
        return Cid.parse(c)
    if isinstance(c, (bytes, bytearray, memoryview)):
        return Cid(bytes(c))
    raise TypeError(f"{what} must be a Cid, bytes or str, not {type(c).__name__}")
