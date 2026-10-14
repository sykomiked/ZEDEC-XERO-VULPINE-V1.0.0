// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Coins;

/// <summary>A token wallet's label, octane, value and piece count.</summary>
/// <param name="ModelId">The free label it was created with.</param>
/// <param name="Octane">The densest tier it uses.</param>
/// <param name="Value">What it holds.</param>
/// <param name="Pieces">How many tokens that takes at its octane.</param>
public readonly record struct TokenWalletInfo(uint ModelId, int Octane, ulong Value, ulong Pieces);

/// <summary>
/// A token holding kept canonical at its own octane (ABI 1.5,
/// <c>zxv_token_wallet</c>). Create one with
/// <see cref="ZxvPlatform.CreateTokenWallet(Capability, uint, int)"/>.
/// </summary>
/// <remarks>
/// <para>
/// Every change (mint, spend, transfer, octane) needs
/// <see cref="CapabilityRights.Mint"/> from the capability the wallet was
/// created with. Reads need none. Thread-safe (libzxv serializes calls).
/// Once the platform halts, every call raises <see cref="ZxvFatalException"/>.
/// </para>
/// <para>
/// HONEST LIMITS. The wallet is not tied to a swarm budget: tokens enter it
/// only by <see cref="Mint"/>, which creates them. It is in process memory
/// and is lost when disposed. Density carries no premium.
/// </para>
/// </remarks>
public sealed class TokenWallet : IDisposable
{
    private readonly Capability _cap;
    private readonly ZxvTokenWalletHandle _handle;

    internal TokenWallet(Capability capability, uint modelId, int octane)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        _cap = capability;
        capability.Enter("TokenWallet.Create");
        int rc = NativeMethods.TokenWalletCreate(capability.Handle, modelId, (uint)octane, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "TokenWallet.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the wallet's label, octane, value and pieces.</summary>
    public TokenWalletInfo Info
    {
        get
        {
            Read("TokenWallet.Info");
            Check(NativeMethods.TokenWalletInfo(_handle, out uint model, out uint octane, out ulong value, out ulong pieces), "TokenWallet.Info");
            return new TokenWalletInfo(model, (int)octane, value, pieces);
        }
    }

    /// <summary>Gets the value held.</summary>
    public ulong Value => Info.Value;

    /// <summary>The counts per tier (<see cref="Token.Tiers"/> of them).</summary>
    /// <returns>A copy of the holding.</returns>
    public unsafe ulong[] GetCounts()
    {
        Read("TokenWallet.GetCounts");
        var counts = new ulong[Token.Tiers];
        fixed (ulong* c = counts)
        {
            Check(NativeMethods.TokenWalletCounts(_handle, c), "TokenWallet.GetCounts");
        }

        return counts;
    }

    /// <summary>Creates <paramref name="amount"/> new tokens in the wallet. Needs <see cref="CapabilityRights.Mint"/>.</summary>
    /// <param name="amount">The amount.</param>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Mint"/>.</exception>
    /// <exception cref="ZxvUsageException">The value would pass 64 bits (<see cref="ZxvStatus.AmountOutOfRange"/>; nothing changes).</exception>
    public void Mint(ulong amount)
    {
        Write("TokenWallet.Mint");
        Check(NativeMethods.TokenWalletMint(_cap.Handle, _handle, amount), "TokenWallet.Mint");
    }

    /// <summary>Spends (burns) <paramref name="amount"/>; the change stays canonical. Needs <see cref="CapabilityRights.Mint"/>.</summary>
    /// <param name="amount">The amount.</param>
    /// <exception cref="InsufficientFundsException">The wallet holds less (nothing changes).</exception>
    public void Spend(ulong amount)
    {
        Write("TokenWallet.Spend");
        Check(NativeMethods.TokenWalletSpend(_cap.Handle, _handle, amount), "TokenWallet.Spend");
    }

    /// <summary>
    /// Moves <paramref name="amount"/> to <paramref name="receiver"/>. This
    /// wallet keeps its change at its octane; the receiver gets the payment
    /// at ITS octane; the total is conserved exactly. Needs
    /// <see cref="CapabilityRights.Mint"/>, and both wallets must belong to
    /// the same platform.
    /// </summary>
    /// <param name="receiver">The receiving wallet (this wallet only checks the funds).</param>
    /// <param name="amount">The amount.</param>
    /// <exception cref="InsufficientFundsException">This wallet holds less (neither changes).</exception>
    /// <exception cref="CapabilityDeniedException">No MINT right, or the wallets belong to different platforms.</exception>
    public void TransferTo(TokenWallet receiver, ulong amount)
    {
        ArgumentNullException.ThrowIfNull(receiver);
        Write("TokenWallet.TransferTo");
        ObjectDisposedException.ThrowIf(receiver._handle.IsClosed, receiver);
        Check(NativeMethods.TokenWalletTransfer(_cap.Handle, _handle, receiver._handle, amount), "TokenWallet.TransferTo");
    }

    /// <summary>Changes the octane and re-expresses the same value canonically. Needs <see cref="CapabilityRights.Mint"/>.</summary>
    /// <param name="octane">0..<see cref="Octane.Max"/>.</param>
    public void SetOctane(int octane)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        Write("TokenWallet.SetOctane");
        Check(NativeMethods.TokenWalletSetOctane(_cap.Handle, _handle, (uint)octane), "TokenWallet.SetOctane");
    }

    /// <summary>Releases the wallet.</summary>
    public void Dispose() => _handle.Dispose();

    private void Write(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }

    private void Read(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Context.ThrowIfHalted(operation);
    }

    private void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, _cap.Context);
        }
    }
}
