// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Hardware;

/// <summary>
/// A ZHP-1 machine profile, the "hardware ROM" of ABI 1.4: a canonical tree of
/// abstract components (<see cref="HwClasses"/>) with integer capability
/// descriptors (<see cref="HwCapKeys"/>) and an opaque private extension per
/// node. It has one encoding and one SHA-256 identity (<see cref="Digest"/>).
/// </summary>
/// <remarks>
/// <see cref="ProbeHost"/> describes the machine you are on (only what the OS
/// reports reliably), <see cref="Reference"/> is the public VULPINE-class
/// reference machine R1 that ZXV software targets, and
/// <see cref="Present(HwProfile, HwProfile)"/> says what serves each R1
/// component on a host. Profiles are plain data: no platform or capability.
/// </remarks>
public sealed class HwProfile : IDisposable
{
    /// <summary>The parent id of a root node (<c>ZXV_HW_ROOT</c>).</summary>
    public const uint Root = 0xFFFF;

    /// <summary>The most private extension bytes per node (<c>ZXV_HW_EXT_MAX</c>).</summary>
    public const int ExtensionMax = 256;

    private readonly ZxvHwProfileHandle _handle;

    private HwProfile(ZxvHwProfileHandle handle) => _handle = handle;

    /// <summary>Gets the number of nodes.</summary>
    public int NodeCount
    {
        get
        {
            Enter();
            Check(NativeMethods.HwProfileCount(_handle, out uint n), "HwProfile.NodeCount");
            return checked((int)n);
        }
    }

    /// <summary>Gets every node, in order.</summary>
    public IReadOnlyList<HwNode> Nodes
    {
        get
        {
            int n = NodeCount;
            var nodes = new HwNode[n];
            for (int i = 0; i < n; i++)
            {
                nodes[i] = GetNode(i);
            }

            return nodes;
        }
    }

    /// <summary>Creates an empty profile to build with <see cref="AddNode"/>.</summary>
    /// <returns>The profile; dispose it when done.</returns>
    public static HwProfile Create() => Wrap(NativeMethods.HwProfileCreate(out ZxvHwProfileHandle h), h, "HwProfile.Create");

    /// <summary>Describes this machine: CPU cluster (ISA, cores, SIMD), memory, cache line. What cannot be detected portably is left out and will be emulated.</summary>
    /// <returns>The profile; dispose it when done.</returns>
    public static HwProfile ProbeHost() => Wrap(NativeMethods.HwProfileProbeHost(out ZxvHwProfileHandle h), h, "HwProfile.ProbeHost");

    /// <summary>A copy of the public VULPINE-class reference machine R1.</summary>
    /// <returns>The profile; dispose it when done.</returns>
    public static HwProfile Reference() => Wrap(NativeMethods.HwProfileReference(out ZxvHwProfileHandle h), h, "HwProfile.Reference");

    /// <summary>Parses a canonical ZHP-1 encoding.</summary>
    /// <param name="encoding">The bytes.</param>
    /// <returns>The profile; dispose it when done.</returns>
    /// <exception cref="ZxvUsageException">Anything non-canonical (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe HwProfile Decode(ReadOnlySpan<byte> encoding)
    {
        fixed (byte* p = encoding)
        {
            return Wrap(NativeMethods.HwProfileDecode(p, (nuint)encoding.Length, out ZxvHwProfileHandle h), h, "HwProfile.Decode");
        }
    }

