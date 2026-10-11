# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Hashes, randomness, secret keys, and ML-KEM-1024 / ML-DSA-87 / Ed25519.

Secret keys and shared secrets live in :class:`SecretKey`: a buffer
allocated by cffi outside Python's object heap and wiped with
``zxv_secure_zero`` on :meth:`SecretKey.close`, on ``__exit__``, and from
``__del__`` as a backstop. The implementations are NOT FIPS 140-3
validated; see ``sdk/docs/SECURITY.md``.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass
from typing import Any, Optional, Tuple

from . import _native
from ._native import ffi
from .context import Capability

__all__ = [
    "Hashes", "random_bytes", "SecretKey", "KeyPair",
    "Ed25519", "MlDsa87", "MlKem1024", "Encapsulation",
]


# ----------------------------------------------------------------- hashes


class Hashes:
    """SHA3-256, SHAKE256 and Ethereum's Keccak-256 (pure functions)."""

    @staticmethod
    def sha3_256(data: bytes) -> bytes:
        """FIPS 202 SHA3-256 digest (32 bytes)."""
        d = _native.to_bytes(data)
        out = ffi.new("uint8_t[32]")
        _native.check(_native.lib().zxv_sha3_256(d, len(d), out))
        return bytes(ffi.buffer(out, 32))

    @staticmethod
    def shake256(data: bytes, length: int) -> bytes:
        """FIPS 202 SHAKE256 output of ``length`` bytes."""
        d = _native.to_bytes(data)
        n = _native.exact_int(length, "length", 0, 1 << 30)
        out = ffi.new("uint8_t[]", max(n, 1))
        _native.check(_native.lib().zxv_shake256(d, len(d), out, n))
        return bytes(ffi.buffer(out, n))

    @staticmethod
    def keccak256(data: bytes) -> bytes:
        """Ethereum Keccak-256 (pre-FIPS padding), 32 bytes."""
        d = _native.to_bytes(data)
        out = ffi.new("uint8_t[32]")
        _native.check(_native.lib().zxv_keccak256(d, len(d), out))
        return bytes(ffi.buffer(out, 32))


def random_bytes(n: int) -> bytes:
    """``n`` bytes from the OS CSPRNG (via ``zxv_random``)."""
    k = _native.exact_int(n, "n", 0, 1 << 30)
    out = ffi.new("uint8_t[]", max(k, 1))
    _native.check(_native.lib().zxv_random(out, k))
    return bytes(ffi.buffer(out, k))


# ----------------------------------------------------------------- secrets


class SecretKey:
    """Secret bytes held in a cffi-owned buffer and wiped on close.

    The buffer is allocated with ``ffi.new`` (C heap, not a Python ``bytes``)
    and handed to ``libzxv`` directly, so the secret never becomes a Python
    object unless you call :meth:`export_bytes_unsafe`. Use it as a context
    manager, or call :meth:`close`; ``__del__`` wipes as a backstop.

    What cannot be guaranteed: bytes you passed in from Python (for
    :meth:`from_bytes`) may still exist in the Python heap; the OS may have
    swapped the page; a core dump may contain it.
    """

    __slots__ = ("_buf", "_len", "_kind", "_lock", "__weakref__")

    def __init__(self, length: int, kind: str = "secret") -> None:
        """Allocate ``length`` zeroed bytes (normally done by the SDK for you)."""
        self._buf: Any = ffi.new("uint8_t[]", length)
        self._len = length
        self._kind = kind
        self._lock = threading.Lock()

    @classmethod
    def from_bytes(cls, data: bytes, kind: str = "secret") -> "SecretKey":
        """Copy key material into a new SecretKey (the source copy is not wiped)."""
        b = _native.to_bytes(data, "secret key")
        k = cls(len(b), kind)
        ffi.memmove(k._buf, b, len(b))
        return k

    @property
    def kind(self) -> str:
        """What the key is, e.g. ``"ed25519"`` or ``"ml-kem-1024 shared secret"``."""
        return self._kind

    @property
    def closed(self) -> bool:
        """True after :meth:`close`."""
        return self._buf is None

    def __len__(self) -> int:
        return self._len

    def _cdata(self) -> Any:
        buf = self._buf
        if buf is None:
            raise ValueError(f"{self._kind} key is closed (wiped)")
        return buf

    def export_bytes_unsafe(self) -> bytes:
        """Copy the secret into a Python ``bytes``. This defeats the wiping."""
        return bytes(ffi.buffer(self._cdata(), self._len))

    def equals(self, other: "SecretKey") -> bool:
        """Constant-time comparison with another SecretKey."""
        a = self._cdata()
        b = other._cdata()
        if self._len != other._len:
            return False
        acc = 0
        for i in range(self._len):  # no early exit; never copies into a bytes object
            acc |= a[i] ^ b[i]
        return acc == 0

    def close(self) -> None:
        """Wipe the buffer with ``zxv_secure_zero`` and release it. Idempotent."""
        with self._lock:
            buf, self._buf = self._buf, None
        if buf is not None:
            try:
                _native.lib().zxv_secure_zero(buf, self._len)
            except Exception:  # pragma: no cover - library unloaded at shutdown
                ffi.memmove(buf, bytes(self._len), self._len)

    def __enter__(self) -> "SecretKey":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:  # pragma: no cover
            pass

    def __repr__(self) -> str:
        state = "wiped" if self._buf is None else f"{self._len} bytes"
        return f"<zxv.SecretKey {self._kind} {state}>"

    def __reduce__(self) -> Any:
        raise TypeError("SecretKey cannot be pickled")


