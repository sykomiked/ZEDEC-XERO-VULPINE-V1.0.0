// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Freight;

/// <summary>One member of a freight set: its header and its 168 packets.</summary>
/// <param name="Header">The header.</param>
/// <param name="Packets">The 3528 packet bytes (168 rows of 21, in order).</param>
public sealed record FreightMember(FreightHeader Header, byte[] Packets);

/// <summary>
/// Freight packets (ABI 1.5, <c>zxv_freight_*</c>): a payload of up to 3360
/// bytes as 168 erasure-coded rows of 21 bytes. Rows 0..k-1 hold the payload
/// verbatim and the rest are Reed-Solomon parity, so ANY k distinct rows
/// rebuild it. Pure: no context, no capability.
/// </summary>
/// <remarks>
/// HONEST LIMITS. Erasure coding only: a lost row is recovered; a CORRUPTED
/// row is detected (the payload SHA-256 fails, <see cref="VerificationFailedException"/>)
/// but not located or repaired, so drop rows you distrust and decode again.
/// A freight is larger than its payload (3528 wire bytes for at most 3360).
/// </remarks>
public static class FreightCodec
{
    /// <summary>Bytes per packet (<c>ZXV_FREIGHT_PACKET_BYTES</c>).</summary>
    public const int PacketBytes = 21;

    /// <summary>Payload bytes per row (<c>ZXV_FREIGHT_ROW_PAYLOAD</c>).</summary>
    public const int RowPayloadBytes = 20;

    /// <summary>Rows per freight (<c>ZXV_FREIGHT_ROWS</c>).</summary>
    public const int Rows = 168;

    /// <summary>One encoded freight, 168 * 21 bytes (<c>ZXV_FREIGHT_BYTES</c>).</summary>
    public const int FreightBytes = 3528;

    /// <summary>The largest payload, at k = 168 (<c>ZXV_FREIGHT_MAX_PAYLOAD</c>).</summary>
    public const int MaxPayloadBytes = 3360;

    /// <summary>Freights in one set (<c>ZXV_FREIGHT_MAX_SET</c>).</summary>
    public const int MaxSetCount = 16384;

    /// <summary>The largest set payload (<c>ZXV_FREIGHT_SET_MAX_BYTES</c>).</summary>
    public const long MaxSetBytes = 55_050_240;

    /// <summary>Encodes the 168 packets of the freight <paramref name="header"/> describes.</summary>
    /// <param name="header">The header built for <paramref name="payload"/>.</param>
    /// <param name="payload">Exactly the payload the header was built for.</param>
    /// <returns>3528 bytes: the rows in order.</returns>
    /// <exception cref="ZxvUsageException">A length mismatch (<see cref="ZxvStatus.InvalidArgument"/>).</exception>
    /// <exception cref="VerificationFailedException">The payload does not match the header's hash.</exception>
    public static unsafe byte[] Encode(FreightHeader header, ReadOnlySpan<byte> payload)
    {
        byte[] h = header.RequireBytes();
        var packets = new byte[FreightBytes];
        fixed (byte* ph = h)
        fixed (byte* pp = payload)
        fixed (byte* po = packets)
        {
            NativeCall.Check(NativeMethods.FreightEncode(ph, pp, (nuint)payload.Length, po), "FreightCodec.Encode");
        }

        return packets;
    }

    /// <summary>
    /// Rebuilds the payload from whatever packets arrived, in any order;
    /// duplicates and unknown rows are ignored and the first k distinct rows
    /// are used.
    /// </summary>
    /// <param name="header">The freight's header.</param>
    /// <param name="packets">Whole 21-byte packets.</param>
    /// <returns>The payload.</returns>
    /// <exception cref="ZxvRefusedException">Fewer than k distinct rows (<see cref="ZxvStatus.ErasureTooFewRows"/>).</exception>
    /// <exception cref="VerificationFailedException">The rebuilt payload fails the header's SHA-256 (a corrupted row, or another freight's header).</exception>
    public static unsafe byte[] Decode(FreightHeader header, ReadOnlySpan<byte> packets)
    {
        byte[] h = header.RequireBytes();
        fixed (byte* ph = h)
        fixed (byte* pp = packets)
        {
            byte* a = ph;
            byte* b = pp;
            nuint n = (nuint)packets.Length;
            return NativeBuffers.ReadBytes((byte* buf, nuint cap, out nuint len) => NativeMethods.FreightDecode(a, b, n, buf, cap, out len), rc => NativeCall.Check(rc, "FreightCodec.Decode"));
        }
    }

