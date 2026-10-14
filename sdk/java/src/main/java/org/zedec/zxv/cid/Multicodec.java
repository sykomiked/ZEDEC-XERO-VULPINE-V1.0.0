// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.cid;

/**
 * Multicodec and multihash codes a {@link Cid} may carry ({@code ZXV_CID_CODEC_*},
 * {@code ZXV_CID_HASH_*}, {@code ZXV_AIPI_MC_*}). The {@code 0x3A10xx} AIPI codes are
 * multicodec PRIVATE-USE values: they are not registered with multiformats.
 */
public final class Multicodec {
    private Multicodec() {}

    /** Raw bytes ({@code 0x55}): blobs, modules, contracts. */
    public static final long RAW = 0x55;
    /** dag-pb ({@code 0x70}): UnixFS directories and file roots, and every CIDv0. */
    public static final long DAG_PB = 0x70;
    /** An AIPI function of intent FN ({@code zxv-fn}, private use). */
    public static final long AIPI_FN = 0x3A1000;
    /** An AIPI function of intent TENSOR_INFER ({@code zxv-tensor-infer}, private use). */
    public static final long AIPI_TENSOR_INFER = 0x3A1001;
    /** An AIPI function of intent SETTLE_LEDGER ({@code zxv-settle-ledger}, private use). */
    public static final long AIPI_SETTLE_LEDGER = 0x3A1002;
    /** An AIPI function of intent SWARM_ROUTE ({@code zxv-swarm-route}, private use). */
    public static final long AIPI_SWARM_ROUTE = 0x3A1003;
    /** An AIPI call record ({@code zxv-aipi-call}, private use). */
    public static final long AIPI_CALL = 0x3A10C0;

    /** The identity multihash ({@code 0x00}): the "digest" is the content itself. */
    public static final int HASH_IDENTITY = 0x00;
    /** The sha2-256 multihash ({@code 0x12}). */
    public static final int HASH_SHA2_256 = 0x12;
}
