// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

/**
 * One tool of a manifest.
 *
 * @param name       1..31 characters of [a-z0-9._-]
 * @param priceVfv   the price in VFV minor units (2 digits; unsigned)
 * @param ratePerMin calls per minute, at least 1
 * @param burst      burst size, at least 1
 * @param flags      {@link ToolFlag} bits
 * @param maxInput   the largest input, at most {@link Web4#MAX_PAYLOAD}
 */
public record Tool(String name, long priceVfv, int ratePerMin, int burst, int flags, int maxInput) {}
