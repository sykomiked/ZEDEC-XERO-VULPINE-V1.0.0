// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_BUFFER
import org.zedec.zxv.mobile.internal.Jni16
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32

/*
 * Vino Embed (libzxv ABI 1.6, group "vino").
 *
 * What an app or game needs to take part in Vino without ever holding anyone's
 * value or keys. Two lanes, which never meet:
 *
 *  - PLAY: a sandbox inside the app ([PlaySandbox]) with its own play units,
 *    issuers, holders and vouchers, in process memory only. Play units are not
 *    money: nothing in this API takes a play sandbox and something live, so a
 *    play unit can never be converted into, redeemed for, or moved into live value.
 *  - LIVE: real Vino, held in the user's own ZXV wallet, never in the app. The
 *    app builds a [PaymentRequest]; the user approves it in their wallet, which
 *    signs a receipt ([VinoReceipt.sign], wallet side); the app checks the
 *    receipt offline ([VinoReceipt.verify]). The app never sees a balance, a key
 *    or a custody account.
 *
 * There is no interest, yield or return of any kind anywhere in this group.
 */

/** Constants of the vino group (`ZXV_VINO_*`, ABI 1.6). */
object Vino {
    /** Size of an encoded [PaymentRequest] (`ZXV_VINO_REQ_BYTES`). */
    const val REQUEST_BYTES = 160

    /** Size of a request id: SHA-256 of the encoded request. */
    const val REQUEST_ID_BYTES = 32

    /** Longest memo, in bytes of printable ASCII 0x20..0x7e (`ZXV_VINO_MEMO_MAX`). */
    const val MEMO_MAX = 64

    /** Size of the message a wallet signs to approve a request (`ZXV_VINO_RCPT_MSG_BYTES`). */
    const val RECEIPT_MESSAGE_BYTES = 88

    /** Size of a wallet's ML-DSA-87 public key. */
    const val WALLET_PUBLIC_KEY_BYTES = 2592

    /** Size of a wallet's ML-DSA-87 secret key. */
    const val WALLET_SECRET_KEY_BYTES = 4896

    /** Size of a receipt signature (ML-DSA-87). */
    const val RECEIPT_SIGNATURE_BYTES = 4627

    /** Largest amount, face or backing step, 2^59 (`ZXV_VINO_AMOUNT_MAX`). */
    const val AMOUNT_MAX: Long = 1L shl 59

    /** Most accounts (issuers plus holders) in one play sandbox (`ZXV_VINO_MAX_ACCOUNTS`). */
    const val MAX_ACCOUNTS = 256

    /** Most active (unredeemed) vouchers in one play sandbox (`ZXV_VINO_MAX_VOUCHERS`). */
    const val MAX_VOUCHERS = 4096

    /** Request flag: the purchase has a paid chance outcome (`ZXV_VINO_REQ_CHANCE`); refused on LIVE. */
    const val FLAG_CHANCE = 0x1

    /** Every request flag this ABI defines (`ZXV_VINO_REQ_FLAGS_KNOWN`); any other bit is refused. */
    const val FLAGS_KNOWN = 0x1
}

/**
 * The rail a play voucher is on. A voucher is on exactly one active rail at a
 * time, so one coin can never be spent on both. Switching rails moves nothing.
 */
enum class VinoRail(
    /** The `ZXV_VINO_RAIL_*` code. */
    val code: Int,
) {
    /** The note in the holder's hand (`ZXV_VINO_RAIL_BEARER`, 0). */
    BEARER(0),

    /** Its twin on the play ledger (`ZXV_VINO_RAIL_GHOST`, 1). */
    GHOST(1);

    companion object {
        /** The rail for a code; [IllegalArgumentException] for an unknown one. */
        @JvmStatic
        fun fromCode(code: Int): VinoRail = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown vino rail $code")
    }
}

/** Which lane a payment request is for. The two lanes never meet. */
enum class VinoLane(
    /** The `ZXV_VINO_LANE_*` code. */
    val code: Int,
) {
    /** Play units inside an app's sandbox; not money (`ZXV_VINO_LANE_PLAY`, 1). */
    PLAY(1),

    /** Real Vino from the user's own wallet, in VFV minor units (`ZXV_VINO_LANE_LIVE`, 2). */
    LIVE(2);

    companion object {
        /** The lane for a code; [IllegalArgumentException] for an unknown one. */
        @JvmStatic
        fun fromCode(code: Int): VinoLane = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown vino lane $code")
    }
}

