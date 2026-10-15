<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.6: Vino Embed (zxv_vino_*), the constants of the group and its
 * overview. What an app or game needs to take part in Vino without ever
 * holding anyone's value or keys. There are two lanes, and they never meet:
 *
 * - PLAY: a sandbox inside the app ({@see VinoPlay}). It has its own play
 *   units (never live Vino), its own issuers, holders and vouchers, and
 *   lives in process memory only. Play units are not money: no call takes a
 *   play sandbox and anything live, so a play unit can never be converted
 *   to, redeemed for, or moved into live value. They are lost on close.
 * - LIVE: real Vino, held in the user's own ZXV wallet, never in the app.
 *   The app builds a {@see VinoPaymentRequest}; the user approves it in
 *   their wallet, which signs a {@see VinoReceipt} with its ML-DSA-87 key;
 *   the app checks the receipt offline (VinoReceipt::verify()). The app
 *   never sees a balance, a key or a custody account.
 *
 * VOUCHERS (play lane). A voucher is one coin whose face is a Fibonacci
 * ladder face ({@see Coin::isFace()}), held by one holder on exactly ONE
 * active rail at a time: {@see VinoRail::Bearer} (a note in the holder's
 * hand) or {@see VinoRail::Ghost} (its twin on the play ledger). Spending
 * must name the active rail, so one voucher can never be spent on both
 * (WrongRailException). Switching rails moves nothing.
 *
 * COVERAGE. An issuer posts play backing and may only issue while its
 * backing stays at least 1.8 times everything it has outstanding, in exact
 * integers: 10 * backing >= 18 * outstanding. An issue or a withdrawal that
 * would break this is refused (CoverageException). Redeeming a voucher
 * returns it to its issuer and retires it: the issuer's outstanding falls
 * and nothing is paid out to anyone. There is no call that turns a voucher
 * into backing, play units or anything else, and no interest, yield or
 * return of any kind.
 *
 * REQUESTS AND RECEIPTS. A payment request is a fixed 160-byte canonical
 * record; its id is the SHA-256 of those bytes. A LIVE request marked as
 * having a paid chance outcome (loot boxes, paid draws) is refused
 * (ChanceRefusedException); the raffle is a separate module, not a payment
 * request. A receipt is the wallet's ML-DSA-87 signature over 88 bytes
 * naming the request id, the wallet key and the approval time. A receipt
 * for an expired request, or one approved outside its window, is
 * ExpiredException.
 *
 * DAILY CAP. A deployment may set a per-device daily cap.
 * {@see VinoSpendMeter} counts what one device approved per day and refuses
 * an amount that would pass the cap. It is a counter, not a legal check,
 * and makes no exemption claim.
 *
 * CAPABILITIES. Every play-lane call and the meter need Capability::VINO
 * from a capability of the handle's context (CapabilityDeniedException
 * without it; ZxvFatalError once the context is poisoned). Encoding and
 * decoding a request, building a receipt message and verifying a receipt
 * are pure and need no capability. Signing a receipt needs CRYPTO_SIGN: it
 * is the wallet's call, not the app's.
 *
 * NOT HERE, by design: minting live Vino, setting pegs, any conversion
 * between Vino and other currencies, V-Bill auctions or bids, V-Bonds and
 * derivatives, and every operator or steward action.
 *
 * HONEST LIMITS. The LIVE flow here is the app's half only: approving a
 * request happens in the user's wallet (zxvd), which posts the payment on
 * its own ledger. ML-DSA-87 here is the reference implementation, not FIPS
 * 140-3 validated. Whether a given deployment of Vino is store credit, a
 * prepaid access product or something else is a legal question pending
 * counsel; nothing in this SDK decides it.
 */
final class Vino
{
    /** Most accounts (issuers plus holders) one play sandbox may open. */
    public const MAX_ACCOUNTS = 256;
    /** Most active (unredeemed) vouchers in one play sandbox. */
    public const MAX_VOUCHERS = 4096;
    /** Largest single amount, face, backing step or daily cap (2^59). */
    public const AMOUNT_MAX = 1 << 59;
    /** Size of an encoded payment request. */
    public const REQUEST_BYTES = 160;
    /** Size of a request id (SHA-256 of the encoded request). */
    public const REQUEST_ID_BYTES = 32;
    /** Size of a merchant id. */
    public const MERCHANT_BYTES = 32;
    /** Size of a request nonce. */
    public const NONCE_BYTES = 16;
    /** Longest memo, in bytes (printable ASCII 0x20..0x7e). */
    public const MEMO_MAX = 64;
    /** Size of the message a wallet signs to approve a request. */
    public const RECEIPT_MSG_BYTES = 88;
    /** Request flag: the purchase has a paid chance outcome (refused on LIVE). */
    public const FLAG_CHANCE = 0x1;
    /** Every request flag this ABI knows; any other bit is ZXV_E_FORMAT. */
    public const FLAGS_KNOWN = 0x1;
    /** ML-DSA-87 wallet public key size. */
    public const PUBLIC_KEY_BYTES = 2592;
    /** ML-DSA-87 wallet secret key size. */
    public const SECRET_KEY_BYTES = 4896;
    /** ML-DSA-87 receipt signature size. */
    public const SIGNATURE_BYTES = 4627;

    private function __construct()
    {
    }

    /**
     * @internal A rail as the C int. An int is passed through unchanged so
     * the library can refuse it (ZXV_E_ARG).
     */
    public static function rail(VinoRail|int $rail): int
    {
        return $rail instanceof VinoRail ? $rail->value : Native::int32($rail, "rail");
    }

    /**
     * @internal A lane as the C int. An int is passed through unchanged so
     * the library can refuse it (ZXV_E_LANE).
     */
    public static function lane(VinoLane|int $lane): int
    {
        return $lane instanceof VinoLane ? $lane->value : Native::int32($lane, "lane");
    }
}
