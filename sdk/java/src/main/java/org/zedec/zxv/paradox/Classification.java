// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.Hexagram;

/**
 * The class pair an operator takes, from {@link ParadoxBook#classify}.
 *
 * @param kind          the kind
 * @param tag           the tag with the kind's polarity (base and phase kept; a forbidden tag stays forbidden)
 * @param registryIndex the registry entry that decided it, or -1
 */
public record Classification(ParadoxKind kind, Hexagram tag, int registryIndex) {
    /** What to do with this pair ({@link Paradox#action}). */
    public ParadoxAction action() {
        return Paradox.action(kind, tag);
    }
}
