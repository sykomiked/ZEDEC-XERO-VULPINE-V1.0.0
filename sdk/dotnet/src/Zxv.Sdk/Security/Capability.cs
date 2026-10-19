// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading;
using Zxv.Interop;

namespace Zxv;

/// <summary>The rights a <see cref="Capability"/> can hold (<c>ZXV_CAP_*</c>).</summary>
[Flags]
#pragma warning disable CA1028 // ulong matches the C ABI's uint64_t bit set.
public enum CapabilityRights : ulong
#pragma warning restore CA1028
{
    /// <summary>No right.</summary>
    None = 0,

    /// <summary>Read balances and journals.</summary>
    LedgerRead = 1UL << 0,

    /// <summary>Post transfers (kernel <c>SB_CAP_CAPITAL_TRANSFER</c>).</summary>
    LedgerPost = 1UL << 1,

    /// <summary>Sign with ML-DSA-87 and Ed25519.</summary>
    CryptoSign = 1UL << 2,

    /// <summary>ML-KEM-1024 key generation, encapsulation and decapsulation.</summary>
    CryptoKem = 1UL << 3,

    /// <summary>Open models and generate.</summary>
    AiInference = 1UL << 4,

    /// <summary>Create and use rmag budgets.</summary>
    Budget = 1UL << 5,

    /// <summary>Admit, defer and advance on a phase clock.</summary>
    Phase = 1UL << 6,

    /// <summary>Record attestations and evidence.</summary>
    Attest = 1UL << 7,

    /// <summary>Use the swarm economy.</summary>
    Swarm = 1UL << 8,

    /// <summary>Encode EVM state-channel states.</summary>
    Evm = 1UL << 9,

    /// <summary>Every right defined by ABI 1.1 (<c>ZXV_CAP_ALL</c>, which keeps this value forever).</summary>
    All = (1UL << 10) - 1,

    /// <summary>Build and call the hardware emulation matrix (<c>ZXV_CAP_HW_MATRIX</c>, ABI 1.4).</summary>
    HwMatrix = 1UL << 10,

    /// <summary>Load hardware images into regions and apply contract packages (<c>ZXV_CAP_HW_RECONF</c>, ABI 1.4).</summary>
    HwReconf = 1UL << 11,

    /// <summary>Every right as of ABI 1.4 (<c>ZXV_CAP_ALL_1_4</c>).</summary>
    AllAbi14 = (1UL << 12) - 1,

    /// <summary>Create AIPI nodes, store blocks and publish function records (<c>ZXV_CAP_AIPI</c>, ABI 1.5).</summary>
    Aipi = 1UL << 12,

    /// <summary>Create token wallets and mint, spend, move or re-octane their tokens (<c>ZXV_CAP_MINT</c>, ABI 1.5).</summary>
    Mint = 1UL << 13,

    /// <summary>Create paradox books, policies, holds and graphs, and run the paradox operators (<c>ZXV_CAP_PARADOX</c>, ABI 1.5).</summary>
    Paradox = 1UL << 14,

    /// <summary>Every right as of ABI 1.5 (<c>ZXV_CAP_ALL_1_5</c>). Kept unchanged: the ABI only adds.</summary>
    AllAbi15 = (1UL << 15) - 1,

    /// <summary>Create and use a Vino play-lane sandbox and a daily spend meter (<c>ZXV_CAP_VINO</c>, ABI 1.6). Not needed to encode payment requests or verify receipts, which are pure.</summary>
    Vino = 1UL << 15,

    /// <summary>Every right as of ABI 1.6 (<c>ZXV_CAP_ALL_1_6</c>): <see cref="AllAbi15"/> plus <see cref="Vino"/>.</summary>
    AllAbi16 = (1UL << 16) - 1,
}

/// <summary>A snapshot of a capability's state.</summary>
/// <param name="Rights">The rights it holds.</param>
/// <param name="ProcessId">The process id recorded at creation.</param>
/// <param name="Attestation">Its attestation; anything but <see cref="Truth.True"/> blocks gated calls.</param>
/// <param name="IsRevoked">Whether it, or an ancestor, was revoked.</param>
public readonly record struct CapabilityInfo(CapabilityRights Rights, uint ProcessId, Truth Attestation, bool IsRevoked);

/// <summary>
/// A capability: the right to make a class of gated calls on one platform.
/// Every ABI 1.1 object that touches state, keys or the engine holds one and
/// presents it on every call; the native library checks, every time, that it
/// is live (not revoked, nor any ancestor), holds the needed right, and is
/// attested <see cref="Truth.True"/>.
/// </summary>
/// <remarks>
/// <para>
/// Create a root with <see cref="ZxvPlatform.CreateRootCapability"/> and hand
/// narrower children (<see cref="Attenuate(CapabilityRights)"/>) to the parts
/// of the program that need less. A capability can be narrowed, never
/// widened. There is no global "current capability".
/// </para>
/// <para>
/// HONEST LIMIT: inside one process this is least-privilege discipline, not a
/// sandbox; code in the same address space can create its own root. It
/// becomes enforcement at a process boundary.
/// </para>
/// <para>
/// A child keeps its parent's native object alive, so disposal order does not
/// matter for memory safety; dispose every capability you create.
/// </para>
/// </remarks>
public sealed class Capability : IDisposable
{
    private Capability(ZxvCapabilityHandle handle, ContextRef context)
    {
        Handle = handle;
        Context = context;
    }

