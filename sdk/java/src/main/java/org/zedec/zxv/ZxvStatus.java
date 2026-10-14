// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * Every libzxv status code by name ({@code ZXV_OK} and {@code ZXV_E_*}), for
 * branching on {@link ZxvException#status()}. The numbers are stable: libzxv
 * never renumbers a status, and new ones are only added. An exception's
 * {@link ZxvException#statusClass() class} (USAGE, REFUSED, FATAL, SYSTEM)
 * decides its Java type; the codes added in ABI 1.5 also have their own
 * subclasses in the group packages (for example
 * {@link org.zedec.zxv.treaty.TreatyException} for {@link #E_TREATY}).
 */
public final class ZxvStatus {
    private ZxvStatus() {}

    /** {@code ZXV_OK}: Success. */
    public static final int OK = 0;
    /** {@code ZXV_E_ARG}: NULL handle/pointer or invalid argument value. */
    public static final int E_ARG = -1;
    /** {@code ZXV_E_BUFFER}: Output buffer too small; *out_len = needed. */
    public static final int E_BUFFER = -2;
    /** {@code ZXV_E_NOTIMPL}: Feature not built into this library yet. */
    public static final int E_NOTIMPL = -3;
    /** {@code ZXV_E_NOT_FOUND}: Unknown account, card, currency or index. */
    public static final int E_NOT_FOUND = -4;
    /** {@code ZXV_E_CAPACITY}: A fixed-size table is full. */
    public static final int E_CAPACITY = -5;
    /** {@code ZXV_E_FUNDS}: Holder account would go below zero. */
    public static final int E_FUNDS = -6;
    /** {@code ZXV_E_CURRENCY}: Unknown, disabled, non-ISO or mismatched ccy. */
    public static final int E_CURRENCY = -7;
    /** {@code ZXV_E_RANGE}: Amount &lt;= 0 or beyond the exact range. */
    public static final int E_RANGE = -8;
    /** {@code ZXV_E_STATE}: Operation not valid in the current state. */
    public static final int E_STATE = -9;
    /** {@code ZXV_E_INVARIANT}: An internal accounting invariant failed. */
    public static final int E_INVARIANT = -10;
    /** {@code ZXV_E_UTF8}: Input string is not valid UTF-8. */
    public static final int E_UTF8 = -11;
    /** {@code ZXV_E_TOO_LONG}: Input string exceeds the field's maximum. */
    public static final int E_TOO_LONG = -12;
    /** {@code ZXV_E_DUPLICATE}: Reference/id already used. */
    public static final int E_DUPLICATE = -13;
    /** {@code ZXV_E_NOMEM}: Allocation failed. */
    public static final int E_NOMEM = -14;
    /** {@code ZXV_E_DECLINED}: Card authorization declined (not a fault). */
    public static final int E_DECLINED = -15;
    /** {@code ZXV_E_FIELD}: Field not valid for this message kind / unset. */
    public static final int E_FIELD = -16;
    /** {@code ZXV_E_CAP}: The capability lacks the permission, is revoked, or its attestation is not TRUE. */
    public static final int E_CAP = -17;
    /** {@code ZXV_E_POISONED}: The context halted after a broken invariant; terminal. */
    public static final int E_POISONED = -18;
    /** {@code ZXV_E_BUDGET}: An rmag or swarm budget is exhausted (denied). */
    public static final int E_BUDGET = -19;
    /** {@code ZXV_E_RMAG_INVALID}: A malformed or overflowing rational amount; never retry the same value. */
    public static final int E_RMAG_INVALID = -20;
    /** {@code ZXV_E_VERIFY}: Signature / tag did not verify (not a fault). */
    public static final int E_VERIFY = -21;
    /** {@code ZXV_E_FORMAT}: Malformed input (number text, GGUF, frame). */
    public static final int E_FORMAT = -22;
    /** {@code ZXV_E_IO}: A file could not be opened or read. */
    public static final int E_IO = -23;
    /** {@code ZXV_E_FLOAT}: NaN or infinity refused at the float boundary. */
    public static final int E_FLOAT = -24;
    /** {@code ZXV_E_INTERNAL}: Unexpected kernel status; see zxv_last_error. */
    public static final int E_INTERNAL = -99;
    /** {@code ZXV_E_NOPROVIDER}: No proven implementation may serve the call (ABI 1.4). */
    public static final int E_NOPROVIDER = -25;
    /** {@code ZXV_E_DISPUTE}: A shadow check found two implementations disagree (ABI 1.4). */
    public static final int E_DISPUTE = -26;
    /** {@code ZXV_E_QUARANTINED}: Image failed in hardware: rolled back, logged (ABI 1.4). */
    public static final int E_QUARANTINED = -27;
    /** {@code ZXV_E_ROLLBACK}: Image or contract not newer / below the floor (ABI 1.4). */
    public static final int E_ROLLBACK = -28;
    /** {@code ZXV_E_BLOCKED}: Image already quarantined; never reloaded (ABI 1.4). */
    public static final int E_BLOCKED = -29;
    /** {@code ZXV_E_TARGET}: Image is for other hardware (ABI 1.4). */
    public static final int E_TARGET = -30;
    /** {@code ZXV_E_POLICY}: Refused by the function's placement policy (ABI 1.4). */
    public static final int E_POLICY = -31;
    /** {@code ZXV_E_DENOM}: Not a Fibonacci ladder face (coins, denominations) (ABI 1.5). */
    public static final int E_DENOM = -32;
    /** {@code ZXV_E_TREATY}: Treaty fails validation; detail in zxv_last_error (ABI 1.5). */
    public static final int E_TREATY = -33;
    /** {@code ZXV_E_CID}: Content does not match its CID, or bad CID (ABI 1.5). */
    public static final int E_CID = -34;
    /** {@code ZXV_E_CONSENT}: No valid consent for this agent action (ABI 1.5). */
    public static final int E_CONSENT = -35;
    /** {@code ZXV_E_ERASURE}: Too few freight rows to rebuild the packet (ABI 1.5). */
    public static final int E_ERASURE = -36;
    /** {@code ZXV_E_PEG}: No valid signed peg for this currency pair (ABI 1.5). */
    public static final int E_PEG = -37;
    /** {@code ZXV_E_LIMIT}: Refused by a hub limit rule (not a fault) (ABI 1.5). */
    public static final int E_LIMIT = -38;
    /** {@code ZXV_E_PARADOX_STEP}: A paradox graph reached its step limit and was escalated to the owner (ABI 1.5). */
    public static final int E_PARADOX_STEP = -39;
    /** {@code ZXV_E_PARADOX_OWNER}: The action needs the owner's approval (a money or device policy) (ABI 1.5). */
    public static final int E_PARADOX_OWNER = -40;
}
