// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

/** Tool flags ({@code ZXV_W4_TOOL_*}), combined with bitwise OR. */
public final class ToolFlag {
    private ToolFlag() {}

    /** The tool moves money. */
    public static final int MONEY = 0x01;
    /** The tool reveals personal data. */
    public static final int PII = 0x02;
    /** The tool answers with STREAM chunks. */
    public static final int STREAM = 0x04;
}