@dataclass
class KeyPair:
    """A public key (bytes) and its :class:`SecretKey`. Closing wipes the secret."""

    public_key: bytes
    secret_key: SecretKey

    def close(self) -> None:
        """Wipe the secret key."""
        self.secret_key.close()

    def __enter__(self) -> "KeyPair":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()


def _fixed(data: Any, n: int, what: str) -> bytes:
    b = _native.to_bytes(data, what)
    if len(b) != n:
        raise ValueError(f"{what} must be {n} bytes, got {len(b)}")
    return b


def _secret(sk: SecretKey, n: int, what: str) -> Any:
    if not isinstance(sk, SecretKey):
        raise TypeError(f"{what} must be a SecretKey (use SecretKey.from_bytes)")
    if len(sk) != n:
        raise ValueError(f"{what} must be {n} bytes")
    return sk._cdata()


class _Signer:
    def __init__(self, cap: Capability) -> None:
        """Use ``cap`` for every call (signing needs ``Cap.CRYPTO_SIGN``)."""
        self._cap = cap

    def _c(self) -> Any:
        return self._cap._handle()

    def _check(self, rc: int) -> None:
        _native.check(rc, self._cap.context)


# ----------------------------------------------------------------- Ed25519


class Ed25519(_Signer):
    """RFC 8032 Ed25519 through a capability (``Cap.CRYPTO_SIGN``)."""

    PUBLIC_KEY_BYTES = 32
    SECRET_KEY_BYTES = 32
    SIGNATURE_BYTES = 64

    def generate(self) -> KeyPair:
        """A fresh key pair from the OS CSPRNG."""
        pk = ffi.new("uint8_t[32]")
        sk = SecretKey(32, "ed25519")
        self._check(_native.lib().zxv_ed25519_keygen(self._c(), pk, sk._cdata()))
        return KeyPair(bytes(ffi.buffer(pk, 32)), sk)

    def public_key(self, secret_key: SecretKey) -> bytes:
        """The public key of a 32-byte RFC 8032 private key (seed)."""
        pk = ffi.new("uint8_t[32]")
        self._check(_native.lib().zxv_ed25519_public(self._c(), _secret(secret_key, 32, "secret_key"), pk))
        return bytes(ffi.buffer(pk, 32))

    def sign(self, secret_key: SecretKey, message: bytes) -> bytes:
        """A 64-byte signature of ``message``."""
        m = _native.to_bytes(message, "message")
        sig = ffi.new("uint8_t[64]")
        self._check(_native.lib().zxv_ed25519_sign(self._c(), _secret(secret_key, 32, "secret_key"),
                                                   m, len(m), sig))
        return bytes(ffi.buffer(sig, 64))

    def verify(self, public_key: bytes, message: bytes, signature: bytes) -> bool:
        """True if valid, False if not (``ZXV_E_VERIFY``)."""
        m = _native.to_bytes(message, "message")
        rc = _native.lib().zxv_ed25519_verify(self._c(), _fixed(public_key, 32, "public_key"), m, len(m),
                                              _fixed(signature, 64, "signature"))
        if rc == -21:
            return False
        self._check(rc)
        return True


# ----------------------------------------------------------------- ML-DSA-87


