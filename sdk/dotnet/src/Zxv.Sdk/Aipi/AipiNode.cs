// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Aipi;

/// <summary>An AIPI node's audit counters.</summary>
/// <param name="Blocks">Blocks indexed.</param>
/// <param name="StoreBytesUsed">Store bytes in use.</param>
/// <param name="VerifiedGets">GETs whose bytes matched their CID.</param>
/// <param name="ForbiddenGets">GETs whose stored bytes no longer matched their CID (those bytes were dropped).</param>
public readonly record struct AipiNodeInfo(uint Blocks, ulong StoreBytesUsed, uint VerifiedGets, uint ForbiddenGets);

/// <summary>
/// An offline, in-memory IPFS node with the AIPI context over it (ABI 1.5,
/// <c>zxv_aipi</c>): a block store with pins that names everything by
/// <see cref="Cid"/>. Create one with
/// <see cref="ZxvPlatform.CreateAipiNode(Capability, long, int, ReadOnlySpan{byte})"/>.
/// </summary>
/// <remarks>
/// <para>
/// Creating the node and storing (<see cref="Put"/>,
/// <see cref="PublishFunction(AipiFunction)"/>) need
/// <see cref="CapabilityRights.Aipi"/> from the capability the node was
/// created with; reads need only the node. Every GET re-hashes the bytes
/// before returning them. The node fails closed with its platform: once the
/// platform halts, every call raises <see cref="ZxvFatalException"/>.
/// Thread-safe (libzxv serializes calls with the node's own lock).
/// </para>
/// <para>
/// HONEST LIMITS. No network transport: no Vinea mesh, no IPFS peers, no
/// HTTP gateway, no libp2p, so a GET finds only what was put into this node.
/// No EXECUTE: running a function needs the Z-Wasm sandbox, which is not
/// part of libzxv; <see cref="Plan"/> only reports the route a call would
/// take. No money moves here. The optional node key (32 bytes) seals private
/// blocks with ChaCha20-Poly1305 and is wiped when the node is disposed.
/// </para>
/// </remarks>
public sealed class AipiNode : IDisposable
{
    /// <summary>The smallest store (<c>ZXV_AIPI_STORE_MIN</c>).</summary>
    public const long MinStoreBytes = 4096;

    /// <summary>The largest store, 256 MiB (<c>ZXV_AIPI_STORE_MAX</c>).</summary>
    public const long MaxStoreBytes = 268_435_456;

    /// <summary>The most blocks one node indexes (<c>ZXV_AIPI_BLOCKS_MAX</c>).</summary>
    public const int MaxBlocks = 65536;

    /// <summary>The largest single block, 2 MiB (<c>ZXV_AIPI_BLOCK_MAX</c>).</summary>
    public const int MaxBlockBytes = 2_097_152;

    private readonly Capability _cap;
    private readonly ZxvAipiHandle _handle;

    internal unsafe AipiNode(Capability capability, long storeBytes, int maxBlocks, ReadOnlySpan<byte> nodeKey)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(storeBytes);
        ArgumentOutOfRangeException.ThrowIfNegative(maxBlocks);
        if (!nodeKey.IsEmpty)
        {
            NativeBuffers.RequireLength(nodeKey, 32, nameof(nodeKey));
        }

