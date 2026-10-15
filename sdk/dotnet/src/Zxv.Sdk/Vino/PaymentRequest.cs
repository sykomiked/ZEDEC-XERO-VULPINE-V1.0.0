// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using Zxv.Interop;

namespace Zxv.Vino;

/// <summary>
/// A Vino payment request: what an app asks the user's wallet to approve. A
/// plain immutable value; <see cref="Encode"/> turns it into its 160 canonical
/// bytes and <see cref="Decode(PaymentRequestBytes)"/> reads them back.
/// </summary>
/// <remarks>
/// <para>
/// THE LIVE FLOW. The app builds a request and encodes it, the user approves
/// it in their own ZXV wallet (which signs a receipt with its ML-DSA-87 key,
/// <see cref="Receipt.Sign"/>), and the app checks the receipt offline with
/// <see cref="Receipt.Verify"/>. The app never sees a balance, a key or a
/// custody account; approving and posting the payment happen in the wallet.
/// </para>
/// <para>
/// VALIDATION. The constructor checks only what .NET can see at once (the
/// merchant and nonce lengths, a non-null memo with no NUL). Everything else
/// is checked by <see cref="Encode"/> with the C ABI's rules, so the same
/// request is accepted or refused the same way in every ZXV SDK. Encoding and
/// decoding are pure: no capability, no state, safe on any thread.
/// </para>
/// <para>
/// A paid chance outcome (a loot box, a paid draw) may never be sold on the
/// LIVE lane: encoding such a request raises
/// <see cref="ZxvStatus.PaidChance"/>. The allotment is a separate module and is
/// not a payment request.
/// </para>
/// </remarks>
public sealed class PaymentRequest : IEquatable<PaymentRequest>
{
    /// <summary>The merchant id length in bytes.</summary>
    public const int MerchantLength = 32;

    /// <summary>The nonce length in bytes.</summary>
    public const int NonceLength = 16;

    /// <summary>The longest memo, in characters (<c>ZXV_VINO_MEMO_MAX</c>). A memo is printable ASCII (0x20..0x7e).</summary>
    public const int MaxMemoLength = 64;

    /// <summary>The largest amount, 2^59 (<c>ZXV_VINO_AMOUNT_MAX</c>).</summary>
    public const ulong MaxAmount = 1UL << 59;

    private readonly byte[] _merchant;
    private readonly byte[] _nonce;

    /// <summary>Creates a request. Nothing beyond the lengths is checked until <see cref="Encode"/>.</summary>
    /// <param name="lane">The lane: <see cref="VinoLane.Live"/> for real Vino, <see cref="VinoLane.Play"/> for play units.</param>
    /// <param name="merchant">The payee's 32-byte id (for example SHA-256 of its public key). Copied.</param>
    /// <param name="nonce">16 bytes unique to this request. Copied.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>: VFV minor units on the LIVE lane, play units on the PLAY lane.</param>
    /// <param name="createdAt">When the request was made, in seconds of your clock (Unix time is the convention).</param>
    /// <param name="expiresAt">When it expires, in the same clock; must be after <paramref name="createdAt"/>.</param>
    /// <param name="memo">0..64 printable ASCII characters shown to the user, for example the item name.</param>
    /// <param name="flags">Request flags; <see cref="PaymentRequestFlags.PaidChance"/> is refused on the LIVE lane.</param>
    /// <exception cref="ArgumentException"><paramref name="merchant"/> is not 32 bytes, <paramref name="nonce"/> is not 16 bytes, or <paramref name="memo"/> contains a NUL character.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="memo"/> is <see langword="null"/>.</exception>
    public PaymentRequest(VinoLane lane, ReadOnlySpan<byte> merchant, ReadOnlySpan<byte> nonce, ulong amount, ulong createdAt, ulong expiresAt, string memo = "", PaymentRequestFlags flags = PaymentRequestFlags.None)
    {
        NativeBuffers.RequireLength(merchant, MerchantLength, nameof(merchant));
        NativeBuffers.RequireLength(nonce, NonceLength, nameof(nonce));
        Memo = Guard.Text(memo, nameof(memo), allowEmpty: true);
        Lane = lane;
        Flags = flags;
        _merchant = merchant.ToArray();
        _nonce = nonce.ToArray();
        Amount = amount;
        CreatedAt = createdAt;
        ExpiresAt = expiresAt;
    }

    /// <summary>Gets the lane.</summary>
    public VinoLane Lane { get; }

    /// <summary>Gets the flags.</summary>
    public PaymentRequestFlags Flags { get; }

    /// <summary>Gets the payee's 32-byte id.</summary>
    public ReadOnlySpan<byte> Merchant => _merchant;

    /// <summary>Gets the 16-byte nonce.</summary>
    public ReadOnlySpan<byte> Nonce => _nonce;

    /// <summary>Gets the amount: VFV minor units (LIVE) or play units (PLAY). An exact integer.</summary>
    public ulong Amount { get; }

    /// <summary>Gets the creation time, in seconds of the caller's clock.</summary>
    public ulong CreatedAt { get; }

    /// <summary>Gets the expiry time, in seconds of the caller's clock. A receipt approved after it, or checked after it, is refused.</summary>
    public ulong ExpiresAt { get; }