class MlDsa87(_Signer):
    """FIPS 204 ML-DSA-87 through a capability (``Cap.CRYPTO_SIGN``)."""

    PUBLIC_KEY_BYTES = 2592
    SECRET_KEY_BYTES = 4896
    SIGNATURE_BYTES = 4627
    CONTEXT_MAX = 255

    def generate(self) -> KeyPair:
        """A fresh key pair."""
        pk = ffi.new("uint8_t[]", 2592)
        sk = SecretKey(4896, "ml-dsa-87")
        self._check(_native.lib().zxv_mldsa87_keygen(self._c(), pk, sk._cdata()))
        return KeyPair(bytes(ffi.buffer(pk, 2592)), sk)

    def generate_from_seed(self, seed: SecretKey) -> KeyPair:
        """The deterministic key pair of a 32-byte seed (for tests / KATs)."""
        pk = ffi.new("uint8_t[]", 2592)
        sk = SecretKey(4896, "ml-dsa-87")
        self._check(_native.lib().zxv_mldsa87_keygen_seed(self._c(), _secret(seed, 32, "seed"), pk, sk._cdata()))
        return KeyPair(bytes(ffi.buffer(pk, 2592)), sk)

    def _ctx(self, context: bytes) -> Tuple[Any, int]:
        c = _native.to_bytes(context, "context")
        if len(c) > 255:
            raise ValueError("ML-DSA context is at most 255 bytes")
        return (c if c else ffi.NULL), len(c)

    def sign(self, secret_key: SecretKey, message: bytes, context: bytes = b"") -> bytes:
        """A hedged (randomized) signature; ``context`` is the FIPS 204 context string."""
        m = _native.to_bytes(message, "message")
        c, cl = self._ctx(context)
        sig = ffi.new("uint8_t[]", 4627)
        self._check(_native.lib().zxv_mldsa87_sign(self._c(), _secret(secret_key, 4896, "secret_key"),
                                                   m, len(m), c, cl, sig))
        return bytes(ffi.buffer(sig, 4627))

    def sign_deterministic(self, secret_key: SecretKey, message: bytes, context: bytes = b"") -> bytes:
        """A deterministic signature (same input, same signature), for KATs."""
        m = _native.to_bytes(message, "message")
        c, cl = self._ctx(context)
        sig = ffi.new("uint8_t[]", 4627)
        self._check(_native.lib().zxv_mldsa87_sign_deterministic(
            self._c(), _secret(secret_key, 4896, "secret_key"), m, len(m), c, cl, sig))
        return bytes(ffi.buffer(sig, 4627))

    def verify(self, public_key: bytes, message: bytes, signature: bytes, context: bytes = b"") -> bool:
        """True if valid, False if not."""
        m = _native.to_bytes(message, "message")
        c, cl = self._ctx(context)
        rc = _native.lib().zxv_mldsa87_verify(self._c(), _fixed(public_key, 2592, "public_key"), m, len(m),
                                              c, cl, _fixed(signature, 4627, "signature"))
        if rc == -21:
            return False
        self._check(rc)
        return True


# ----------------------------------------------------------------- ML-KEM-1024


@dataclass
class Encapsulation:
    """An ML-KEM ciphertext (public) and the shared secret (a :class:`SecretKey`)."""

    ciphertext: bytes
    shared_secret: SecretKey

    def close(self) -> None:
        """Wipe the shared secret."""
        self.shared_secret.close()

    def __enter__(self) -> "Encapsulation":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()


class MlKem1024(_Signer):
    """FIPS 203 ML-KEM-1024 through a capability (``Cap.CRYPTO_KEM``)."""

    ENCAPSULATION_KEY_BYTES = 1568
    DECAPSULATION_KEY_BYTES = 3168
    CIPHERTEXT_BYTES = 1568
    SHARED_SECRET_BYTES = 32

    def generate(self) -> KeyPair:
        """A fresh (encapsulation key, decapsulation key) pair."""
        ek = ffi.new("uint8_t[]", 1568)
        dk = SecretKey(3168, "ml-kem-1024")
        self._check(_native.lib().zxv_mlkem1024_keygen(self._c(), ek, dk._cdata()))
        return KeyPair(bytes(ffi.buffer(ek, 1568)), dk)

    def generate_derand(self, d: SecretKey, z: SecretKey) -> KeyPair:
        """Deterministic key generation from 32-byte ``d`` and ``z`` (for KATs)."""
        ek = ffi.new("uint8_t[]", 1568)
        dk = SecretKey(3168, "ml-kem-1024")
        self._check(_native.lib().zxv_mlkem1024_keygen_derand(
            self._c(), _secret(d, 32, "d"), _secret(z, 32, "z"), ek, dk._cdata()))
        return KeyPair(bytes(ffi.buffer(ek, 1568)), dk)

    def encapsulate(self, encapsulation_key: bytes, *, m: Optional[SecretKey] = None) -> Encapsulation:
        """Encapsulate to a public key. ``m`` (32 bytes) makes it deterministic, for KATs.

        Raises:
            ZxvUsageError: the key fails the FIPS 203 modulus check (``ZXV_E_FORMAT``).
        """
        ek = _fixed(encapsulation_key, 1568, "encapsulation_key")
        ct = ffi.new("uint8_t[]", 1568)
        ss = SecretKey(32, "ml-kem-1024 shared secret")
        L = _native.lib()
        if m is None:
            rc = L.zxv_mlkem1024_encaps(self._c(), ek, ct, ss._cdata())
        else:
            rc = L.zxv_mlkem1024_encaps_derand(self._c(), ek, _secret(m, 32, "m"), ct, ss._cdata())
        self._check(rc)
        return Encapsulation(bytes(ffi.buffer(ct, 1568)), ss)

    def decapsulate(self, decapsulation_key: SecretKey, ciphertext: bytes) -> SecretKey:
        """The shared secret of a ciphertext."""
        ss = SecretKey(32, "ml-kem-1024 shared secret")
        self._check(_native.lib().zxv_mlkem1024_decaps(
            self._c(), _secret(decapsulation_key, 3168, "decapsulation_key"),
            _fixed(ciphertext, 1568, "ciphertext"), ss._cdata()))
        return ss
