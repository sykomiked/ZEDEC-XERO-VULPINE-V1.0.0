// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

/**
 * A ledger account: its id in the context, name, currency and kind.
 *
 * @param id       the platform account id (0, 1, 2, ... in opening order)
 * @param name     the name given when it was opened
 * @param currency the currency code it holds
 * @param kind     holder or issuer
 */
public record Account(int id, String name, String currency, AccountKind kind) {}
