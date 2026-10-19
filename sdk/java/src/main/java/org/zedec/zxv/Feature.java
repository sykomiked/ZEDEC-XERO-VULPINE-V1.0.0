// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * Feature groups a libzxv build may contain ({@code ZXV_FEAT_*}). Modules
 * still being built are present as stubs that raise
 * {@link NotImplementedException}; {@link Zxv#features()} says which are live.
 */
public enum Feature {
    /** Triple-ledger accounts and postings. */
    LEDGER(0x00000001),
    /** ISO 20022 pacs.008. */
    MX_PACS008(0x00000002),
    /** ISO 20022 camt.053. */
    MX_CAMT053(0x00000004),
    /** Full ISO 4217 table lookup. */
    ISO4217(0x00000008),
    /** ISO 20022 pacs.002. */
    MX_PACS002(0x00000010),
    /** ISO 20022 pacs.004. */
    MX_PACS004(0x00000020),
    /** ISO 20022 pacs.009. */
    MX_PACS009(0x00000040),
    /** ISO 20022 camt.056. */
    MX_CAMT056(0x00000080),
    /** ISO 20022 camt.029. */
    MX_CAMT029(0x00000100),
    /** Multilateral netting cycles. */
    NETTING(0x00000200),
    /** Card issue, reissue, authorize. */
    CARDS(0x00000400),
    /** PAN check digits. */
    CARD_CHECK(0x00000800),
    /** VSS conformance checks. */
    VSS(0x00001000),
    /** Exact rationals. */
    RATIONAL(0x00002000),
    /** Four-valued logic and attestations. */
    LPRES(0x00004000),
    /** Capability handles. */
    CAPS(0x00008000),
    /** Rational resource budgets. */
    RMAG(0x00010000),
    /** Phase coordinator ticks. */
    PHASE(0x00020000),
    /** UBH-168 harmonic wire frames. */
    WIRE(0x00040000),
    /** Fibonacci and emotional swarm token economy. */
    SWARM(0x00080000),
    /** SHA3-256, SHAKE256, Keccak-256. */
    HASH(0x00100000),
    /** FIPS 203 ML-KEM-1024. */
    MLKEM1024(0x00200000),
    /** FIPS 204 ML-DSA-87. */
    MLDSA87(0x00400000),
    /** RFC 8032 Ed25519. */
    ED25519(0x00800000),
    /** Integer GGUF inference engine. */
    TENSOR(0x01000000),
    /** LoRA adapters (not built yet). */
    LORA(0x02000000),
    /** EVM state-channel encoding. */
    EVM(0x04000000),
    /** The 64-state hexagram tag and quarantine log (ABI 1.3). */
    HEX(0x08000000),
    /** The hardware emulation matrix, ZHP-1 profiles and hardware updates (ABI 1.4). */
    HW(0x10000000);

    private final int bit;

    Feature(int bit) {
        this.bit = bit;
    }

    /** The {@code ZXV_FEAT_*} bit. */
    public int bit() {
        return bit;
    }
}
