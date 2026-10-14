// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * The ABI 1.5 feature groups ({@code ZXV_FEAT2_*}), reported through a second
 * mask because the 32 bits of {@link Feature} are nearly used up. A group that
 * is not built into the loaded library refuses every call with
 * {@link NotImplementedException}; {@link Zxv#features2()} says which are live.
 */
public enum Feature2 {
    /** Treaties: COBOL copybook records and Sutra text ({@link org.zedec.zxv.treaty}). */
    TREATY(0x00000001),
    /** Content-addressed functions and IPFS CIDs ({@link org.zedec.zxv.cid}, {@link org.zedec.zxv.aipi}). */
    AIPI(0x00000002),
    /** Fibonacci coins, token denominations and octane ({@link org.zedec.zxv.coin}). */
    COIN(0x00000004),
    /** The 0.08889% assurance fee and its split ({@link org.zedec.zxv.fee}). */
    FEE(0x00000008),
    /** The V-Bill hub: pegs, quotes, swaps, limit rules ({@link org.zedec.zxv.vhub}). */
    VHUB(0x00000010),
    /** Erasure-coded freight packets and op streams ({@link org.zedec.zxv.freight}). */
    FREIGHT(0x00000020),
    /** Web 4 agents: manifests, directory, consent, envelopes ({@link org.zedec.zxv.web4}). */
    WEB4(0x00000040),
    /** The paradox engine ({@link org.zedec.zxv.paradox}). */
    PARADOX(0x00000080);

    private final int bit;

    Feature2(int bit) {
        this.bit = bit;
    }

    /** The {@code ZXV_FEAT2_*} bit. */
    public int bit() {
        return bit;
    }
}
