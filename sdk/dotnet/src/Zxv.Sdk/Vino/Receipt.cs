// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Vino;

/// <summary>
/// Wallet receipts for Vino payment requests (ABI 1.6): the wallet's ML-DSA-87
/// signature approving one request at one time, which an app checks offline.
/// </summary>
/// <remarks>
/// <para>
/// A receipt signs the 88-byte <see cref="ReceiptMessage"/> (empty FIPS 204
/// context): the tag <c>"zxv/vino-rcpt/v1"</c>, the request id, SHA-256 of the
/// wallet's public key, and the approval time. The approval time must lie
/// within the request's window [created, expires]; a receipt outside it, or
/// checked after the request expired, is not accepted.
/// </para>
/// <para>
/// WHO CALLS WHAT. <see cref="Sign"/> is the wallet's call: it needs the
/// wallet's secret key and <see cref="CapabilityRights.CryptoSign"/>. An app
/// never holds a wallet key and only calls <see cref="Verify"/> and
/// <see cref="CreateMessage"/>, which are pure: no capability, no state, safe
/// on any thread. A valid receipt shows the user approved the request; the
/// payment itself is posted by the wallet on its own ledger.
/// </para>
/// <para>
/// HONEST LIMITS. ML-DSA-87 here is the reference implementation, not FIPS
/// 140-3 validated. A build without post-quantum crypto raises
/// <see cref="ZxvFeatureNotAvailableException"/> from <see cref="Sign"/> and
/// <see cref="Verify"/>.
/// </para>
/// </remarks>
public static class Receipt
{
    /// <summary>The wallet public key length (ML-DSA-87).</summary>
    public const int PublicKeyLength = MlDsa87.PublicKeyBytes;

    /// <summary>The wallet secret key length (ML-DSA-87).</summary>
    public const int SecretKeyLength = MlDsa87.SecretKeyBytes;

    /// <summary>The receipt signature length (ML-DSA-87).</summary>
    public const int SignatureLength = MlDsa87.SignatureBytes;

    /// <summary>Builds the 88 bytes a wallet signs to approve a request at a given time.</summary>
    /// <param name="requestId">The request's id (<see cref="PaymentRequestBytes.Id"/>).</param>
    /// <param name="walletPublicKey">The wallet's ML-DSA-87 public key (2592 bytes).</param>
    /// <param name="approvedAt">The approval time, in the request's clock.</param>
    /// <returns>The message.</returns>
    /// <exception cref="ArgumentException"><paramref name="requestId"/> is <c>default</c>, or the key is not 2592 bytes.</exception>
    public static unsafe ReceiptMessage CreateMessage(PaymentRequestId requestId, ReadOnlySpan<byte> walletPublicKey, ulong approvedAt)
    {
        NativeBuffers.RequireLength(requestId.Bytes, PaymentRequestId.Length, nameof(requestId));
        NativeBuffers.RequireLength(walletPublicKey, PublicKeyLength, nameof(walletPublicKey));
        var output = new byte[ReceiptMessage.Length];
        fixed (byte* id = requestId.Bytes)
        fixed (byte* pk = walletPublicKey)
        fixed (byte* o = output)
        {
            NativeCall.Check(NativeMethods.VinoReceiptMsg(id, pk, approvedAt, o), "Receipt.CreateMessage");
        }

        return ReceiptMessage.Own(output);
    }

