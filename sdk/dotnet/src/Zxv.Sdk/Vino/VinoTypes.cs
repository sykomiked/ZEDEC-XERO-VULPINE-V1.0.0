// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Security.Cryptography;

namespace Zxv.Vino;

/// <summary>
/// The two lanes of Vino Embed (<c>ZXV_VINO_LANE_*</c>). They never meet:
/// nothing in the SDK takes a play sandbox and something live together, so
/// play units can never become live value.
/// </summary>
public enum VinoLane
{
    /// <summary>The play lane (<c>ZXV_VINO_LANE_PLAY</c>, 1): a sandbox inside the app with its own play units, which are not money.</summary>
    Play = 1,

    /// <summary>The live lane (<c>ZXV_VINO_LANE_LIVE</c>, 2): real Vino held in the user's own ZXV wallet, never in the app.</summary>
    Live = 2,
}

/// <summary>
/// The rail a play-lane voucher is active on (<c>ZXV_VINO_RAIL_*</c>). A voucher
/// is active on exactly one rail at a time, and a transfer must name that rail,
/// so one voucher can never be spent twice.
/// </summary>
public enum VinoRail
{
    /// <summary>The note in the holder's hand (<c>ZXV_VINO_RAIL_BEARER</c>, 0).</summary>
    Bearer = 0,

    /// <summary>Its twin on the play ledger (<c>ZXV_VINO_RAIL_GHOST</c>, 1).</summary>
    Ghost = 1,
}

/// <summary>The state of a play-lane voucher (<c>ZXV_VINO_V_*</c>).</summary>
public enum VoucherState
{
    /// <summary>Held by a holder and spendable (<c>ZXV_VINO_V_ACTIVE</c>, 1).</summary>
    Active = 1,

    /// <summary>Returned to its issuer and retired (<c>ZXV_VINO_V_REDEEMED</c>, 2). Its id stays listed but it can never move again.</summary>
    Redeemed = 2,
}

/// <summary>Flags on a payment request (<c>ZXV_VINO_REQ_*</c>).</summary>
[Flags]
#pragma warning disable CA1028, CA1711 // uint matches the C ABI's flags field; "Flags" is the C name.
public enum PaymentRequestFlags : uint
#pragma warning restore CA1028, CA1711
{
    /// <summary>No flag.</summary>
    None = 0,

    /// <summary>
    /// The purchase has a paid chance outcome, such as a loot box or a paid draw
    /// (<c>ZXV_VINO_REQ_CHANCE</c>). Allowed only on the play lane: a LIVE
    /// request with this flag is refused with <see cref="Interop.ZxvStatus.PaidChance"/>.
    /// </summary>
    PaidChance = 0x1,
}

/// <summary>What a play-lane voucher is and where it is.</summary>
/// <param name="Id">The voucher id (above 0, never reused in its sandbox).</param>
/// <param name="Face">Its face in play units: a Fibonacci ladder face (1, 2, 3, 5, 8, 13, ...).</param>
/// <param name="Issuer">The issuer account that issued it and to which it redeems.</param>
/// <param name="Holder">The holder account that holds it (the last holder, once redeemed).</param>
/// <param name="Rail">The one rail it is active on.</param>
/// <param name="State">Active or redeemed.</param>
public readonly record struct VoucherInfo(ulong Id, ulong Face, uint Issuer, uint Holder, VinoRail Rail, VoucherState State);

/// <summary>A play-lane account and its totals.</summary>
/// <param name="Account">The account id (counted up from 0 in its sandbox).</param>
/// <param name="Owner">The owner id given when it was opened.</param>
/// <param name="IsIssuer"><see langword="true"/> for an issuer, <see langword="false"/> for a holder.</param>
/// <param name="Backing">For an issuer, the play units of backing it has posted; 0 for a holder.</param>
/// <param name="Faces">For an issuer, the faces it has outstanding; for a holder, the faces it holds.</param>
/// <param name="ActiveVouchers">How many active vouchers it issued (issuer) or holds (holder).</param>
public readonly record struct VinoAccount(uint Account, uint Owner, bool IsIssuer, ulong Backing, ulong Faces, uint ActiveVouchers);

