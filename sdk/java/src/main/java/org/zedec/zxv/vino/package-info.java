// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
/**
 * Vino Embed (ABI 1.6): what an app or game needs to take part in Vino without ever holding
 * anyone's value or keys.
 *
 * <h2>Two lanes that never meet</h2>
 * <ul>
 *   <li><b>PLAY</b> ({@link org.zedec.zxv.vino.PlaySandbox}): a sandbox inside the app with its own
 *       play units, issuers, holders and Fibonacci-face vouchers, in process memory only. Play units
 *       are not money. No call takes a play sandbox together with anything live, so a play unit can
 *       never be converted into, redeemed for, or moved into live value. Games use it freely.</li>
 *   <li><b>LIVE</b>: real Vino, held in the user's own ZXV wallet, never in the app. The app builds
 *       a {@link org.zedec.zxv.vino.PaymentRequest}; the user approves it in their wallet, which
 *       signs a {@link org.zedec.zxv.vino.Receipt}; the app checks the receipt offline. The app never
 *       sees a balance, a key or a custody account.</li>
 * </ul>
 *
 * <h2>Vouchers and coverage (play lane)</h2>
 * A voucher is one coin with a Fibonacci ladder face, held by one holder on exactly one active
 * {@link org.zedec.zxv.vino.Rail}: BEARER (a note in the holder's hand) or GHOST (its twin on the
 * ledger). A transfer must name the active rail ({@link org.zedec.zxv.vino.RailException}), so one
 * voucher can never be spent on both. An issuer posts play backing and may only issue while
 * {@code 10 * backing >= 18 * outstanding} ({@link org.zedec.zxv.vino.CoverageException}).
 * Redeeming a voucher returns it to its issuer and retires it: outstanding falls, and nothing is
 * paid out to anyone. There is no interest, yield or return of any kind.
 *
 * <h2>Requests and receipts (both lanes)</h2>
 * A request is a fixed 160-byte canonical record whose id is SHA-256 of those bytes. A receipt is
 * the wallet's ML-DSA-87 signature over the request id, the wallet key's hash and the approval
 * time. A receipt for an expired request, or approved outside the request's window, is refused
 * ({@link org.zedec.zxv.vino.ExpiredException}). A LIVE request that marks a paid chance outcome
 * (a loot box, a paid draw) is refused ({@link org.zedec.zxv.vino.ChanceException}). The raffle is a
 * separate module, not a payment request.
 *
 * <h2>Daily cap</h2>
 * A deployment may set a per-device daily cap; {@link org.zedec.zxv.vino.SpendMeter} counts what
 * one device approved per day and refuses an amount that would pass it
 * ({@link org.zedec.zxv.vino.DailyCapException}). It is a counter, not a legal check, and makes no
 * exemption claim.
 *
 * <h2>Capabilities</h2>
 * The play sandbox and the meter need {@link org.zedec.zxv.Permission#VINO}. Building, encoding and
 * decoding requests and verifying receipts are pure and need none. Signing a receipt needs
 * {@link org.zedec.zxv.Permission#CRYPTO_SIGN}: it is the wallet's call, not the app's.
 *
 * <h2>Not here, by design</h2>
 * Minting live Vino, setting pegs, any fiat on-ramp or off-ramp, V-Bill auctions or bids, V-Bonds
 * and derivatives, and every operator or steward action. They are never app-facing.
 *
 * <h2>Honest limits</h2>
 * Play units are not money and are lost when the sandbox closes. The LIVE flow here is the app's
 * half only: approving a request happens in the user's wallet, which posts the payment on its own
 * ledger. ML-DSA-87 here is the reference implementation, not FIPS 140-3 validated. Whether a given
 * deployment of Vino is store credit, a prepaid access product or something else is a legal
 * question pending counsel; nothing in this SDK decides it.
 */
package org.zedec.zxv.vino;
