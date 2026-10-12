// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

import org.zedec.zxv.Hexagram;

/**
 * One record of a hardware audit log (a region's or a matrix's), a SHA-256
 * hash chain with domain {@code zxv/hw-audit/v1}.
 *
 * @param event   what happened
 * @param reason  the failure status (0 for none)
 * @param id      the region id, or the function id for {@link HwEvent#CONTRACT}
 * @param version the image or contract version
 * @param tag     the hexagram tag; forbidden for rejections and quarantines
 * @param subject the SHA-256 of the package header
 * @param hash    this record's chain hash (the last one is the head)
 */
public record HwAuditRecord(HwEvent event, int reason, int id, int version, Hexagram tag, byte[] subject, byte[] hash) {
    /**
     * The head of an audit log.
     *
     * @param head  the last record's hash
     * @param count the number of records
     */
    public record Head(byte[] head, int count) {}
}