/// <summary>
/// The 32-byte id of a payment request: SHA-256 of its 160 canonical bytes. A
/// wallet receipt names the request by this id.
/// </summary>
public readonly struct PaymentRequestId : IEquatable<PaymentRequestId>
{
    /// <summary>The id length in bytes.</summary>
    public const int Length = 32;

    private readonly byte[]? _bytes;

    private PaymentRequestId(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the 32 bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Wraps a copy of 32 id bytes.</summary>
    /// <param name="bytes">Exactly 32 bytes.</param>
    /// <returns>The id.</returns>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> is not 32 bytes long.</exception>
    public static PaymentRequestId FromBytes(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, Length, nameof(bytes));
        return new PaymentRequestId(bytes.ToArray());
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(PaymentRequestId left, PaymentRequestId right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(PaymentRequestId left, PaymentRequestId right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The 32 bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(PaymentRequestId other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is PaymentRequestId o && Equals(o);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <summary>The id as lowercase hex.</summary>
    /// <returns>64 hex digits.</returns>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();
}

/// <summary>
/// The 160 canonical bytes of a payment request (<c>ZXV_VINO_REQ_BYTES</c>):
/// what an app sends to the user's wallet, and what the wallet signs over.
/// </summary>
/// <remarks>
/// This type only holds 160 bytes; it does not check them. Use
/// <see cref="PaymentRequest.Decode(PaymentRequestBytes)"/> to validate and
/// read them. <see cref="Receipt.Sign"/> and <see cref="Receipt.Verify"/>
/// validate them too.
/// </remarks>
public readonly struct PaymentRequestBytes : IEquatable<PaymentRequestBytes>
{
    /// <summary>The record length in bytes (<c>ZXV_VINO_REQ_BYTES</c>).</summary>
    public const int Length = 160;

    private readonly byte[]? _bytes;

    private PaymentRequestBytes(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the 160 bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets the request id: SHA-256 of the 160 bytes, as the C ABI computes it.</summary>
    /// <exception cref="InvalidOperationException">This is <c>default</c> and holds no bytes.</exception>
    public PaymentRequestId Id => _bytes is null
        ? throw new InvalidOperationException("A default PaymentRequestBytes holds no request.")
        : PaymentRequestId.FromBytes(SHA256.HashData(_bytes));

    /// <summary>Wraps a copy of 160 request bytes, for example ones received from another party. Nothing is validated here.</summary>
    /// <param name="bytes">Exactly 160 bytes.</param>
    /// <returns>The bytes.</returns>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> is not 160 bytes long.</exception>
    public static PaymentRequestBytes FromBytes(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, Length, nameof(bytes));
        return new PaymentRequestBytes(bytes.ToArray());
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(PaymentRequestBytes left, PaymentRequestBytes right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(PaymentRequestBytes left, PaymentRequestBytes right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The 160 bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(PaymentRequestBytes other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is PaymentRequestBytes o && Equals(o);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <summary>The bytes as lowercase hex.</summary>
    /// <returns>320 hex digits.</returns>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();

    /// <summary>Wraps bytes the SDK owns, without copying.</summary>
    internal static PaymentRequestBytes Own(byte[] bytes) => new(bytes);
}

/// <summary>
/// The exact 88 bytes a wallet signs to approve a payment request
/// (<c>ZXV_VINO_RCPT_MSG_BYTES</c>): the ASCII tag <c>"zxv/vino-rcpt/v1"</c>,
/// the request id, SHA-256 of the wallet's public key, and the approval time
/// (8 bytes, little-endian). <see cref="Receipt.Sign"/> and
/// <see cref="Receipt.Verify"/> build it for you; use it to sign with your own
/// ML-DSA-87 signer (empty context).
/// </summary>
public readonly struct ReceiptMessage : IEquatable<ReceiptMessage>
{
    /// <summary>The message length in bytes (<c>ZXV_VINO_RCPT_MSG_BYTES</c>).</summary>
    public const int Length = 88;

    private readonly byte[]? _bytes;

    private ReceiptMessage(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the 88 bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(ReceiptMessage left, ReceiptMessage right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(ReceiptMessage left, ReceiptMessage right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The 88 bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(ReceiptMessage other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is ReceiptMessage o && Equals(o);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <summary>The bytes as lowercase hex.</summary>
    /// <returns>176 hex digits.</returns>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();

    /// <summary>Wraps bytes the SDK owns, without copying.</summary>
    internal static ReceiptMessage Own(byte[] bytes) => new(bytes);
}

/// <summary>The outcome of checking a wallet receipt offline with <see cref="Receipt.Verify"/>.</summary>
public enum ReceiptVerdict
{
    /// <summary>The receipt is the wallet's approval of this request at this time, inside the request's window, and the request has not expired.</summary>
    Valid = 0,

    /// <summary>The signature is not that wallet's approval of this request at this approval time (<c>ZXV_E_VERIFY</c>). Treat the payment as not approved.</summary>
    NotApproved = 1,

    /// <summary>The request has expired, or the approval time is outside its window (<c>ZXV_E_EXPIRED</c>). Treat the payment as not approved.</summary>
    Expired = 2,
}
