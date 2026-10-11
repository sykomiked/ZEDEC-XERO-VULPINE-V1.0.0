// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.evm;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.ledger.Account;

/**
 * Encodes exact ledger balances for a Solidity state-channel contract
 * ({@code sdk/solidity/ZxvStateChannel.sol}). ZXV has no secp256k1: the
 * parties sign {@link EvmState#digest()} with their own Ethereum keys
 * (EIP-191) in the host wallet. Needs {@code EVM} (and {@code LEDGER_READ}
 * for {@link #ledgerState}).
 */
public final class StateChannel {
    private final Capability cap;
    private final ZxvContext ctx;

    private StateChannel(Capability cap) {
        this.cap = cap;
        this.ctx = Caps.ctx(cap);
    }

    /** State-channel encoding gated by {@code cap}. */
    public static StateChannel with(Capability cap) {
        return new StateChannel(cap);
    }

    /**
     * Encodes explicit balances.
     *
     * @param channelId 32 bytes
     * @param nonce     the state's nonce (unsigned 64-bit)
     * @param parties   addresses and balances, in contract order
     */
    public EvmState encodeState(byte[] channelId, long nonce, List<Party> parties) {
        Ffi.requireLength("channel id", channelId, 32);
        int n = parties.size();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment addr = a.allocate(Math.max(1, 20L * n)), bal = a.allocate(JAVA_LONG, Math.max(1, n));
            for (int i = 0; i < n; i++) {
                MemorySegment.copy(parties.get(i).address(), 0, addr, JAVA_BYTE, 20L * i, 20);
                bal.setAtIndex(JAVA_LONG, i, parties.get(i).balanceMinor());
            }
            return twoCall(a, (buf, capBytes, len, dg, l) -> Ffi.call(Native.evm_encode_state, l.h(0),
                    Ffi.bytes(a, channelId), nonce, addr, bal, n, buf, capBytes, len, dg));
        }
    }

    /**
     * Encodes each account's EQUITY (888) balance, read from the ledger.
     *
     * @param currency  every account's currency
     * @param accounts  the ledger accounts, in contract order
     * @param addresses one 20-byte address per account
     */
    public EvmState ledgerState(byte[] channelId, long nonce, String currency, List<Account> accounts, List<byte[]> addresses) {
        Ffi.requireLength("channel id", channelId, 32);
        int n = accounts.size();
        if (addresses.size() != n) throw new IllegalArgumentException("one address per account");
        try (Arena a = Arena.ofConfined()) {
            MemorySegment addr = a.allocate(Math.max(1, 20L * n)), ids = a.allocate(JAVA_INT, Math.max(1, n));
            for (int i = 0; i < n; i++) {
                Ffi.requireLength("address", addresses.get(i), 20);
                MemorySegment.copy(addresses.get(i), 0, addr, JAVA_BYTE, 20L * i, 20);
                ids.setAtIndex(JAVA_INT, i, accounts.get(i).id());
            }
            MemorySegment ccy = Ffi.cstr(a, currency);
            return twoCall(a, (buf, capBytes, len, dg, l) -> Ffi.call(Native.evm_ledger_state, l.h(0),
                    Ffi.bytes(a, channelId), nonce, ccy, ids, addr, n, buf, capBytes, len, dg));
        }
    }

    @FunctionalInterface
    private interface EncodeCall {
        int call(MemorySegment buf, long cap, MemorySegment outLen, MemorySegment digest, Lease l);
    }

    private EvmState twoCall(Arena a, EncodeCall f) {
        try (Lease l = Lease.of(Caps.res(cap))) {
            MemorySegment len = a.allocate(JAVA_LONG), dg = a.allocate(32);
            int st = f.call(MemorySegment.NULL, 0, len, dg, l);
            if (st != Ffi.E_BUFFER) Ffi.check(st, ctx);
            long n = len.get(JAVA_LONG, 0);
            MemorySegment buf = a.allocate(Math.max(1, n));
            Ffi.check(f.call(buf, n, len, dg, l), ctx);
            return new EvmState(buf.asSlice(0, len.get(JAVA_LONG, 0)).toArray(JAVA_BYTE), dg.toArray(JAVA_BYTE));
        }
    }
}
