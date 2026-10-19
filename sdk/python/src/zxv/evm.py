# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""EVM state channels: ledger balances encoded for a Solidity contract.

The encoding is ``abi.encode(bytes32 channelId, uint256 nonce,
address[] parties, int256[] balances)`` and the digest is its Keccak-256.
The parties sign the digest with their own Ethereum keys (EIP-191) in the
host wallet: ZXV has no secp256k1.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Sequence

from . import _native
from ._native import ffi
from .context import Capability

__all__ = ["EvmState", "StateChannel"]


@dataclass(frozen=True)
class EvmState:
    """An encoded state and its Keccak-256 digest."""

    encoding: bytes
    digest: bytes


def _bytes_n(b: Any, n: int, what: str) -> bytes:
    v = _native.to_bytes(b, what)
    if len(v) != n:
        raise ValueError(f"{what} must be {n} bytes")
    return v


class StateChannel:
    """Encodes state-channel states through a capability (``Cap.EVM``)."""

    def __init__(self, cap: Capability, channel_id: bytes) -> None:
        """A channel with a 32-byte id."""
        self._cap = cap
        self._id = _bytes_n(channel_id, 32, "channel_id")

    @property
    def channel_id(self) -> bytes:
        """The 32-byte channel id."""
        return self._id

    def _run(self, call: Callable[[Any, int, Any, Any], int]) -> EvmState:
        n = ffi.new("size_t *")
        dg = ffi.new("uint8_t[32]")
        rc = call(ffi.NULL, 0, n, dg)
        if rc != -2:
            _native.check(rc, self._cap.context)
        buf = ffi.new("uint8_t[]", n[0])
        _native.check(call(buf, n[0], n, dg), self._cap.context)
        return EvmState(bytes(ffi.buffer(buf, n[0])), bytes(ffi.buffer(dg, 32)))

    @staticmethod
    def _addresses(addresses: Sequence[bytes]) -> bytes:
        return b"".join(_bytes_n(a, 20, "address") for a in addresses)

    def encode_state(self, nonce: int, addresses: Sequence[bytes], balances: Sequence[int]) -> EvmState:
        """Encode explicit signed balances (int64 minor units) for the parties."""
        if len(addresses) != len(balances):
            raise ValueError("addresses and balances must have the same length")
        addr = self._addresses(addresses)
        bal = ffi.new("int64_t[]", [_native.exact_int(b, "balance", *_native.I64) for b in balances] or [0])
        nn = _native.exact_int(nonce, "nonce", *_native.U64)
        c = self._cap._handle()
        L = _native.lib()
        return self._run(lambda b, cap, n, d: L.zxv_evm_encode_state(
            c, self._id, nn, addr, bal, len(balances), b, cap, n, d))

    def ledger_state(self, nonce: int, currency: str, accounts: Sequence[int],
                     addresses: Sequence[bytes]) -> EvmState:
        """Encode each account's EQUITY (888) balance (needs ``EVM`` and ``LEDGER_READ``)."""
        if len(addresses) != len(accounts):
            raise ValueError("addresses and accounts must have the same length")
        addr = self._addresses(addresses)
        acc = ffi.new("uint32_t[]", [_native.exact_int(a, "account", *_native.U32) for a in accounts] or [0])
        nn = _native.exact_int(nonce, "nonce", *_native.U64)
        ccy = _native.to_cstr(currency, "currency")
        c = self._cap._handle()
        L = _native.lib()
        return self._run(lambda b, cap, n, d: L.zxv_evm_ledger_state(
            c, self._id, nn, ccy, acc, addr, len(accounts), b, cap, n, d))
