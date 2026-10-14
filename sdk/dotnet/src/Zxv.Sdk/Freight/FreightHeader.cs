// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Freight;

/// <summary>Freight header flags (<c>ZXV_FREIGHT_F_*</c>).</summary>
[Flags]
public enum FreightOptions : byte
{
    /// <summary>A plain payload.</summary>
    None = 0,

    /// <summary>The (set's) payload is an op stream the receiver expands (<see cref="FreightOps"/>).</summary>
    FreightOps = 0x01,
}

/// <summary>
/// The 64-byte freight header (ABI 1.5): "ZXFH", version, k, flags, payload
/// length, set sequence and count, freight id, expand limit and the payload's
/// SHA-256. A checked value type: every instance either is <c>default</c>
/// (empty) or passed <c>zxv_freight_header_check</c>.
/// </summary>
/// <remarks>
/// HONEST LIMITS. The header is not protected by the erasure code and is not
/// authenticated: sign it, or the outer message, if the sender matters.
/// </remarks>
public readonly struct FreightHeader : IEquatable<FreightHeader>
{
    /// <summary>The serialized length (<c>ZXV_FREIGHT_HEADER_BYTES</c>).</summary>
    public const int Length = 64;

    private readonly byte[]? _bytes;

    private FreightHeader(byte[] bytes)
    {
        _bytes = bytes;
        Version = bytes[4];
        K = bytes[5];
        Flags = (FreightOptions)bytes[6];
        PayloadLength = (ushort)(bytes[8] | (bytes[9] << 8));
        Sequence = (ushort)(bytes[10] | (bytes[11] << 8));
        SequenceCount = (ushort)(bytes[12] | (bytes[13] << 8));
        FreightId = BitConverterLe(bytes, 16);
        ExpandLimit = BitConverterLe(bytes, 24);
    }

    /// <summary>Gets the serialized 64 bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets the format version (1).</summary>
    public byte Version { get; }

    /// <summary>Gets the number of data rows, 1..168: payload per freight is k * 20 bytes, and up to 168 - k rows may be lost.</summary>
    public byte K { get; }

    /// <summary>Gets the flags.</summary>
    public FreightOptions Flags { get; }

    /// <summary>Gets the payload length (at most k * 20).</summary>
    public ushort PayloadLength { get; }

    /// <summary>Gets this member's index in its set.</summary>
    public ushort Sequence { get; }

    /// <summary>Gets the number of freights in the set, 1..16384.</summary>
    public ushort SequenceCount { get; }

    /// <summary>Gets the freight (set) id.</summary>
    public ulong FreightId { get; }

    /// <summary>Gets the most bytes an op stream may expand to (0 unless <see cref="FreightOptions.FreightOps"/>).</summary>
    public ulong ExpandLimit { get; }

    /// <summary>Gets the payload's SHA-256 (32 bytes).</summary>
    public ReadOnlySpan<byte> PayloadSha256 => _bytes is null ? default : _bytes.AsSpan(32, 32);

    /// <summary>Builds the header of a single freight (seq 0 of 1) carrying <paramref name="payload"/>; it hashes the payload.</summary>
    /// <param name="freightId">The freight id.</param>
    /// <param name="k">Data rows, 1..168.</param>
    /// <param name="flags">The flags.</param>
    /// <param name="expandLimit">0, unless <paramref name="flags"/> has <see cref="FreightOptions.FreightOps"/>.</param>
    /// <param name="payload">At most k * 20 bytes.</param>
    /// <returns>The header.</returns>
    /// <exception cref="ZxvUsageException">Bad k, flags or limit (<see cref="ZxvStatus.InvalidArgument"/>), or the payload is too long (<see cref="ZxvStatus.TooLong"/>).</exception>
    public static unsafe FreightHeader Create(ulong freightId, byte k, FreightOptions flags, ulong expandLimit, ReadOnlySpan<byte> payload)
    {
        var bytes = new byte[Length];
        fixed (byte* p = payload)
        fixed (byte* h = bytes)
        {
            NativeCall.Check(NativeMethods.FreightHeaderInit(freightId, k, (byte)flags, expandLimit, p, (nuint)payload.Length, h), "FreightHeader.Create");
        }

        return new FreightHeader(bytes);
    }

    /// <summary>Serializes a header from its fields (for a set member, or one received field by field).</summary>
    /// <param name="k">Data rows, 1..168.</param>
    /// <param name="flags">The flags.</param>
    /// <param name="payloadLength">At most k * 20.</param>
    /// <param name="sequence">Below <paramref name="sequenceCount"/>.</param>
    /// <param name="sequenceCount">1..16384.</param>
    /// <param name="freightId">The freight id.</param>
    /// <param name="expandLimit">0 unless an op stream.</param>
    /// <param name="payloadSha256">32 bytes.</param>
    /// <returns>The header.</returns>
    /// <exception cref="ZxvUsageException">A field fails the check (<see cref="ZxvStatus.InvalidArgument"/>).</exception>
    public static unsafe FreightHeader FromFields(byte k, FreightOptions flags, ushort payloadLength, ushort sequence, ushort sequenceCount, ulong freightId, ulong expandLimit, ReadOnlySpan<byte> payloadSha256)
    {
        NativeBuffers.RequireLength(payloadSha256, 32, nameof(payloadSha256));
        var bytes = new byte[Length];
        fixed (byte* s = payloadSha256)
        fixed (byte* h = bytes)
        {
            NativeCall.Check(NativeMethods.FreightHeaderSerialize(k, (byte)flags, payloadLength, sequence, sequenceCount, freightId, expandLimit, s, h), "FreightHeader.FromFields");
        }

        return new FreightHeader(bytes);
    }

    /// <summary>Parses and checks a serialized header.</summary>
    /// <param name="header">64 bytes.</param>
    /// <returns>The header.</returns>
    /// <exception cref="ZxvUsageException">Bad magic, reserved bytes or a field (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe FreightHeader Parse(ReadOnlySpan<byte> header)
    {
        NativeBuffers.RequireLength(header, Length, nameof(header));
        fixed (byte* h = header)
        {
            NativeCall.Check(NativeMethods.FreightHeaderParse(h, out _, out _, out _, out _, out _, out _, out _, out _, null), "FreightHeader.Parse");
        }

        return new FreightHeader(header.ToArray());
    }

    /// <summary>Whether <paramref name="header"/> is a well-formed serialized header.</summary>
    /// <param name="header">The bytes.</param>
    /// <returns><see langword="true"/> when it passes the check.</returns>
    public static unsafe bool IsValid(ReadOnlySpan<byte> header)
    {
        if (header.Length != Length)
        {
            return false;
        }

        fixed (byte* h = header)
        {
            return NativeMethods.FreightHeaderCheck(h) == (int)ZxvStatus.Ok;
        }
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether the serialized bytes are equal.</returns>
    public static bool operator ==(FreightHeader left, FreightHeader right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(FreightHeader left, FreightHeader right) => !left.Equals(right);

    /// <summary>A copy of the 64 bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(FreightHeader other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is FreightHeader h && Equals(h);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <inheritdoc/>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();

    internal byte[] RequireBytes() => _bytes ?? throw new ArgumentException("The header is empty (default).");

    private static ulong BitConverterLe(byte[] b, int at)
    {
        ulong v = 0;
        for (int i = 7; i >= 0; i--)
        {
            v = (v << 8) | b[at + i];
        }

        return v;
    }
}
