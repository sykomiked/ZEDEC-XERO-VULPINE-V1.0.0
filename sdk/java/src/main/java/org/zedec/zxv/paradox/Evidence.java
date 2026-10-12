// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

/**
 * Evidence counts before and after, for the trend of a classification
 * (uint32 bits each).
 *
 * @param pos0  evidence for, before
 * @param neg0  evidence against, before
 * @param pos1  evidence for, after
 * @param neg1  evidence against, after
 * @param stale whether the newest evidence is past the caller's freshness limit
 */
public record Evidence(int pos0, int neg0, int pos1, int neg1, boolean stale) {}