    /// <summary>Gets the memo shown to the user (printable ASCII, possibly empty).</summary>
    public string Memo { get; }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether every field is equal.</returns>
    public static bool operator ==(PaymentRequest? left, PaymentRequest? right) => left is null ? right is null : left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether any field differs.</returns>
    public static bool operator !=(PaymentRequest? left, PaymentRequest? right) => !(left == right);

    /// <summary>
    /// Validates and decodes 160 request bytes, for example ones received from
    /// an app. Uses the same rules as <see cref="Encode"/>, plus the magic,
    /// version and zero padding.
    /// </summary>
    /// <param name="bytes">The request bytes.</param>
    /// <returns>The request.</returns>
    /// <exception cref="ZxvUsageException">A bad lane (<see cref="ZxvStatus.WrongLane"/>), bad magic, version, padding, unknown flag or memo byte (<see cref="ZxvStatus.InvalidFormat"/>), a memo length above 64 (<see cref="ZxvStatus.TooLong"/>), or a bad amount or window (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    /// <exception cref="ZxvRefusedException">A LIVE request marked as a paid chance outcome (<see cref="ZxvStatus.PaidChance"/>).</exception>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> is <c>default</c>.</exception>
    public static PaymentRequest Decode(PaymentRequestBytes bytes) => Decode(bytes.Bytes);

    /// <summary>Validates and decodes 160 request bytes. See <see cref="Decode(PaymentRequestBytes)"/>.</summary>
    /// <param name="bytes">Exactly 160 bytes.</param>
    /// <returns>The request.</returns>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> is not 160 bytes long.</exception>
    /// <exception cref="ZxvException">The bytes are refused, as for <see cref="Decode(PaymentRequestBytes)"/>.</exception>
    public static unsafe PaymentRequest Decode(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, PaymentRequestBytes.Length, nameof(bytes));
        Span<byte> merchant = stackalloc byte[MerchantLength];
        Span<byte> nonce = stackalloc byte[NonceLength];
        Span<byte> memo = stackalloc byte[MaxMemoLength + 1];
        int lane;
        uint flags;
        ulong amount, created, expires;
        fixed (byte* input = bytes)
        fixed (byte* m = merchant)
        fixed (byte* n = nonce)
        fixed (byte* t = memo)
        {
            NativeCall.Check(NativeMethods.VinoRequestDecode(input, out lane, out flags, m, n, out amount, out created, out expires, t, null), "PaymentRequest.Decode");
        }

        // The memo is printable ASCII, validated natively, so this is lossless.
        return new PaymentRequest((VinoLane)lane, merchant, nonce, amount, created, expires, NativeBuffers.FromZ(memo), (PaymentRequestFlags)flags);
    }

    /// <summary>
    /// Validates the request and encodes it as its 160 canonical bytes. The
    /// request id is <see cref="PaymentRequestBytes.Id"/> of the result.
    /// </summary>
    /// <returns>The bytes to hand to the user's wallet.</returns>
    /// <exception cref="ZxvUsageException">The lane is not PLAY or LIVE (<see cref="ZxvStatus.WrongLane"/>); an unknown flag or a memo that is not printable ASCII (<see cref="ZxvStatus.InvalidFormat"/>); the amount is 0 or above <see cref="MaxAmount"/>, or the expiry is not after the creation time (<see cref="ZxvStatus.AmountOutOfRange"/>); or the memo is longer than 64 (<see cref="ZxvStatus.TooLong"/>).</exception>
    /// <exception cref="ZxvRefusedException">A LIVE request with <see cref="PaymentRequestFlags.PaidChance"/> (<see cref="ZxvStatus.PaidChance"/>).</exception>
    public unsafe PaymentRequestBytes Encode()
    {
        // Non-ASCII memo characters become multi-byte UTF-8, which the native
        // check refuses as not printable ASCII (ZXV_E_FORMAT).
        byte[] memo = Encoding.UTF8.GetBytes(Memo + "\0");
        var output = new byte[PaymentRequestBytes.Length];
        fixed (byte* m = _merchant)
        fixed (byte* n = _nonce)
        fixed (byte* t = memo)
        fixed (byte* o = output)
        {
            NativeCall.Check(NativeMethods.VinoRequestEncode((int)Lane, (uint)Flags, m, n, Amount, CreatedAt, ExpiresAt, t, o, null), "PaymentRequest.Encode");
        }

        return PaymentRequestBytes.Own(output);
    }

    /// <inheritdoc/>
    public bool Equals(PaymentRequest? other) =>
        other is not null
        && Lane == other.Lane
        && Flags == other.Flags
        && Amount == other.Amount
        && CreatedAt == other.CreatedAt
        && ExpiresAt == other.ExpiresAt
        && string.Equals(Memo, other.Memo, StringComparison.Ordinal)
        && Merchant.SequenceEqual(other.Merchant)
        && Nonce.SequenceEqual(other.Nonce);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is PaymentRequest r && Equals(r);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.Add(Lane);
        h.Add(Flags);
        h.Add(Amount);
        h.Add(CreatedAt);
        h.Add(ExpiresAt);
        h.Add(Memo, StringComparer.Ordinal);
        h.AddBytes(Merchant);
        h.AddBytes(Nonce);
        return h.ToHashCode();
    }

    /// <summary>A short description for logs (no secrets are involved).</summary>
    /// <returns>The lane, amount, window and memo.</returns>
    public override string ToString() => $"PaymentRequest({Lane}, amount {Amount}, {CreatedAt}..{ExpiresAt}, \"{Memo}\")";
}