    /// <summary>
    /// Wallet side: approves <paramref name="request"/> at
    /// <paramref name="approvedAt"/> and signs the receipt with the wallet's
    /// ML-DSA-87 key (hedged signing: each call gives a different, equally
    /// valid signature). The secret key is used in place and never copied or
    /// logged by the SDK; it stays the caller's.
    /// </summary>
    /// <param name="capability">The wallet's capability; needs <see cref="CapabilityRights.CryptoSign"/>.</param>
    /// <param name="request">The request bytes as the app encoded them; they are validated first.</param>
    /// <param name="walletSecretKey">The wallet's ML-DSA-87 secret key (4896 bytes).</param>
    /// <param name="walletPublicKey">The matching public key (2592 bytes).</param>
    /// <param name="approvedAt">The approval time; must be within the request's [created, expires] window.</param>
    /// <returns>The 4627-byte receipt signature.</returns>
    /// <exception cref="ZxvRefusedException">The approval time is outside the request's window (<see cref="ZxvStatus.Expired"/>), or the request is refused as for <see cref="PaymentRequest.Decode(PaymentRequestBytes)"/>.</exception>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.CryptoSign"/>.</exception>
    /// <exception cref="ZxvUsageException">The request bytes are malformed, as for <see cref="PaymentRequest.Decode(PaymentRequestBytes)"/>.</exception>
    /// <exception cref="ArgumentException">A key or the request has the wrong length.</exception>
    public static unsafe byte[] Sign(Capability capability, PaymentRequestBytes request, SecretKey walletSecretKey, ReadOnlySpan<byte> walletPublicKey, ulong approvedAt)
    {
        ArgumentNullException.ThrowIfNull(capability);
        ArgumentNullException.ThrowIfNull(walletSecretKey);
        NativeBuffers.RequireLength(request.Bytes, PaymentRequestBytes.Length, nameof(request));
        NativeBuffers.RequireLength(walletPublicKey, PublicKeyLength, nameof(walletPublicKey));
        walletSecretKey.RequireLength(SecretKeyLength, nameof(walletSecretKey));
        capability.Enter("Receipt.Sign");
        var sig = new byte[SignatureLength];
        fixed (byte* r = request.Bytes)
        fixed (byte* pk = walletPublicKey)
        {
            byte* rp = r, pkp = pk;
            walletSecretKey.Use(sk =>
            {
                fixed (byte* g = sig)
                {
                    capability.Check(NativeMethods.VinoReceiptSign(capability.Handle, rp, sk, pkp, approvedAt, g), "Receipt.Sign");
                }
            });
        }

        return sig;
    }

    /// <summary>
    /// App side, offline: checks that <paramref name="signature"/> is the
    /// wallet's approval of <paramref name="request"/> at
    /// <paramref name="approvedAt"/>, that the approval lies within the
    /// request's window, and that <paramref name="now"/> is not past its expiry.
    /// </summary>
    /// <param name="request">The request bytes the app encoded.</param>
    /// <param name="walletPublicKey">The wallet's ML-DSA-87 public key (2592 bytes).</param>
    /// <param name="approvedAt">The approval time the wallet reported with the receipt.</param>
    /// <param name="signature">The receipt signature (4627 bytes).</param>
    /// <param name="now">The current time, in the request's clock.</param>
    /// <returns>
    /// <see cref="ReceiptVerdict.Valid"/>, <see cref="ReceiptVerdict.NotApproved"/>
    /// (not that wallet's signature over this request and time) or
    /// <see cref="ReceiptVerdict.Expired"/>. Only <see cref="ReceiptVerdict.Valid"/> means approved.
    /// </returns>
    /// <exception cref="ZxvException">The request bytes are refused, as for <see cref="PaymentRequest.Decode(PaymentRequestBytes)"/>.</exception>
    /// <exception cref="ZxvFeatureNotAvailableException">The library was built without post-quantum crypto.</exception>
    /// <exception cref="ArgumentException">A key, the signature or the request has the wrong length.</exception>
    public static unsafe ReceiptVerdict Verify(PaymentRequestBytes request, ReadOnlySpan<byte> walletPublicKey, ulong approvedAt, ReadOnlySpan<byte> signature, ulong now)
    {
        NativeBuffers.RequireLength(request.Bytes, PaymentRequestBytes.Length, nameof(request));
        NativeBuffers.RequireLength(walletPublicKey, PublicKeyLength, nameof(walletPublicKey));
        NativeBuffers.RequireLength(signature, SignatureLength, nameof(signature));
        int rc;
        fixed (byte* r = request.Bytes)
        fixed (byte* pk = walletPublicKey)
        fixed (byte* g = signature)
        {
            rc = NativeMethods.VinoReceiptVerify(r, pk, approvedAt, g, now);
        }

        switch ((ZxvStatus)rc)
        {
            case ZxvStatus.Ok: return ReceiptVerdict.Valid;
            case ZxvStatus.VerificationFailed: return ReceiptVerdict.NotApproved;
            case ZxvStatus.Expired: return ReceiptVerdict.Expired;
            default: throw NativeCall.Create(rc, "Receipt.Verify");
        }
    }
}
