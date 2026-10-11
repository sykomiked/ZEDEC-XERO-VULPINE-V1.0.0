// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

import java.nio.charset.StandardCharsets;

/**
 * One generated token.
 *
 * @param id    the token id
 * @param piece its UTF-8 bytes; a multi-byte character may be split across tokens
 */
public record Token(int id, byte[] piece) {
    /** The piece decoded leniently (a split character shows as U+FFFD; use the result's text for exact output). */
    public String text() {
        return new String(piece, StandardCharsets.UTF_8);
    }
}
