// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

/**
 * Supplies the bytes a CIDREF op names while an op stream expands
 * ({@link OpStream#expand(byte[], long, CidResolver)}). It runs on the
 * calling thread, inside the native expansion.
 *
 * <p>Return exactly {@code length} bytes, or null when the content is not
 * held. Any other length, or a thrown exception, fails the expansion: an
 * exception never unwinds through C; it is caught at the boundary and
 * rethrown from {@code expand}. A resolver must not expand another stream.
 */
@FunctionalInterface
public interface CidResolver {
    /**
     * The bytes for content id {@code cid}.
     *
     * @param cid    the content id as it appears in the stream (1..128 bytes)
     * @param length the number of bytes the op needs
     * @return exactly {@code length} bytes, or null if unresolved
     */
    byte[] resolve(byte[] cid, long length);
}
