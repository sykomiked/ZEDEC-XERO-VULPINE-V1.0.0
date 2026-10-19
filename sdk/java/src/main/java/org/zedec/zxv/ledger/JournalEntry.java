// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

/**
 * One journal line of an account.
 *
 * @param entryId       the platform entry id (shared by both sides of a posting)
 * @param amountMinor   signed minor units: positive received, negative sent
 * @param counterparty  the other account's id
 * @param reference     the posting's unique reference
 */
public record JournalEntry(long entryId, long amountMinor, int counterparty, String reference) {}