        _cap = capability;
        capability.Enter("AipiNode.Create");
        int rc;
        fixed (byte* k = nodeKey)
        {
            rc = NativeMethods.AipiCreate(capability.Handle, (nuint)storeBytes, (uint)maxBlocks, nodeKey.IsEmpty ? null : k, out _handle);
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "AipiNode.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the audit counters.</summary>
    public AipiNodeInfo Info
    {
        get
        {
            Read("AipiNode.Info");
            Check(NativeMethods.AipiInfo(_handle, out uint blocks, out ulong used, out uint ok, out uint bad), "AipiNode.Info");
            return new AipiNodeInfo(blocks, used, ok, bad);
        }
    }

    /// <summary>
    /// Stores and pins <paramref name="data"/> as one raw block and returns
    /// its CID. Putting the same bytes again changes nothing. Needs
    /// <see cref="CapabilityRights.Aipi"/>.
    /// </summary>
    /// <param name="data">At most <see cref="MaxBlockBytes"/> bytes.</param>
    /// <param name="isPrivate">Store it sealed and never announceable (needs a node key).</param>
    /// <returns>The block's CID (CIDv1, raw, SHA2-256).</returns>
    /// <exception cref="ZxvRefusedException">
    /// Private without a node key, or the same bytes were stored with the other
    /// visibility (<see cref="ZxvStatus.InvalidState"/>): making something public
    /// is a deliberate act, not a side effect. Or the store is full
    /// (<see cref="ZxvStatus.CapacityExceeded"/>).
    /// </exception>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Aipi"/>.</exception>
    public unsafe Cid Put(ReadOnlySpan<byte> data, bool isPrivate = false)
    {
        Write("AipiNode.Put");
        byte* o = stackalloc byte[Cid.MaxBytes];
        fixed (byte* d = data)
        {
            Check(NativeMethods.AipiPut(_cap.Handle, _handle, d, (nuint)data.Length, isPrivate ? 1 : 0, o, out nuint n), "AipiNode.Put");
            return Cid.Wrap(o, n);
        }
    }

    /// <summary>AIPI GET: the bytes <paramref name="cid"/> names, re-hashed before they are returned.</summary>
    /// <param name="cid">The CID.</param>
    /// <returns>The bytes.</returns>
    /// <exception cref="ZxvUsageException">Not held here (<see cref="ZxvStatus.NotFound"/>: there is no network).</exception>
    /// <exception cref="ZxvRefusedException">The stored bytes no longer match (<see cref="ZxvStatus.CidMismatch"/>; they are dropped and counted as forbidden), an identity CID, or a private block without a node key (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe byte[] Get(Cid cid)
    {
        byte[] c = cid.ToArray();
        Read("AipiNode.Get");
        fixed (byte* pc = c)
        {
            byte* x = pc;
            return NativeBuffers.ReadBytes((byte* b, nuint cap, out nuint l) => NativeMethods.AipiGet(_handle, x, (nuint)c.Length, b, cap, out l), rc => Check(rc, "AipiNode.Get"));
        }
    }

    /// <summary>Like <see cref="Get"/>, but returns <see langword="false"/> when the block is not held here.</summary>
    /// <param name="cid">The CID.</param>
    /// <param name="data">The bytes, when found.</param>
    /// <returns>Whether the block is held here.</returns>
    public bool TryGet(Cid cid, out byte[] data)
    {
        try
        {
            data = Get(cid);
            return true;
        }
        catch (ZxvUsageException e) when (e.Status == ZxvStatus.NotFound)
        {
            data = Array.Empty<byte>();
            return false;
        }
    }

    /// <summary>
    /// Stores a function record (sealed when its privacy is
    /// <see cref="AipiPrivacy.Local"/>) and returns its function CID. The
    /// module and contract need not be held yet. Needs <see cref="CapabilityRights.Aipi"/>.
    /// </summary>
    /// <param name="function">The function.</param>
    /// <returns>Its function CID.</returns>
    public Cid PublishFunction(AipiFunction function)
    {
        ArgumentNullException.ThrowIfNull(function);
        return PublishFunction(function.Record);
    }

    /// <summary>Stores raw function record bytes (checked strictly) and returns the function CID. Needs <see cref="CapabilityRights.Aipi"/>.</summary>
    /// <param name="record">The record bytes.</param>
    /// <returns>Its function CID.</returns>
    /// <exception cref="ZxvUsageException">Not a valid record (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public unsafe Cid PublishFunction(ReadOnlySpan<byte> record)
    {
        Write("AipiNode.PublishFunction");
        byte* o = stackalloc byte[Cid.MaxBytes];
        fixed (byte* r = record)
        {
            Check(NativeMethods.AipiPublishFn(_cap.Handle, _handle, r, (nuint)record.Length, o, out nuint n), "AipiNode.PublishFunction");
            return Cid.Wrap(o, n);
        }
    }

    /// <summary>GETs a function record by function CID, verifies it by hash, decodes it strictly and checks its codec against its intent.</summary>
    /// <param name="functionCid">The function CID.</param>
    /// <returns>The function.</returns>
    /// <exception cref="ZxvRefusedException">Not a function CID, or codec and intent disagree (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    public unsafe AipiFunction LoadFunction(Cid functionCid)
    {
        byte[] c = functionCid.ToArray();
        Read("AipiNode.LoadFunction");
        fixed (byte* pc = c)
        {
            byte* x = pc;
            byte[] rec = NativeBuffers.ReadBytes((byte* b, nuint cap, out nuint l) => NativeMethods.AipiLoadFn(_handle, x, (nuint)c.Length, b, cap, out l), rc => Check(rc, "AipiNode.LoadFunction"));
            return AipiFunction.Decode(rec);
        }
    }

    /// <summary>
    /// The route a call of <paramref name="functionCid"/> on
    /// <paramref name="input"/> would take now. Runs and changes nothing.
    /// Offline, this is <see cref="AipiRoute.Local"/> when the module (and
    /// contract) are held, else <see cref="AipiRoute.Unavailable"/>.
    /// </summary>
    /// <param name="functionCid">The function CID.</param>
    /// <param name="input">At most <see cref="AipiCall.MaxInputBytes"/> bytes.</param>
    /// <param name="inputPrivate">Whether the input is private.</param>
    /// <returns>The route.</returns>
    public unsafe AipiRoute Plan(Cid functionCid, ReadOnlySpan<byte> input, bool inputPrivate = false)
    {
        byte[] c = functionCid.ToArray();
        Read("AipiNode.Plan");
        fixed (byte* pc = c)
        fixed (byte* pi = input)
        {
            Check(NativeMethods.AipiPlan(_handle, pc, (nuint)c.Length, pi, (nuint)input.Length, inputPrivate ? 1 : 0, out int route), "AipiNode.Plan");
            return (AipiRoute)route;
        }
    }

    /// <summary>Releases the node and wipes its node key.</summary>
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