    /// <summary>Gets the current rights.</summary>
    public CapabilityRights Rights => GetInfo().Rights;

    /// <summary>Gets the current attestation.</summary>
    public Truth Attestation => GetInfo().Attestation;

    /// <summary>Gets a value indicating whether this capability or an ancestor was revoked.</summary>
    public bool IsRevoked => GetInfo().IsRevoked;

    internal ZxvCapabilityHandle Handle { get; }

    // SafeHandle.IsClosed stays false while children hold a reference, so
    // disposal is tracked here to make this object observably disposed at once.
    private int _disposed;

    internal ContextRef Context { get; }

    /// <summary>Reads the capability's state.</summary>
    /// <returns>The snapshot.</returns>
    public CapabilityInfo GetInfo()
    {
        Enter("Capability.GetInfo");
        NativeCall.Check(NativeMethods.CapInfo(Handle, out ulong caps, out uint pid, out int att, out int revoked), "Capability.GetInfo", Context);
        return new CapabilityInfo((CapabilityRights)caps, pid, (Truth)att, revoked != 0);
    }

    /// <summary>
    /// Creates a child holding only the rights in <paramref name="keep"/> that
    /// this capability also holds. Revoking this capability revokes the child.
    /// </summary>
    /// <param name="keep">The rights to keep.</param>
    /// <returns>The narrower capability.</returns>
    public Capability Attenuate(CapabilityRights keep)
    {
        Enter("Capability.Attenuate");
        int rc = NativeMethods.CapAttenuate(Handle, (ulong)keep, out ZxvCapabilityHandle child);
        if (rc != (int)ZxvStatus.Ok)
        {
            child.Dispose();
            NativeCall.Check(rc, "Capability.Attenuate", Context);
        }

        child.AddDependency(Handle);
        return new Capability(child, Context);
    }

    /// <summary>Revokes this capability, and through it every descendant, permanently.</summary>
    public void Revoke()
    {
        Enter("Capability.Revoke");
        NativeCall.Check(NativeMethods.CapRevoke(Handle), "Capability.Revoke", Context);
    }

    /// <summary>
    /// Sets the attestation. Anything but <see cref="Truth.True"/> blocks every
    /// gated call; <see cref="Truth.Both"/> is reported as a contradictory attestation.
    /// </summary>
    /// <param name="attestation">The new attestation.</param>
    public void SetAttestation(Truth attestation)
    {
        Enter("Capability.SetAttestation");
        NativeCall.Check(NativeMethods.CapSetAttestation(Handle, (int)attestation), "Capability.SetAttestation", Context);
    }

    /// <summary>Returns whether a call needing <paramref name="needed"/> would pass the capability check now.</summary>
    /// <param name="needed">The rights a call needs.</param>
    /// <returns><see langword="true"/> when it would.</returns>
    public bool Allows(CapabilityRights needed)
    {
        Enter("Capability.Allows");
        int rc = NativeMethods.CapCheck(Handle, (ulong)needed);
        if (rc == (int)ZxvStatus.CapabilityDenied)
        {
            return false;
        }

        NativeCall.Check(rc, "Capability.Allows", Context);
        return true;
    }

    /// <summary>Throws unless a call needing <paramref name="needed"/> would pass the capability check now.</summary>
    /// <param name="needed">The rights a call needs.</param>
    /// <exception cref="CapabilityDeniedException">It would not; <see cref="ZxvException.Detail"/> says why.</exception>
    public void Demand(CapabilityRights needed)
    {
        Enter("Capability.Demand");
        NativeCall.Check(NativeMethods.CapCheck(Handle, (ulong)needed), "Capability.Demand", Context);
    }

    /// <summary>Releases this handle. Children stay usable; the native parent is freed after the last child.</summary>
    /// <remarks>This capability throws <see cref="ObjectDisposedException"/> at once, even while children still hold it.</remarks>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
        {
            Handle.Dispose();
        }
    }

    internal static Capability CreateRoot(ContextRef context, CapabilityRights rights, uint processId, ReadOnlySpan<byte> identity)
    {
        if (!identity.IsEmpty && identity.Length != 21)
        {
            throw new ArgumentException("An identity is exactly 21 bytes (168 bits), or empty for zeros.", nameof(identity));
        }

        context.ThrowIfDisposed();
        context.ThrowIfHalted("Capability.CreateRoot");
        int rc;
        ZxvCapabilityHandle handle;
        unsafe
        {
            fixed (byte* id = identity)
            {
                rc = NativeMethods.CapCreate(context.Handle, processId, identity.IsEmpty ? null : id, (ulong)rights, out handle);
            }
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            handle.Dispose();
            NativeCall.Check(rc, "Capability.CreateRoot", context);
        }

        handle.AddDependency(context.Handle);
        return new Capability(handle, context);
    }

    /// <summary>Fails fast in managed code when the handle is closed or the context is known to be halted.</summary>
    internal void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0 || Handle.IsClosed, this);
        Context.ThrowIfHalted(operation);
    }

    /// <summary>Checks a native status from a call made with this capability.</summary>
    internal void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, Context);
        }
    }
}
