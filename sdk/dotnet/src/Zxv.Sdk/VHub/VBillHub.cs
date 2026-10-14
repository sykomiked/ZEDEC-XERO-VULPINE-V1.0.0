// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.VHub;

/// <summary>
/// The exact 40 bytes an issuer signs to set a peg: the ASCII tag
/// "zxv/vhub-peg/v1", the slot, then num, den and epoch (each 8 bytes
/// little-endian). Sign <see cref="ToArray"/> with
/// <see cref="MlDsa87.Sign(SecretKey, ReadOnlySpan{byte}, ReadOnlySpan{byte})"/>
/// and an empty context.
/// </summary>
public readonly struct PegMessage : IEquatable<PegMessage>
{
    /// <summary>The message length (<c>ZXV_VHUB_PEG_MSG_BYTES</c>).</summary>
    public const int Length = 40;

    private readonly byte[]? _bytes;

    private PegMessage(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the 40 bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Builds the message for a peg.</summary>
    /// <param name="slot">The currency slot.</param>
    /// <param name="numerator">Minor units of the currency.</param>
    /// <param name="denominator">Per this many V-Bills.</param>
    /// <param name="epoch">The peg epoch.</param>
    /// <returns>The message.</returns>
    public static unsafe PegMessage Create(byte slot, ulong numerator, ulong denominator, ulong epoch)
    {
        var bytes = new byte[Length];
        fixed (byte* p = bytes)
        {
            NativeCall.Check(NativeMethods.VhubPegMsg(slot, numerator, denominator, epoch, p), "PegMessage.Create");
        }

        return new PegMessage(bytes);
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(PegMessage left, PegMessage right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(PegMessage left, PegMessage right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(PegMessage other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is PegMessage m && Equals(m);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <inheritdoc/>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();
}

/// <summary>A currency's hub accounts and current peg; an unset peg reads 0/0 at epoch 0.</summary>
/// <param name="IssuerAccount">The issuer's account.</param>
/// <param name="PoolAccount">The hub's pool of the currency.</param>
/// <param name="VBillFloatAccount">The currency's V-Bill float.</param>
/// <param name="PegNumerator">Minor units per <paramref name="PegDenominator"/> V-Bills.</param>
/// <param name="PegDenominator">V-Bills.</param>
/// <param name="PegEpoch">The peg's epoch.</param>
public readonly record struct VHubCurrencyInfo(uint IssuerAccount, uint PoolAccount, uint VBillFloatAccount, ulong PegNumerator, ulong PegDenominator, ulong PegEpoch)
{
    /// <summary>Gets a value indicating whether a peg has been set.</summary>
    public bool HasPeg => PegDenominator != 0;
}

/// <summary>An account's rails: A = L + E.</summary>
/// <param name="Debit">What it holds.</param>
/// <param name="Credit">What it owes (issuers only).</param>
/// <param name="Equity">Debit minus credit.</param>
public readonly record struct VHubBalance(ulong Debit, ulong Credit, long Equity);

/// <summary>A quote: the largest exact amount not above the request, and what it would carry.</summary>
/// <param name="Amount">Minor units of the sending currency.</param>
/// <param name="VBills">V-Bills it would move.</param>
/// <param name="Received">Minor units of the receiving currency.</param>
public readonly record struct VHubQuote(ulong Amount, ulong VBills, ulong Received);

/// <summary>What a swap moved.</summary>
/// <param name="VBills">V-Bills moved between the floats.</param>
/// <param name="Received">Minor units the receiver got.</param>
public readonly record struct VHubSwap(ulong VBills, ulong Received);

/// <summary>
/// A V-Bill hub (ABI 1.5, <c>zxv_vhub</c>): test currencies with one exact,
/// issuer-signed peg each against the V-Bill, and swaps A to V-Bill to B as
/// one balanced, all-or-nothing posting. Create one with
/// <see cref="ZxvPlatform.CreateVBillHub(Capability, ReadOnlySpan{byte})"/>.
/// </summary>
/// <remarks>
/// <para>
/// EXACT. A peg is num/den; both conversions in a swap must divide exactly
/// or it is refused (<see cref="ZxvStatus.AmountOutOfRange"/>). Nothing is
/// rounded and there is no floating-point rate anywhere. RULES: a currency
/// with no limit rule refuses every swap (fail closed,
/// <see cref="ZxvStatus.InvalidState"/>), so add a rule to each currency
/// first. Changes need <see cref="CapabilityRights.LedgerPost"/>; quotes,
/// balances and the check need <see cref="CapabilityRights.LedgerRead"/>.
/// Thread-safe.
/// </para>
/// <para>
/// HONEST LIMITS. A demonstration hub with test currencies only (ISO 4217
/// codes are refused) and no real money: its ledger lives in process memory,
/// separate from the platform's ledger, and is lost on dispose. It is
/// connected to no settlement rail, central bank or issuer. Peg signatures
/// use ML-DSA-87 (not FIPS 140-3 validated). V-Bills are likely securities in
/// many jurisdictions; real use needs legal review and a participating
/// issuer. An exact peg removes rounding, not exchange-rate risk.
/// </para>
/// </remarks>
public sealed class VBillHub : IDisposable
{
    /// <summary>Currency slots 0..7 (<c>ZXV_VHUB_MAX_CCY</c>).</summary>
    public const int MaxCurrencies = 8;

    /// <summary>Limit rules per currency (<c>ZXV_VHUB_MAX_RULES</c>).</summary>
    public const int MaxRules = 4;

    /// <summary>The largest single amount, 2^59 (<c>ZXV_VHUB_AMOUNT_MAX</c>).</summary>
    public const ulong MaxAmount = 1UL << 59;

    /// <summary>The V-Bill issuer's account (<c>ZXV_VHUB_VBILL_ISSUER</c>).</summary>
    public const uint VBillIssuerAccount = 0;

    private readonly Capability _cap;
    private readonly ZxvVHubHandle _handle;

    internal unsafe VBillHub(Capability capability, ReadOnlySpan<byte> seed)
    {
        if (!seed.IsEmpty)
        {
            NativeBuffers.RequireLength(seed, 32, nameof(seed));
        }

        _cap = capability;
        capability.Enter("VBillHub.Create");
        int rc;
        fixed (byte* s = seed)
        {
            rc = NativeMethods.VhubCreate(capability.Handle, seed.IsEmpty ? null : s, out _handle);
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "VBillHub.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>
    /// Registers test currency <paramref name="slot"/>: opens its issuer,
    /// pool and V-Bill float accounts and funds the pool and the float. The
    /// peg starts unset. A failure registers nothing.
    /// </summary>
    /// <param name="slot">0..7.</param>
    /// <param name="code">3..12 characters of A-Z and 0-9; not an ISO 4217 code, "VBL" or "VFV".</param>
    /// <param name="minorUnits">0..4.</param>
    /// <param name="issuerPublicKey">The issuer's ML-DSA-87 public key (2592 bytes); it alone may set the peg.</param>
    /// <param name="poolFunding">Units issued into the pool (may be 0).</param>
    /// <param name="vbillBacking">V-Bills issued into the float (may be 0).</param>
    /// <exception cref="ZxvUsageException">Bad code, slot or amount, an ISO 4217 code (<see cref="ZxvStatus.InvalidCurrency"/>), or the slot or code is taken (<see cref="ZxvStatus.Duplicate"/>).</exception>
    public unsafe void AddCurrency(byte slot, string code, byte minorUnits, ReadOnlySpan<byte> issuerPublicKey, ulong poolFunding, ulong vbillBacking)
    {
        byte[] c = NativeBuffers.Utf8Z(code, nameof(code));
        NativeBuffers.RequireLength(issuerPublicKey, MlDsa87.PublicKeyBytes, nameof(issuerPublicKey));
        Enter("VBillHub.AddCurrency");
        fixed (byte* pc = c)
        fixed (byte* pk = issuerPublicKey)
        {
            Check(NativeMethods.VhubAddCcy(_cap.Handle, _handle, slot, pc, minorUnits, pk, poolFunding, vbillBacking), "VBillHub.AddCurrency");
        }
    }

    /// <summary>A currency's accounts and peg. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="slot">The slot.</param>
    /// <returns>The information.</returns>
    /// <exception cref="ZxvUsageException">The slot is not registered (<see cref="ZxvStatus.NotFound"/>).</exception>
    public VHubCurrencyInfo GetCurrency(byte slot)
    {
        Enter("VBillHub.GetCurrency");
        Check(NativeMethods.VhubCcyInfo(_cap.Handle, _handle, slot, out uint i, out uint p, out uint f, out ulong n, out ulong d, out ulong e), "VBillHub.GetCurrency");
        return new VHubCurrencyInfo(i, p, f, n, d, e);
    }

    /// <summary>
    /// Sets a currency's peg to <paramref name="numerator"/>/<paramref name="denominator"/>
    /// at <paramref name="epoch"/>, given the issuer's signature over
    /// <see cref="PegMessage.Create"/> of the same numbers. A refused peg changes nothing.
    /// </summary>
    /// <param name="slot">The slot.</param>
    /// <param name="numerator">Minor units of the currency (not 0).</param>
    /// <param name="denominator">Per this many V-Bills (not 0).</param>
    /// <param name="epoch">Must be above the current epoch.</param>
    /// <param name="signature">The issuer's ML-DSA-87 signature (4627 bytes, empty context).</param>
    /// <exception cref="ZxvRefusedException">A stale or replayed epoch (<see cref="ZxvStatus.PegInvalid"/>).</exception>
    /// <exception cref="VerificationFailedException">Not the issuer's signature over these numbers.</exception>
    public unsafe void SetPeg(byte slot, ulong numerator, ulong denominator, ulong epoch, ReadOnlySpan<byte> signature)
    {
        NativeBuffers.RequireLength(signature, MlDsa87.SignatureBytes, nameof(signature));
        Enter("VBillHub.SetPeg");
        fixed (byte* s = signature)
        {
            Check(NativeMethods.VhubSetPeg(_cap.Handle, _handle, slot, numerator, denominator, epoch, s), "VBillHub.SetPeg");
        }
    }

    /// <summary>Opens a holder account in currency <paramref name="slot"/> for <paramref name="owner"/>.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="owner">An id of the caller's choosing.</param>
    /// <returns>The account number.</returns>
    public uint OpenAccount(byte slot, uint owner)
    {
        Enter("VBillHub.OpenAccount");
        Check(NativeMethods.VhubOpenAccount(_cap.Handle, _handle, slot, owner, out uint account), "VBillHub.OpenAccount");
        return account;
    }

    /// <summary>Test faucet: issues <paramref name="amount"/> to a holder account, a pool, or (V-Bills) a float.</summary>
    /// <param name="account">The account.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>.</param>
    public void Issue(uint account, ulong amount)
    {
        Enter("VBillHub.Issue");
        Check(NativeMethods.VhubIssue(_cap.Handle, _handle, account, amount), "VBillHub.Issue");
    }

    /// <summary>One account's rails. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="account">The account.</param>
    /// <returns>Debit, credit and equity.</returns>
    public VHubBalance GetBalance(uint account)
    {
        Enter("VBillHub.GetBalance");
        Check(NativeMethods.VhubBalance(_cap.Handle, _handle, account, out ulong d, out ulong c, out long e), "VBillHub.GetBalance");
        return new VHubBalance(d, c, e);
    }

    /// <summary>Adds the built-in limit rule to a currency: at most <paramref name="limit"/> minor units out per sender per rule epoch.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="limit">The limit.</param>
    /// <exception cref="ZxvException">Four rules already (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public void AddLimit(byte slot, ulong limit)
    {
        Enter("VBillHub.AddLimit");
        Check(NativeMethods.VhubAddLimit(_cap.Handle, _handle, slot, limit), "VBillHub.AddLimit");
    }

    /// <summary>Starts the next rule epoch: every limit starts every sender at zero.</summary>
    /// <returns>The new epoch.</returns>
    public ulong NextEpoch()
    {
        Enter("VBillHub.NextEpoch");
        Check(NativeMethods.VhubNextEpoch(_cap.Handle, _handle, out ulong epoch), "VBillHub.NextEpoch");
        return epoch;
    }

    /// <summary>
    /// The largest exact amount of <paramref name="fromSlot"/> not above
    /// <paramref name="want"/>, and what it would carry. Runs no rules and
    /// checks no balances, so a quoted swap may still be refused.
    /// </summary>
    /// <param name="fromSlot">The sending currency.</param>
    /// <param name="toSlot">The receiving currency.</param>
    /// <param name="want">The amount wanted.</param>
    /// <returns>The quote.</returns>
    /// <exception cref="ZxvRefusedException">Either peg is unset (<see cref="ZxvStatus.PegInvalid"/>).</exception>
    public VHubQuote Quote(byte fromSlot, byte toSlot, ulong want)
    {
        Enter("VBillHub.Quote");
        Check(NativeMethods.VhubQuote(_cap.Handle, _handle, fromSlot, toSlot, want, out ulong a, out ulong v, out ulong r), "VBillHub.Quote");
        return new VHubQuote(a, v, r);
    }

    /// <summary>
    /// Swaps <paramref name="amount"/> from <paramref name="sender"/> to
    /// <paramref name="receiver"/> as one six-line posting. A refused swap
    /// changes nothing.
    /// </summary>
    /// <param name="fromSlot">The sending currency.</param>
    /// <param name="toSlot">The receiving currency.</param>
    /// <param name="sender">The sender's holder account.</param>
    /// <param name="receiver">The receiver's holder account.</param>
    /// <param name="amount">Minor units of the sending currency.</param>
    /// <param name="tick">A caller-supplied time (never the wall clock).</param>
    /// <returns>What moved.</returns>
    /// <exception cref="ZxvRefusedException">A limit refused (<see cref="ZxvStatus.LimitExceeded"/>) or a peg is unset (<see cref="ZxvStatus.PegInvalid"/>).</exception>
    /// <exception cref="ZxvException">Not exact under the pegs (<see cref="ZxvStatus.AmountOutOfRange"/>), or a currency has no rule (<see cref="ZxvStatus.InvalidState"/>).</exception>
    /// <exception cref="InsufficientFundsException">The sender, a float or a pool would go below zero.</exception>
    public VHubSwap Swap(byte fromSlot, byte toSlot, uint sender, uint receiver, ulong amount, ulong tick)
    {
        Enter("VBillHub.Swap");
        Check(NativeMethods.VhubSwap(_cap.Handle, _handle, fromSlot, toSlot, sender, receiver, amount, tick, out ulong v, out ulong r), "VBillHub.Swap");
        return new VHubSwap(v, r);
    }

    /// <summary>
    /// Verifies the whole hub ledger (A = L + E per account, debits equal
    /// credits per asset, the posting hash chain). A failure poisons the
    /// platform (<see cref="ZxvFatalException"/>).
    /// </summary>
    /// <returns>The number of postings so far.</returns>
    public ulong Check()
    {
        Enter("VBillHub.Check");
        Check(NativeMethods.VhubCheck(_cap.Handle, _handle, out ulong postings), "VBillHub.Check");
        return postings;
    }

    /// <summary>Releases the hub and its ledger.</summary>
    public void Dispose() => _handle.Dispose();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }

    private void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, _cap.Context);
        }
    }
}
