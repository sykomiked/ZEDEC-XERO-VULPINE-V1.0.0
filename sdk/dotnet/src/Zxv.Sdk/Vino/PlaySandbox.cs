// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Vino;

/// <summary>
/// A Vino play-lane sandbox (ABI 1.6, <c>zxv_vino_play</c>): issuers, holders
/// and Fibonacci-face vouchers in play units, living in process memory. Create
/// one with <see cref="ZxvPlatform.CreatePlaySandbox(Capability)"/>.
/// </summary>
/// <remarks>
/// <para>
/// HOW IT WORKS. Open issuer and holder accounts. An issuer posts play backing
/// (<see cref="PostBacking"/>) and may then issue vouchers to holders
/// (<see cref="Issue"/>) as long as its backing stays at least 1.8 times
/// everything it has outstanding (10 x backing &gt;= 18 x outstanding, exact
/// integers, no rounding); an issue or a withdrawal that would break this is
/// refused with <see cref="ZxvStatus.InsufficientCoverage"/>. Each voucher is
/// one coin with a Fibonacci ladder face, held by one holder and active on
/// exactly one rail, <see cref="VinoRail.Bearer"/> or
/// <see cref="VinoRail.Ghost"/>. A transfer must name the active rail (else
/// <see cref="ZxvStatus.WrongRail"/>), so a voucher can never be spent twice.
/// The holder redeems a voucher to its issuer (<see cref="Redeem"/>), which
/// retires it.
/// </para>
/// <para>
/// WHAT IT IS NOT. Play units are not money. They come from nowhere
/// (<see cref="PostBacking"/> is a faucet), are worth nothing, and are lost
/// when the sandbox is disposed. No call takes a play sandbox and anything
/// live, so play units can never be converted, redeemed for, or moved into
/// live value. Redeeming retires a voucher and pays nothing out to anyone.
/// There is no interest, yield or return of any kind.
/// </para>
/// <para>
/// CAPABILITY AND ERRORS. Every call needs <see cref="CapabilityRights.Vino"/>
/// from the capability the sandbox was created with
/// (<see cref="CapabilityDeniedException"/> otherwise). Caller mistakes (an
/// account of the wrong kind, a voucher not held by the named holder) raise
/// <see cref="ZxvUsageException"/>; refusals (<see cref="ZxvStatus.InsufficientCoverage"/>,
/// <see cref="ZxvStatus.WrongRail"/>, <see cref="ZxvStatus.InsufficientFunds"/>)
/// raise <see cref="ZxvRefusedException"/> or a subclass. A refused call
/// changes nothing. Once the platform halts, every call raises
/// <see cref="ZxvFatalException"/>. After <see cref="Dispose"/>, every call
/// raises <see cref="ObjectDisposedException"/>.
/// </para>
/// <para>
/// THREADING. Safe to use from several threads: libzxv serializes calls on
/// one sandbox with an internal lock.
/// </para>
/// </remarks>
public sealed class PlaySandbox : IDisposable
{
    /// <summary>The most accounts (issuers plus holders) one sandbox holds (<c>ZXV_VINO_MAX_ACCOUNTS</c>).</summary>
    public const int MaxAccounts = 256;

    /// <summary>The most active (unredeemed) vouchers one sandbox holds (<c>ZXV_VINO_MAX_VOUCHERS</c>).</summary>
    public const int MaxVouchers = 4096;

    /// <summary>The largest single amount of play units, 2^59 (<c>ZXV_VINO_AMOUNT_MAX</c>).</summary>
    public const ulong MaxAmount = 1UL << 59;

    private readonly Capability _cap;
    private readonly ZxvVinoPlayHandle _handle;