/** A play voucher's state. */
enum class VoucherState(
    /** The `ZXV_VINO_V_*` code. */
    val code: Int,
) {
    /** Held and spendable (`ZXV_VINO_V_ACTIVE`, 1). */
    ACTIVE(1),

    /** Returned to its issuer and retired; nothing was paid out (`ZXV_VINO_V_REDEEMED`, 2). */
    REDEEMED(2);

    companion object {
        /** The state for a code; [IllegalArgumentException] for an unknown one. */
        @JvmStatic
        fun fromCode(code: Int): VoucherState = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown voucher state $code")
    }
}

/**
 * A play-lane sandbox (`zxv_vino_play`): issuers, holders and Fibonacci-face
 * vouchers, all in play units, in process memory only.
 *
 * **Play units are not money.** They come from nowhere ([back] is a faucet),
 * are worth nothing, are lost when the sandbox is closed, and can never become
 * live value: no call takes a sandbox and anything live. Games use it freely.
 *
 * How it works:
 *  - An issuer posts play backing ([back]) and may issue vouchers only while
 *    `10 * backing >= 18 * outstanding` (1.8x coverage, exact integers). An
 *    issue or a withdrawal ([unback]) that would break this is refused with
 *    [CoverageException].
 *  - A voucher has one face from the Fibonacci ladder (1, 2, 3, 5, 8, 13, ...;
 *    other faces are [NotADenominationException]), one issuer, one holder and
 *    one active [VinoRail]. A [transfer] must name the active rail ([RailException]).
 *  - [redeem] returns a voucher to its issuer and retires it: the issuer's
 *    outstanding falls by its face, and nothing is paid out to anyone. No call
 *    turns a voucher into backing, play units, or anything else, and there is
 *    no interest, yield or return of any kind.
 *
 * Every call needs [Permission.VINO] on [capability] ([CapabilityDeniedException]
 * otherwise); a halted context gives [ZxvFatalError]. Account ids are uint32
 * values in a `Long`, counting up from 0; voucher ids, faces and amounts are
 * uint64 values in a `Long` (ids start at 1 and are never reused).
 *
 * Thread-safe: libzxv serializes the calls. Close it (`use { }`) before its
 * context; closing the context closes it too. Use after close throws
 * [IllegalStateException].
 */