    /// <summary>The number of freights needed to carry <paramref name="length"/> bytes at <paramref name="k"/> (0 bytes still needs one).</summary>
    /// <param name="length">The data length.</param>
    /// <param name="k">Data rows, 1..168.</param>
    /// <returns>The set count.</returns>
    /// <exception cref="ZxvUsageException">More than <see cref="MaxSetCount"/> freights (<see cref="ZxvStatus.TooLong"/>).</exception>
    public static int SetCount(long length, byte k)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(length);
        NativeCall.Check(NativeMethods.FreightSetCount((ulong)length, k, out uint count), "FreightCodec.SetCount");
        return (int)count;
    }

    /// <summary>Builds member <paramref name="sequence"/> of the set carrying <paramref name="data"/>.</summary>
    /// <param name="freightId">The set's id.</param>
    /// <param name="k">Data rows, 1..168.</param>
    /// <param name="flags">The flags.</param>
    /// <param name="expandLimit">0 unless an op stream.</param>
    /// <param name="data">The whole set's data.</param>
    /// <param name="sequence">Below <see cref="SetCount"/>.</param>
    /// <returns>The member's header and packets.</returns>
    /// <exception cref="ZxvUsageException">The sequence is not below the set count (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe FreightMember Split(ulong freightId, byte k, FreightOptions flags, ulong expandLimit, ReadOnlySpan<byte> data, int sequence)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(sequence);
        var header = new byte[FreightHeader.Length];
        var packets = new byte[FreightBytes];
        fixed (byte* d = data)
        fixed (byte* h = header)
        fixed (byte* p = packets)
        {
            NativeCall.Check(NativeMethods.FreightSplit(freightId, k, (byte)flags, expandLimit, d, (nuint)data.Length, (uint)sequence, h, p), "FreightCodec.Split");
        }

        return new FreightMember(FreightHeader.Parse(header), packets);
    }
}

/// <summary>A join's progress.</summary>
/// <param name="Received">Members accepted so far.</param>
/// <param name="Count">Members in the set (0 until the first is accepted).</param>
/// <param name="IsComplete">Whether every member arrived.</param>
/// <param name="TotalBytes">The joined length once complete, else 0.</param>
public readonly record struct FreightJoinStatus(int Received, int Count, bool IsComplete, long TotalBytes);

/// <summary>
/// Reassembles one freight set, member by member and in any order
/// (<c>zxv_freight_join</c>). Holds only its own memory: no context. Calls
/// are serialized by a managed lock, so any thread may use it.
/// </summary>
public sealed class FreightJoin : IDisposable
{
    private readonly ZxvFreightJoinHandle _handle;
    private readonly object _gate = new();

    /// <summary>Creates a join with a buffer of <paramref name="capacity"/> bytes.</summary>
    /// <param name="capacity">0..<see cref="FreightCodec.MaxSetBytes"/>.</param>
    /// <exception cref="ZxvUsageException">The capacity is too large (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public FreightJoin(long capacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(capacity);
        int rc = NativeMethods.FreightJoinCreate((ulong)capacity, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "FreightJoin.Create");
        }
    }

    /// <summary>Gets the progress.</summary>
    public FreightJoinStatus Status
    {
        get
        {
            lock (_gate)
            {
                Enter();
                NativeCall.Check(NativeMethods.FreightJoinStatus(_handle, out uint r, out uint c, out int done, out ulong total), "FreightJoin.Status");
                return new FreightJoinStatus((int)r, (int)c, done != 0, (long)total);
            }
        }
    }

    /// <summary>
    /// Decodes one member into its place. A member already received changes
    /// nothing. A failed member is not recorded; add it again when more rows arrive.
    /// </summary>
    /// <param name="header">The member's header.</param>
    /// <param name="packets">Whole 21-byte packets that arrived.</param>
    /// <exception cref="ZxvException">The member belongs to another set (<see cref="ZxvStatus.InvalidState"/>), or the set does not fit (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    /// <exception cref="ZxvRefusedException">Too few rows (<see cref="ZxvStatus.ErasureTooFewRows"/>).</exception>
    public unsafe void Add(FreightHeader header, ReadOnlySpan<byte> packets)
    {
        byte[] h = header.RequireBytes();
        lock (_gate)
        {
            Enter();
            fixed (byte* ph = h)
            fixed (byte* pp = packets)
            {
                NativeCall.Check(NativeMethods.FreightJoinAdd(_handle, ph, pp, (nuint)packets.Length), "FreightJoin.Add");
            }
        }
    }

    /// <summary>The joined bytes.</summary>
    /// <returns>The set's data.</returns>
    /// <exception cref="ZxvException">Not every member has arrived (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe byte[] GetResult()
    {
        lock (_gate)
        {
            Enter();
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.FreightJoinResult(_handle, b, c, out l), rc => NativeCall.Check(rc, "FreightJoin.GetResult"));
        }
    }

    /// <summary>Releases the join and its buffer.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