    internal PlaySandbox(Capability capability)
    {
        _cap = capability;
        capability.Enter("PlaySandbox.Create");
        int rc = NativeMethods.VinoPlayCreate(capability.Handle, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "PlaySandbox.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets a value indicating whether <see cref="Dispose"/> has been called.</summary>
    public bool IsDisposed => _handle.IsClosed;

    /// <summary>
    /// Opens an issuer account: one that posts play backing and issues
    /// vouchers. Account ids count up from 0, shared with holders.
    /// </summary>
    /// <param name="owner">An id of your choosing (for example a player or shop id); owners need not be distinct.</param>
    /// <returns>The new account id.</returns>
    /// <exception cref="ZxvException"><see cref="MaxAccounts"/> accounts are open (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public uint OpenIssuer(uint owner) => Open(owner, 1, "PlaySandbox.OpenIssuer");

    /// <summary>Opens a holder account: one that receives, spends and redeems vouchers. Account ids count up from 0, shared with issuers.</summary>
    /// <param name="owner">An id of your choosing (for example a player id); owners need not be distinct.</param>
    /// <returns>The new account id.</returns>
    /// <exception cref="ZxvException"><see cref="MaxAccounts"/> accounts are open (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public uint OpenHolder(uint owner) => Open(owner, 0, "PlaySandbox.OpenHolder");

    /// <summary>
    /// Posts <paramref name="amount"/> play units of backing to an issuer
    /// (<c>zxv_vino_play_back</c>). This is the play faucet: the units come
    /// from nowhere and are worth nothing.
    /// </summary>
    /// <param name="issuer">An issuer account.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>; the issuer's backing may not pass 2^62.</param>
    /// <exception cref="ZxvUsageException">Not an issuer (<see cref="ZxvStatus.InvalidArgument"/>), no such account (<see cref="ZxvStatus.NotFound"/>), or the amount is 0 or too large (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public void PostBacking(uint issuer, ulong amount)
    {
        Enter("PlaySandbox.PostBacking");
        Check(NativeMethods.VinoPlayBack(_cap.Handle, _handle, issuer, amount), "PlaySandbox.PostBacking");
    }

    /// <summary>
    /// Withdraws <paramref name="amount"/> of an issuer's backing
    /// (<c>zxv_vino_play_unback</c>), allowed only while the issuer stays at
    /// 1.8x coverage of what it has outstanding. A refusal changes nothing.
    /// </summary>
    /// <param name="issuer">An issuer account.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>.</param>
    /// <exception cref="ZxvRefusedException">The issuer would fall below 1.8x coverage (<see cref="ZxvStatus.InsufficientCoverage"/>).</exception>
    /// <exception cref="InsufficientFundsException">More than the issuer's backing.</exception>
    /// <exception cref="ZxvUsageException">As for <see cref="PostBacking"/>.</exception>
    public void WithdrawBacking(uint issuer, ulong amount)
    {
        Enter("PlaySandbox.WithdrawBacking");
        Check(NativeMethods.VinoPlayUnback(_cap.Handle, _handle, issuer, amount), "PlaySandbox.WithdrawBacking");
    }

    /// <summary>
    /// Issues one voucher of face <paramref name="face"/> from
    /// <paramref name="issuer"/> to <paramref name="holder"/>, active on
    /// <paramref name="rail"/>. The issuer's outstanding grows by the face.
    /// </summary>
    /// <param name="issuer">An issuer account.</param>
    /// <param name="holder">A holder account.</param>
    /// <param name="face">A Fibonacci ladder face in play units (1, 2, 3, 5, 8, 13, ...; see <see cref="Coins.Coin"/>).</param>
    /// <param name="rail">The rail the voucher starts on.</param>
    /// <returns>The new voucher's id (above 0, never reused in this sandbox).</returns>
    /// <exception cref="ZxvRefusedException">The issuer would fall below 1.8x coverage (<see cref="ZxvStatus.InsufficientCoverage"/>).</exception>
    /// <exception cref="ZxvUsageException">Not a ladder face (<see cref="ZxvStatus.Denomination"/>), <paramref name="issuer"/> is not an issuer or <paramref name="holder"/> not a holder, or a bad rail (<see cref="ZxvStatus.InvalidArgument"/>), or no such account (<see cref="ZxvStatus.NotFound"/>).</exception>
    /// <exception cref="ZxvException"><see cref="MaxVouchers"/> vouchers are active (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public ulong Issue(uint issuer, uint holder, ulong face, VinoRail rail)
    {
        Enter("PlaySandbox.Issue");
        Check(NativeMethods.VinoIssue(_cap.Handle, _handle, issuer, holder, face, (int)rail, out ulong id), "PlaySandbox.Issue");
        return id;
    }

    /// <summary>
    /// Spends an active voucher: moves it from holder <paramref name="from"/>
    /// to holder <paramref name="to"/> on <paramref name="rail"/>, which must
    /// be its active rail. The rail does not change.
    /// </summary>
    /// <param name="voucher">The voucher id.</param>
    /// <param name="from">The holder that holds it now.</param>
    /// <param name="to">Another holder account.</param>
    /// <param name="rail">The voucher's active rail.</param>
    /// <exception cref="ZxvRefusedException">Not its active rail (<see cref="ZxvStatus.WrongRail"/>).</exception>
    /// <exception cref="ZxvUsageException">Already redeemed (<see cref="ZxvStatus.InvalidState"/>); <paramref name="from"/> does not hold it, <paramref name="to"/> is not a holder, they are the same, or a bad rail (<see cref="ZxvStatus.InvalidArgument"/>); or no such voucher or account (<see cref="ZxvStatus.NotFound"/>).</exception>
    public void Transfer(ulong voucher, uint from, uint to, VinoRail rail)
    {
        Enter("PlaySandbox.Transfer");
        Check(NativeMethods.VinoTransfer(_cap.Handle, _handle, voucher, from, to, (int)rail), "PlaySandbox.Transfer");
    }

    /// <summary>Switches an active voucher, by its holder, to the other rail (bearer to ghost or back). Switching moves nothing.</summary>
    /// <param name="voucher">The voucher id.</param>
    /// <param name="holder">The holder that holds it.</param>
    /// <param name="rail">The rail to switch to.</param>
    /// <exception cref="ZxvRefusedException">It is already on <paramref name="rail"/> (<see cref="ZxvStatus.WrongRail"/>).</exception>
    /// <exception cref="ZxvUsageException">Already redeemed (<see cref="ZxvStatus.InvalidState"/>), <paramref name="holder"/> does not hold it or a bad rail (<see cref="ZxvStatus.InvalidArgument"/>), or no such voucher (<see cref="ZxvStatus.NotFound"/>).</exception>
    public void SetRail(ulong voucher, uint holder, VinoRail rail)
    {
        Enter("PlaySandbox.SetRail");
        Check(NativeMethods.VinoSetRail(_cap.Handle, _handle, voucher, holder, (int)rail), "PlaySandbox.SetRail");
    }

    /// <summary>
    /// Redeems an active voucher, by its holder, to its issuer: the voucher is
    /// retired and the issuer's outstanding falls by its face. Nothing is paid
    /// out to anyone, and the voucher can never move again.
    /// </summary>
    /// <param name="voucher">The voucher id.</param>
    /// <param name="holder">The holder that holds it.</param>
    /// <exception cref="ZxvUsageException">Already redeemed (<see cref="ZxvStatus.InvalidState"/>), <paramref name="holder"/> does not hold it (<see cref="ZxvStatus.InvalidArgument"/>), or no such voucher (<see cref="ZxvStatus.NotFound"/>).</exception>
    public void Redeem(ulong voucher, uint holder)
    {
        Enter("PlaySandbox.Redeem");
        Check(NativeMethods.VinoRedeem(_cap.Handle, _handle, voucher, holder), "PlaySandbox.Redeem");
    }

    /// <summary>Reads a voucher's face, issuer, holder, active rail and state. Redeemed vouchers stay readable by id.</summary>
    /// <param name="voucher">The voucher id.</param>
    /// <returns>The voucher.</returns>
    /// <exception cref="ZxvUsageException">No such voucher (<see cref="ZxvStatus.NotFound"/>).</exception>
    public VoucherInfo GetVoucher(ulong voucher)
    {
        Enter("PlaySandbox.GetVoucher");
        Check(NativeMethods.VinoVoucherInfo(_cap.Handle, _handle, voucher, out ulong face, out uint issuer, out uint holder, out int rail, out int state), "PlaySandbox.GetVoucher");
        return new VoucherInfo(voucher, face, issuer, holder, (VinoRail)rail, (VoucherState)state);
    }

    /// <summary>Reads an account's owner, kind and totals.</summary>
    /// <param name="account">The account id.</param>
    /// <returns>The account.</returns>
    /// <exception cref="ZxvUsageException">No such account (<see cref="ZxvStatus.NotFound"/>).</exception>
    public VinoAccount GetAccount(uint account)
    {
        Enter("PlaySandbox.GetAccount");
        Check(NativeMethods.VinoAccountInfo(_cap.Handle, _handle, account, out uint owner, out int isIssuer, out ulong backing, out ulong faces, out uint count), "PlaySandbox.GetAccount");
        return new VinoAccount(account, owner, isIssuer != 0, backing, faces, count);
    }

    /// <summary>Lists the ids of the active vouchers an account issued (issuer) or holds (holder), ascending. Redeemed vouchers are not listed.</summary>
    /// <param name="account">The account id.</param>
    /// <returns>The ids; empty when there are none.</returns>
    /// <exception cref="ZxvUsageException">No such account (<see cref="ZxvStatus.NotFound"/>).</exception>
    public unsafe ulong[] GetVouchers(uint account)
    {
        Enter("PlaySandbox.GetVouchers");

        // Two-call pattern: ask for the count, then fetch. Another thread may
        // issue in between, so retry a bounded number of times.
        int rc = NativeMethods.VinoVouchers(_cap.Handle, _handle, account, null, 0, out nuint count);
        for (int attempt = 0; rc == (int)ZxvStatus.BufferTooSmall && attempt < 4; attempt++)
        {
            var ids = new ulong[checked((int)count)];
            fixed (ulong* p = ids)
            {
                rc = NativeMethods.VinoVouchers(_cap.Handle, _handle, account, p, (nuint)ids.Length, out count);
            }

            if (rc == (int)ZxvStatus.Ok)
            {
                int n = checked((int)count);
                return n == ids.Length ? ids : ids.AsSpan(0, n).ToArray();
            }
        }

        if (rc == (int)ZxvStatus.Ok)
        {
            return Array.Empty<ulong>();
        }

        Check(rc, "PlaySandbox.GetVouchers");
        throw new ZxvSystemException(ZxvStatus.Internal, "PlaySandbox.GetVouchers", "the voucher list kept growing between calls");
    }

    /// <summary>
    /// Re-checks every invariant of the sandbox: each active voucher has a
    /// ladder face, one rail, an issuer and a holder; each issuer's
    /// outstanding equals the sum of its active faces and is covered 1.8x;
    /// each holder's total equals what it holds.
    /// </summary>
    /// <exception cref="ZxvFatalException">An invariant failed; the platform is halted.</exception>
    public void Check()
    {
        Enter("PlaySandbox.Check");
        Check(NativeMethods.VinoPlayCheck(_cap.Handle, _handle), "PlaySandbox.Check");
    }

    /// <summary>Releases the sandbox. Every play unit and voucher in it is gone. Safe to call more than once.</summary>
    public void Dispose() => _handle.Dispose();

    /// <summary>Opens an account with the raw <c>is_issuer</c> value (tests pass values other than 0 and 1).</summary>
    internal uint Open(uint owner, int isIssuer, string operation)
    {
        Enter(operation);
        Check(NativeMethods.VinoPlayOpen(_cap.Handle, _handle, owner, isIssuer, out uint account), operation);
        return account;
    }

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