class PlaySandbox private constructor(
    /** The capability every call passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    private val res = NativeResource("play sandbox", handle, Jni16::vinoPlayDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /**
     * One play voucher, as [voucher] reports it.
     *
     * @property id its id (> 0)
     * @property face its Fibonacci face, in play units
     * @property issuer the issuer account it came from
     * @property holder the holder account that holds it (or last held it, once redeemed)
     * @property rail its active rail
     * @property state [VoucherState.ACTIVE] or [VoucherState.REDEEMED]
     */
    data class Voucher(
        val id: Long,
        val face: Long,
        val issuer: Long,
        val holder: Long,
        val rail: VinoRail,
        val state: VoucherState,
    )

    /**
     * One account of the sandbox, as [account] reports it.
     *
     * @property id the account id
     * @property owner the owner id given to [openIssuer] or [openHolder]
     * @property isIssuer true for an issuer, false for a holder
     * @property backing an issuer's play backing; 0 for a holder
     * @property faces an issuer's outstanding (sum of the faces of its active
     *   vouchers), or the sum of the faces a holder holds
     * @property count how many active vouchers the issuer issued, or the holder holds
     */
    data class Account(
        val id: Long,
        val owner: Long,
        val isIssuer: Boolean,
        val backing: Long,
        val faces: Long,
        val count: Long,
    )

    companion object {
        /**
         * An empty sandbox on `cap`'s context. Needs [Permission.VINO].
         *
         * @throws CapabilityDeniedException without VINO
         */
        @JvmStatic
        fun create(cap: Capability): PlaySandbox = cap.res.use { h ->
            val o = LongArray(1)
            Status.check(Jni16.vinoPlayCreate(h, o), cap.context.state)
            PlaySandbox(cap, o[0])
        }
    }

    private inline fun <T> gated(f: (Long, Long) -> T): T = capability.res.use { c -> res.use { h -> f(c, h) } }

    /**
     * Opens an account for `owner` (a uint32 id of your choosing; owners need
     * not be distinct). Returns the new account id.
     *
     * @param isIssuer true opens an issuer, false a holder
     * @throws ZxvException `ZXV_E_CAPACITY` once [Vino.MAX_ACCOUNTS] are open
     */
    fun open(owner: Long, isIssuer: Boolean): Long {
        requireU32(owner, "owner")
        return gated { c, h -> LongArray(1).also { Status.check(Jni16.vinoPlayOpen(c, h, owner, if (isIssuer) 1 else 0, it), st) }[0] }
    }

    /** Opens an issuer account for `owner`; returns its id. See [open]. */
    fun openIssuer(owner: Long): Long = open(owner, true)

    /** Opens a holder account for `owner`; returns its id. See [open]. */
    fun openHolder(owner: Long): Long = open(owner, false)

    /**
     * Posts `amount` play units of backing to `issuer`. This is the play
     * faucet: play units come from nowhere and are worth nothing.
     *
     * @throws ZxvUsageException `ZXV_E_ARG` if `issuer` is a holder
     * @throws ZxvException `ZXV_E_NOT_FOUND` (no such account), `ZXV_E_RANGE`
     *   (amount 0, above [Vino.AMOUNT_MAX], or backing would pass 2^62)
     */
    fun back(issuer: Long, amount: Long) {
        requireU32(issuer, "issuer")
        gated { c, h -> Status.check(Jni16.vinoPlayBack(c, h, issuer, amount), st) }
    }

    /**
     * Withdraws `amount` of `issuer`'s play backing, only while it stays at
     * 1.8x coverage. A refusal changes nothing.
     *
     * @throws CoverageException it would fall below 1.8x
     * @throws InsufficientFundsException more than its backing
     */
    fun unback(issuer: Long, amount: Long) {
        requireU32(issuer, "issuer")
        gated { c, h -> Status.check(Jni16.vinoPlayUnback(c, h, issuer, amount), st) }
    }

    /**
     * Issues one voucher of Fibonacci `face` from `issuer` to `holder` on
     * `rail`. Returns its id (> 0, never reused in this sandbox).
     *
     * @throws NotADenominationException `face` is not a ladder face
     * @throws CoverageException the issuer would fall below 1.8x
     * @throws ZxvUsageException `ZXV_E_ARG` (issuer not an issuer, holder not a holder)
     * @throws ZxvException `ZXV_E_NOT_FOUND`, `ZXV_E_CAPACITY` ([Vino.MAX_VOUCHERS] active)
     */
    fun issue(issuer: Long, holder: Long, face: Long, rail: VinoRail): Long {
        requireU32(issuer, "issuer")
        requireU32(holder, "holder")
        return gated { c, h -> LongArray(1).also { Status.check(Jni16.vinoIssue(c, h, issuer, holder, face, rail.code, it), st) }[0] }
    }

    /**
     * Moves active `voucher` from holder `from` to holder `to`, spending it on
     * `rail`, which must be its active rail. The rail does not change.
     *
     * @throws RailException `rail` is not its active rail
     * @throws ZxvRefusedException `ZXV_E_STATE` (it was redeemed)
     * @throws ZxvUsageException `ZXV_E_ARG` (`from` is not its holder, `to` not a holder, `from == to`)
     * @throws ZxvException `ZXV_E_NOT_FOUND` (no such voucher or account)
     */
    fun transfer(voucher: Long, from: Long, to: Long, rail: VinoRail) {
        requireU32(from, "from")
        requireU32(to, "to")
        gated { c, h -> Status.check(Jni16.vinoTransfer(c, h, voucher, from, to, rail.code), st) }
    }

    /**
     * Switches active `voucher`, by its `holder`, to `rail` (BEARER <-> GHOST).
     * Nothing moves.
     *
     * @throws RailException it is already on `rail`
     * @throws ZxvUsageException `ZXV_E_ARG` (`holder` is not its holder)
     */
    fun setRail(voucher: Long, holder: Long, rail: VinoRail) {
        requireU32(holder, "holder")
        gated { c, h -> Status.check(Jni16.vinoSetRail(c, h, voucher, holder, rail.code), st) }
    }

    /**
     * Redeems active `voucher`, by its `holder`, to its issuer: it is retired
     * and the issuer's outstanding falls by its face. **Nothing is paid out**:
     * no play units, no backing, no live value.
     *
     * @throws ZxvRefusedException `ZXV_E_STATE` (already redeemed)
     * @throws ZxvUsageException `ZXV_E_ARG` (`holder` is not its holder)
     */
    fun redeem(voucher: Long, holder: Long) {
        requireU32(holder, "holder")
        gated { c, h -> Status.check(Jni16.vinoRedeem(c, h, voucher, holder), st) }
    }

    /** A voucher's face, issuer, holder, rail and state. `ZXV_E_NOT_FOUND` for an unknown id. */
    fun voucher(id: Long): Voucher = gated { c, h ->
        val o = LongArray(5)
        Status.check(Jni16.vinoVoucherInfo(c, h, id, o), st)
        Voucher(id, o[0], o[1], o[2], VinoRail.fromCode(o[3].toInt()), VoucherState.fromCode(o[4].toInt()))
    }

    /** An account's owner, kind and totals. `ZXV_E_NOT_FOUND` for an unknown id. */
    fun account(id: Long): Account {
        requireU32(id, "account")
        return gated { c, h ->
            val o = LongArray(5)
            Status.check(Jni16.vinoAccountInfo(c, h, id, o), st)
            Account(id, o[0], o[1] != 0L, o[2], o[3], o[4])
        }
    }

    /**
     * The ids of the active vouchers `account` issued (an issuer) or holds (a
     * holder), ascending. Redeemed vouchers are not listed.
     */
    fun vouchers(account: Long): List<Long> {
        requireU32(account, "account")
        return gated { c, h ->
            val n = LongArray(1)
            var buf: LongArray? = null
            // Two calls: ask for the count, then fill. Another thread may issue in
            // between, so a too-small buffer just asks again.
            while (true) {
                val rc = Jni16.vinoVouchers(c, h, account, buf, n)
                if (rc == E_BUFFER) {
                    buf = LongArray(Math.toIntExact(n[0]))
                    continue
                }
                Status.check(rc, st)
                break
            }
            (buf ?: LongArray(0)).copyOf(n[0].toInt()).toList()
        }
    }

    /**
     * Re-checks every invariant of the sandbox: each active voucher has a
     * ladder face, one rail, an issuer and a holder; each issuer's outstanding
     * equals the sum of its active faces and `10 * backing >= 18 * outstanding`;
     * each holder's total equals what it holds. Returns normally when all hold.
     *
     * @throws ZxvFatalError `ZXV_E_INVARIANT`: one failed and the context is now poisoned
     */
    fun check() {
        gated { c, h -> Status.check(Jni16.vinoPlayCheck(c, h), st) }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the sandbox; every play unit and voucher in it is gone. Idempotent. */
    override fun close() = res.close()
}

/**
 * A payment request (both lanes): a plain value the app builds and the user's
 * wallet approves. Its canonical form is [Vino.REQUEST_BYTES] bytes
 * ([encode]); its id is SHA-256 of those bytes ([id]).
 *
 * `amount` is in VFV minor units for [VinoLane.LIVE] and play units for
 * [VinoLane.PLAY] (play units are not money). Times are seconds of the
 * caller's clock (Unix time by convention); a wallet approves only within
 * `[createdAt, expiresAt]`. A LIVE request may not mark a paid chance outcome
 * ([Vino.FLAG_CHANCE]: loot boxes, paid draws): encoding it is
 * [ChanceException]. The allotment is a separate module, not a payment request.
 *
 * Building one checks nothing; [encode] applies every rule. Byte arrays are
 * copied in and out, so an instance never changes. `amount`, `createdAt` and
 * `expiresAt` are uint64 values in a `Long`.
 *
 * @property merchant the payee's 32-byte id (e.g. SHA-256 of its key)
 * @property nonce 16 bytes, unique per request
 * @property amount 1 .. [Vino.AMOUNT_MAX]
 * @property createdAt when the request was made
 * @property expiresAt after which it can no longer be approved (> [createdAt])
 * @property memo 0..64 printable ASCII characters (0x20..0x7e)
 * @property flags `ZXV_VINO_REQ_*` bits ([Vino.FLAG_CHANCE]); unknown bits are refused
 */
class PaymentRequest internal constructor(
    /** The raw lane code; [lane] is its enum (codes other than 1 and 2 are refused by [encode]). */
    internal val laneCode: Int,
    merchant: ByteArray,
    nonce: ByteArray,
    val amount: Long,
    val createdAt: Long,
    val expiresAt: Long,
    val memo: String,
    val flags: Int,
) {
    private val merchantBytes = merchant.copyOf()
    private val nonceBytes = nonce.copyOf()

    init {
        requireLength(merchantBytes, 32, "merchant")
        requireLength(nonceBytes, 16, "nonce")
    }

    /**
     * A request for `lane`.
     *
     * @throws ZxvUsageException `ZXV_E_ARG` if `merchant` is not 32 bytes or `nonce` not 16
     */
    @JvmOverloads
    constructor(
        lane: VinoLane,
        merchant: ByteArray,
        nonce: ByteArray,
        amount: Long,
        createdAt: Long,
        expiresAt: Long,
        memo: String = "",
        flags: Int = 0,
    ) : this(lane.code, merchant, nonce, amount, createdAt, expiresAt, memo, flags)

    /** The lane: [VinoLane.PLAY] or [VinoLane.LIVE]. */
    val lane: VinoLane get() = VinoLane.fromCode(laneCode)

    /** The payee's 32-byte id (a copy). */
    val merchant: ByteArray get() = merchantBytes.copyOf()

    /** The 16-byte nonce (a copy). */
    val nonce: ByteArray get() = nonceBytes.copyOf()

    /** True if [flags] marks a paid chance outcome ([Vino.FLAG_CHANCE]); allowed on PLAY only. */
    val hasChanceOutcome: Boolean get() = flags and Vino.FLAG_CHANCE != 0

    /**
     * The canonical [Vino.REQUEST_BYTES]-byte record. Pure.
     *
     * @throws LaneException the lane is not PLAY or LIVE
     * @throws ChanceException a LIVE request with [Vino.FLAG_CHANCE]
     * @throws ZxvException `ZXV_E_RANGE` (amount 0 or above [Vino.AMOUNT_MAX],
     *   `expiresAt <= createdAt`), `ZXV_E_FORMAT` (unknown flag, memo not
     *   printable ASCII), `ZXV_E_TOO_LONG` (memo above 64 bytes)
     */
    fun encode(): ByteArray = encodeInto(null)

    /** The request id: SHA-256 of [encode]'s bytes (32 bytes). Same refusals as [encode]. */
    fun id(): ByteArray = ByteArray(Vino.REQUEST_ID_BYTES).also { encodeInto(it) }

    private fun encodeInto(id: ByteArray?): ByteArray {
        val m = if (memo.isEmpty()) null else cstr(memo, "memo")
        val out = ByteArray(Vino.REQUEST_BYTES)
        Status.check(Jni16.vinoRequestEncode(laneCode, flags, merchantBytes, nonceBytes, amount, createdAt, expiresAt, m, out, id), null)
        return out
    }

    override fun equals(other: Any?): Boolean = other is PaymentRequest && laneCode == other.laneCode &&
        merchantBytes.contentEquals(other.merchantBytes) && nonceBytes.contentEquals(other.nonceBytes) &&
        amount == other.amount && createdAt == other.createdAt && expiresAt == other.expiresAt &&
        memo == other.memo && flags == other.flags

    override fun hashCode(): Int = listOf(
        laneCode, merchantBytes.contentHashCode(), nonceBytes.contentHashCode(), amount, createdAt, expiresAt, memo, flags,
    ).hashCode()

    override fun toString(): String =
        "PaymentRequest(lane=$laneCode, amount=${java.lang.Long.toUnsignedString(amount)}, " +
            "createdAt=${java.lang.Long.toUnsignedString(createdAt)}, expiresAt=${java.lang.Long.toUnsignedString(expiresAt)}, " +
            "memo=\"$memo\", flags=$flags)"

    companion object {
        /**
         * Decodes and validates request bytes: the same rules as [encode], plus
         * the magic, version and zero padding (`ZXV_E_FORMAT`). Pure.
         *
         * @throws ZxvUsageException `ZXV_E_ARG` if `bytes` is not [Vino.REQUEST_BYTES] long
         */
        @JvmStatic
        fun decode(bytes: ByteArray): PaymentRequest = decodeWithId(bytes).first

        /** The id (SHA-256) of valid request bytes; refuses invalid ones as [decode] does. */
        @JvmStatic
        fun idOf(bytes: ByteArray): ByteArray = decodeWithId(bytes).second

        private fun decodeWithId(bytes: ByteArray): Pair<PaymentRequest, ByteArray> {
            requireLength(bytes, Vino.REQUEST_BYTES, "request")
            val o = LongArray(5)
            val merchant = ByteArray(32)
            val nonce = ByteArray(16)
            val memo = ByteArray(Vino.MEMO_MAX + 1)
            val id = ByteArray(Vino.REQUEST_ID_BYTES)
            Status.check(Jni16.vinoRequestDecode(bytes, o, merchant, nonce, memo, id), null)
            val memoLen = memo.indexOf(0).let { if (it < 0) Vino.MEMO_MAX else it }
            val req = PaymentRequest(
                o[0].toInt(), merchant, nonce, o[2], o[3], o[4], String(memo, 0, memoLen, Charsets.US_ASCII), o[1].toInt(),
            )
            return req to id
        }
    }
}

/**
 * Receipts: the user's wallet approving a [PaymentRequest] (both lanes).
 *
 * A receipt is the wallet's ML-DSA-87 signature (empty FIPS 204 context) over
 * [Vino.RECEIPT_MESSAGE_BYTES] bytes: the tag `"zxv/vino-rcpt/v1"` || the
 * request id || SHA-256(wallet public key) || `approvedAt` (8 bytes,
 * little-endian). The request carries its expiry: a receipt for an expired
 * request, or approved after it, is [ExpiredException].
 *
 * The app's half is [verify] (pure and offline: no capability, no network).
 * [sign] is the wallet's call and needs [Permission.CRYPTO_SIGN]; an app never
 * holds the user's key. ML-DSA-87 here is the reference implementation, not
 * FIPS 140-3 validated.
 */
object VinoReceipt {
    /**
     * The [Vino.RECEIPT_MESSAGE_BYTES] bytes a wallet signs to approve request
     * `requestId` (32 bytes) with public key `walletPublicKey` (2592 bytes) at
     * `approvedAt`. Pure.
     */
    @JvmStatic
    fun message(requestId: ByteArray, walletPublicKey: ByteArray, approvedAt: Long): ByteArray {
        requireLength(requestId, Vino.REQUEST_ID_BYTES, "requestId")
        requireLength(walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES, "walletPublicKey")
        return ByteArray(Vino.RECEIPT_MESSAGE_BYTES).also { Status.check(Jni16.vinoReceiptMsg(requestId, walletPublicKey, approvedAt, it), null) }
    }

    /**
     * Wallet side: approves `request` (its [Vino.REQUEST_BYTES] encoded bytes)
     * at `approvedAt`, which must lie within the request's
     * `[createdAt, expiresAt]`, and signs with the wallet's ML-DSA-87 key.
     * Returns the [Vino.RECEIPT_SIGNATURE_BYTES]-byte signature (hedged, so it
     * differs on every call; each one verifies).
     *
     * The secret key stays in [SecretKey] memory: it is passed to libzxv by
     * address, never copied to the heap, and this object keeps no reference.
     *
     * @param cap needs [Permission.CRYPTO_SIGN]
     * @param walletSecretKey the wallet's [Vino.WALLET_SECRET_KEY_BYTES]-byte key
     * @param walletPublicKey its public key
     * @throws ExpiredException `approvedAt` is outside the request's window
     * @throws CapabilityDeniedException without CRYPTO_SIGN
     * @throws ZxvException any refusal of decoding the request
     */
    @JvmStatic
    fun sign(cap: Capability, request: ByteArray, walletSecretKey: SecretKey, walletPublicKey: ByteArray, approvedAt: Long): ByteArray {
        requireLength(request, Vino.REQUEST_BYTES, "request")
        requireLength(walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES, "walletPublicKey")
        if (walletSecretKey.size != Vino.WALLET_SECRET_KEY_BYTES) {
            throw Status.usage(E_ARG, "a wallet secret key is ${Vino.WALLET_SECRET_KEY_BYTES} bytes, got ${walletSecretKey.size}")
        }
        val sig = ByteArray(Vino.RECEIPT_SIGNATURE_BYTES)
        cap.res.use { c ->
            walletSecretKey.withPointer { sk ->
                Status.check(Jni16.vinoReceiptSign(c, request, sk, walletPublicKey, approvedAt, sig), cap.context.state)
            }
        }
        return sig
    }

    /**
     * App side, offline: returns normally if `signature` is the wallet's
     * approval of `request` at `approvedAt`, with `approvedAt` inside the
     * request's window and `now` not past its expiry. Pure: no capability.
     *
     * Times are seconds of the caller's clock (Unix time by convention).
     *
     * @throws VerificationFailedException not this wallet's signature over this request and time
     * @throws ExpiredException the request expired, or `approvedAt` is outside its window
     * @throws ZxvException any refusal of decoding the request
     */
    @JvmStatic
    fun verify(request: ByteArray, walletPublicKey: ByteArray, approvedAt: Long, signature: ByteArray, now: Long) {
        requireLength(request, Vino.REQUEST_BYTES, "request")
        requireLength(walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES, "walletPublicKey")
        requireLength(signature, Vino.RECEIPT_SIGNATURE_BYTES, "signature")
        Status.check(Jni16.vinoReceiptVerify(request, walletPublicKey, approvedAt, signature, now), null)
    }

    /**
     * [verify] as a boolean: false for a bad signature ([VerificationFailedException])
     * or an expired request ([ExpiredException]); every other error still throws.
     */
    @JvmStatic
    fun isValid(request: ByteArray, walletPublicKey: ByteArray, approvedAt: Long, signature: ByteArray, now: Long): Boolean =
        try {
            verify(request, walletPublicKey, approvedAt, signature, now)
            true
        } catch (_: VerificationFailedException) {
            false
        } catch (_: ExpiredException) {
            false
        }
}

/**
 * A per-device daily spend counter (`zxv_vino_meter`) for a deployment that
 * sets a daily cap. It counts what one device approved per day index and
 * refuses an amount that would pass the cap.
 *
 * It is a counter, not a legal check, and makes no exemption claim.
 *
 * Needs [Permission.VINO] on [capability]. Thread-safe (libzxv serializes the
 * calls). Close it before its context; use after close throws [IllegalStateException].
 */
class SpendMeter private constructor(
    /** The capability every call passes. */
    val capability: Capability,
    /** The most one device may approve per day. */
    val dailyCap: Long,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    private val res = NativeResource("spend meter", handle, Jni16::vinoMeterDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /**
     * What [tryAdmit] decided.
     *
     * @property admitted true if the amount was counted
     * @property left what is left today after the call (unchanged when refused)
     */
    data class Admission(val admitted: Boolean, val left: Long)

    companion object {
        /**
         * A meter with a cap of `dailyCap` per day (1 .. [Vino.AMOUNT_MAX]).
         *
         * @throws CapabilityDeniedException without [Permission.VINO]
         * @throws ZxvException `ZXV_E_RANGE` for a cap out of range
         */
        @JvmStatic
        fun create(cap: Capability, dailyCap: Long): SpendMeter = cap.res.use { h ->
            val o = LongArray(1)
            Status.check(Jni16.vinoMeterCreate(h, dailyCap, o), cap.context.state)
            SpendMeter(cap, dailyCap, o[0])
        }
    }

    /**
     * Counts `amount` against day `day` (e.g. Unix time / 86400) and returns
     * what is left that day. A day after the last one starts a fresh count.
     *
     * @throws LimitException the day's total would pass the cap; nothing was counted
     * @throws ZxvException `ZXV_E_RANGE`: `day` is before the last day seen
     *   (the clock went back), or amount is 0 or above [Vino.AMOUNT_MAX]
     */
    fun admit(day: Long, amount: Long): Long = capability.res.use { c ->
        res.use { h ->
            val left = LongArray(1)
            Status.check(Jni16.vinoMeterAdmit(c, h, day, amount, left), st)
            left[0]
        }
    }

    /**
     * [admit] without an exception for the cap: returns whether `amount` was
     * counted and what is left today. Other errors still throw.
     */
    fun tryAdmit(day: Long, amount: Long): Admission = capability.res.use { c ->
        res.use { h ->
            val left = LongArray(1)
            when (val rc = Jni16.vinoMeterAdmit(c, h, day, amount, left)) {
                ZxvStatus.E_LIMIT -> Admission(false, left[0])
                else -> {
                    Status.check(rc, st)
                    Admission(true, left[0])
                }
            }
        }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the meter. Idempotent. */
    override fun close() = res.close()
}
