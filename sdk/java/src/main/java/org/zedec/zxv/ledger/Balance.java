// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

/**
 * Rail balances of an account, in exact minor units.
 *
 * @param debit  DEBIT rail 555: the sum received (asset, backing)
 * @param credit CREDIT rail 777: the sum sent (claim, liability)
 * @param equity EQUITY rail 888: {@code debit - credit}, the spendable position
 */
public record Balance(long debit, long credit, long equity) {}
