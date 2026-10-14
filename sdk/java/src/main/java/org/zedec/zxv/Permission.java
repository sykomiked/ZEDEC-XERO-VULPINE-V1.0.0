// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import java.util.Collection;
import java.util.EnumSet;
import java.util.Set;

/** One permission bit a {@link Capability} may hold ({@code ZXV_CAP_*}). */
public enum Permission {
    /** Read balances and journals. */
    LEDGER_READ,
    /** Post transfers (capital transfer). */
    LEDGER_POST,
    /** Sign with ML-DSA-87 or Ed25519. */
    CRYPTO_SIGN,
    /** ML-KEM-1024 key generation, encapsulation, decapsulation. */
    CRYPTO_KEM,
    /** Open models and generate tokens. */
    AI_INFERENCE,
    /** Create and use resource budgets. */
    BUDGET,
    /** Admit, defer and advance on the phase clock. */
    PHASE,
    /** Record attestations and evidence. */
    ATTEST,
    /** Drive the swarm token economy. */
    SWARM,
    /** EVM state-channel encoding. */
    EVM,
    /** Build and call the hardware emulation matrix (ABI 1.4). */
    HW_MATRIX,
    /** Load hardware images into regions and apply contract packages (ABI 1.4). */
    HW_RECONF,
    /**
     * Store blocks and publish functions on an AIPI node
     * ({@link org.zedec.zxv.aipi.AipiNode}; ABI 1.5, {@code ZXV_CAP_AIPI}).
     */
    AIPI,
    /**
     * Create tokens and change a token wallet's octane
     * ({@link org.zedec.zxv.coin.TokenWallet}; ABI 1.5, {@code ZXV_CAP_MINT}). Coin minting is
     * not in the ABI yet; this bit gates only the token wallet.
     */
    MINT,
    /**
     * Run paradox operators, write paradox logs and the signature registry, and create
     * paradox handles ({@link org.zedec.zxv.paradox.ParadoxBook}; ABI 1.5, {@code ZXV_CAP_PARADOX}).
     */
    PARADOX;

    /** The {@code ZXV_CAP_*} bit. */
    public long bit() {
        return 1L << ordinal();
    }

    /** Every permission (as of ABI 1.5: {@code ZXV_CAP_ALL_1_5}). */
    public static Set<Permission> all() {
        return EnumSet.allOf(Permission.class);
    }

    /** The bit mask of a set. */
    public static long mask(Collection<Permission> perms) {
        long m = 0;
        for (Permission p : perms) m |= p.bit();
        return m;
    }

    /** The set for a bit mask (unknown bits ignored). */
    public static Set<Permission> fromMask(long mask) {
        EnumSet<Permission> s = EnumSet.noneOf(Permission.class);
        for (Permission p : values()) if ((mask & p.bit()) != 0) s.add(p);
        return s;
    }
}
