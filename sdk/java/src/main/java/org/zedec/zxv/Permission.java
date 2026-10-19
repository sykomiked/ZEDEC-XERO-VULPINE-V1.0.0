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
    PARADOX,
    /**
     * Use the Vino Embed play lane and the daily spend meter
     * ({@link org.zedec.zxv.vino.PlaySandbox}, {@link org.zedec.zxv.vino.SpendMeter}; ABI 1.6,
     * {@code ZXV_CAP_VINO}). Payment requests and verifying receipts are pure and need no
     * permission; signing a receipt is the wallet's job and needs {@link #CRYPTO_SIGN}.
     */
    VINO;

    /** {@code ZXV_CAP_ALL_1_5}: the mask of every permission defined as of ABI 1.5 (bits 0 to 14). */
    public static final long ALL_1_5 = (1L << 15) - 1;
    /** {@code ZXV_CAP_ALL_1_6}: the mask of every permission defined as of ABI 1.6 (bits 0 to 15). */
    public static final long ALL_1_6 = (1L << 16) - 1;

    /**
     * The {@code ZXV_CAP_*} bit.
     *
     * @return the single bit of this permission ({@code 1L << ordinal()})
     */
    public long bit() {
        return 1L << ordinal();
    }

    /**
     * Every permission this SDK knows: as of ABI 1.6 that is {@link #ALL_1_6}
     * ({@code ZXV_CAP_ALL_1_6}). The set grows when an ABI adds a permission.
     *
     * @return a new mutable set of every permission
     */
    public static Set<Permission> all() {
        return EnumSet.allOf(Permission.class);
    }

    /**
     * Every permission defined as of ABI 1.5 ({@code ZXV_CAP_ALL_1_5}): {@link #all()} without
     * {@link #VINO}. For code that must keep exactly the 1.5 set.
     *
     * @return a new mutable set of the fifteen ABI 1.5 permissions
     */
    public static Set<Permission> all1_5() {
        return fromMask(ALL_1_5);
    }

    /**
     * The bit mask of a set.
     *
     * @param perms the permissions
     * @return the OR of their bits
     */
    public static long mask(Collection<Permission> perms) {
        long m = 0;
        for (Permission p : perms) m |= p.bit();
        return m;
    }

    /**
     * The set for a bit mask (unknown bits ignored).
     *
     * @param mask a {@code ZXV_CAP_*} mask
     * @return a new mutable set of the permissions whose bits are in {@code mask}
     */
    public static Set<Permission> fromMask(long mask) {
        EnumSet<Permission> s = EnumSet.noneOf(Permission.class);
        for (Permission p : values()) if ((mask & p.bit()) != 0) s.add(p);
        return s;
    }
}