    /// <summary>
    /// The target as served on the host: a copy of <paramref name="target"/> in
    /// which every node that runs something carries <see cref="HwCapKeys.Kind"/>
    /// (<see cref="HwKind.Hard"/> where the host has it, <see cref="HwKind.Soft"/> where it is emulated).
    /// </summary>
    /// <param name="host">The host profile.</param>
    /// <param name="target">The target profile.</param>
    /// <returns>The presented profile; dispose it when done.</returns>
    public static HwProfile Present(HwProfile host, HwProfile target)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(target);
        host.Enter();
        target.Enter();
        return Wrap(NativeMethods.HwProfilePresent(host._handle, target._handle, out ZxvHwProfileHandle h), h, "HwProfile.Present");
    }

    /// <summary>Counts the target components a host runs natively and those the matrix must emulate.</summary>
    /// <param name="host">The host profile.</param>
    /// <param name="target">The target profile.</param>
    /// <returns>The counts.</returns>
    public static HwMatch Match(HwProfile host, HwProfile target)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(target);
        host.Enter();
        target.Enter();
        Check(NativeMethods.HwProfileMatch(host._handle, target._handle, out uint n, out uint e), "HwProfile.Match");
        return new HwMatch(n, e);
    }

    /// <summary>The pairing of target node <paramref name="index"/> (in target order).</summary>
    /// <param name="host">The host profile.</param>
    /// <param name="target">The target profile.</param>
    /// <param name="index">The target node index.</param>
    /// <returns>The pairing.</returns>
    /// <exception cref="ZxvUsageException">Past the last node (<see cref="ZxvStatus.NotFound"/>).</exception>
    public static HwPairing Pair(HwProfile host, HwProfile target, int index)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(target);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        host.Enter();
        target.Enter();
        Check(NativeMethods.HwProfilePair(host._handle, target._handle, (uint)index, out uint need, out int kind, out uint have, out uint unmet), "HwProfile.Pair");
        return new HwPairing(need, (HwPairKind)kind, have, unmet);
    }

    /// <summary>The canonical ZHP-1 encoding.</summary>
    /// <returns>The bytes.</returns>
    public unsafe byte[] Encode()
    {
        Enter();
        int rc = NativeMethods.HwProfileEncode(_handle, null, 0, out nuint n);
        if (rc != (int)ZxvStatus.BufferTooSmall)
        {
            Check(rc, "HwProfile.Encode");
        }

        var buf = new byte[checked((int)n)];
        fixed (byte* p = buf)
        {
            Check(NativeMethods.HwProfileEncode(_handle, p, n, out n), "HwProfile.Encode");
        }

        return buf.AsSpan(0, checked((int)n)).ToArray();
    }

    /// <summary>The SHA-256 of the canonical encoding: the profile's identity.</summary>
    /// <returns>32 bytes.</returns>
    public unsafe byte[] Digest()
    {
        Enter();
        var d = new byte[32];
        fixed (byte* p = d)
        {
            Check(NativeMethods.HwProfileDigest(_handle, p), "HwProfile.Digest");
        }

        return d;
    }

    /// <summary>Adds a node: ids strictly increase, parent first (or <see cref="Root"/>).</summary>
    /// <param name="id">The node id.</param>
    /// <param name="parent">The parent id, or <see cref="Root"/>.</param>
    /// <param name="componentClass">Its class (<see cref="HwClasses"/>).</param>
    public void AddNode(uint id, uint parent, uint componentClass)
    {
        Enter();
        Check(NativeMethods.HwProfileNodeAdd(_handle, id, parent, componentClass), "HwProfile.AddNode");
    }

    /// <summary>Sets a node's capability.</summary>
    /// <param name="nodeId">The node.</param>
    /// <param name="key">The key (<see cref="HwCapKeys"/>).</param>
    /// <param name="value">The value.</param>
    public void SetCap(uint nodeId, uint key, ulong value)
    {
        Enter();
        Check(NativeMethods.HwProfileCapSet(_handle, nodeId, key, value), "HwProfile.SetCap");
    }

    /// <summary>Reads a node's capability.</summary>
    /// <param name="nodeId">The node.</param>
    /// <param name="key">The key (<see cref="HwCapKeys"/>).</param>
    /// <param name="value">The value, when present.</param>
    /// <returns><see langword="true"/> if the node has the capability.</returns>
    public bool TryGetCap(uint nodeId, uint key, out ulong value)
    {
        Enter();
        int rc = NativeMethods.HwProfileCapGet(_handle, nodeId, key, out value);
        if (rc == (int)ZxvStatus.NotFound)
        {
            value = 0;
            return false;
        }

        Check(rc, "HwProfile.TryGetCap");
        return true;
    }

    /// <summary>Sets a node's private extension: opaque bytes ZXV carries, encodes and digests but never interprets.</summary>
    /// <param name="nodeId">The node.</param>
    /// <param name="extension">At most <see cref="ExtensionMax"/> bytes.</param>
    public unsafe void SetExtension(uint nodeId, ReadOnlySpan<byte> extension)
    {
        Enter();
        fixed (byte* p = extension)
        {
            Check(NativeMethods.HwProfileExtSet(_handle, nodeId, p, (nuint)extension.Length), "HwProfile.SetExtension");
        }
    }

    /// <summary>Reads a node's private extension.</summary>
    /// <param name="nodeId">The node.</param>
    /// <returns>The bytes (empty if none).</returns>
    public unsafe byte[] GetExtension(uint nodeId)
    {
        Enter();
        byte* scratch = stackalloc byte[ExtensionMax];
        Check(NativeMethods.HwProfileExtGet(_handle, nodeId, scratch, ExtensionMax, out nuint n), "HwProfile.GetExtension");
        return new ReadOnlySpan<byte>(scratch, checked((int)n)).ToArray();
    }

    /// <summary>Reads node <paramref name="index"/>.</summary>
    /// <param name="index">0 .. <see cref="NodeCount"/> - 1.</param>
    /// <returns>The node.</returns>
    public HwNode GetNode(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter();
        Check(NativeMethods.HwProfileNode(_handle, (uint)index, out uint id, out uint parent, out uint cls, out uint n), "HwProfile.GetNode");
        return new HwNode(id, parent, cls, n);
    }

    /// <summary>Releases the profile.</summary>
    public void Dispose() => _handle.Dispose();

    private static HwProfile Wrap(int rc, ZxvHwProfileHandle handle, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            handle.Dispose();
            Check(rc, operation);
        }

        return new HwProfile(handle);
    }

    private static void Check(int rc, string operation) => NativeCall.Check(rc, operation);

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
