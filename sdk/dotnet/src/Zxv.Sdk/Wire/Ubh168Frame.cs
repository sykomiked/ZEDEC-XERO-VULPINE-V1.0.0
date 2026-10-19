// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using Zxv.Interop;

namespace Zxv.Wire;

/// <summary>How the W5 correlation resolver reads a frame (<c>ZXV_TRUTH_*</c>).</summary>
public enum WireTruth
{
    /// <summary>All rails silent.</summary>
    Unknown = 0,

    /// <summary>The S- witness runs parallel to S+.</summary>
    True = 1,

    /// <summary>A dominant opposed witness.</summary>
    False = 2,

    /// <summary>One side silent, or the idle pilot.</summary>
    Neutral = 3,

    /// <summary>A contradiction, held.</summary>
    Glut = 4,

    /// <summary>A persistent contradiction.</summary>
    Paradox = 5,
}

/// <summary>
/// A UBH-168 harmonic wire frame: 21 octets, a tag then five 32-bit words in
/// the fixed L-B-L-B-L byte order (W0 little-endian S+, W1 big-endian S-,
/// W2 LE S+, W3 BE S-, W4 LE S+). The octets are identical on every host.
/// </summary>
/// <remarks>
/// Packing and unpacking are pure managed code, so a service can frame
/// without the native library; the tests check them byte for byte against
/// <c>zxv_wire_pack</c> and <c>sdk/conformance/vectors.json</c>. Only
/// <see cref="Resolve"/> calls the native resolver.
/// </remarks>
public readonly record struct Ubh168Frame
{
    /// <summary>Frame length in bytes.</summary>
    public const int FrameBytes = 21;

    /// <summary>The 32-bit word every idle pilot frame carries ("1VXZ" read little-endian).</summary>
    public const uint IdleWord = 0x5A58_5631;

    private readonly uint _p0, _p1, _p2, _m0, _m1;

    /// <summary>Initializes a frame.</summary>
    /// <param name="tag">The tag: 100 * sync + 10 * shell + trunk (see <see cref="MakeTag"/>).</param>
    /// <param name="sPlus0">S+ word 0 (W0).</param>
    /// <param name="sPlus1">S+ word 1 (W2).</param>
    /// <param name="sPlus2">S+ word 2 (W4).</param>
    /// <param name="sMinus0">S- word 0 (W1).</param>
    /// <param name="sMinus1">S- word 1 (W3).</param>
    public Ubh168Frame(byte tag, uint sPlus0, uint sPlus1, uint sPlus2, uint sMinus0, uint sMinus1)
    {
        Tag = tag;
        _p0 = sPlus0;
        _p1 = sPlus1;
        _p2 = sPlus2;
        _m0 = sMinus0;
        _m1 = sMinus1;
    }

    /// <summary>Gets the tag octet.</summary>
    public byte Tag { get; }

    /// <summary>Gets the trunk (tag mod 10).</summary>
    public int Trunk => Tag % 10;

    /// <summary>Gets the shell ((tag / 10) mod 10).</summary>
    public int Shell => Tag / 10 % 10;

    /// <summary>Gets a value indicating whether the sync digit is set (tag &gt;= 100).</summary>
    public bool Sync => Tag >= 100;

    /// <summary>Gets the three S+ words.</summary>
    /// <param name="index">0, 1 or 2.</param>
    /// <returns>The word.</returns>
    public uint SPlus(int index) => index switch
    {
        0 => _p0,
        1 => _p1,
        2 => _p2,
        _ => throw new ArgumentOutOfRangeException(nameof(index)),
    };

    /// <summary>Gets the two S- words.</summary>
    /// <param name="index">0 or 1.</param>
    /// <returns>The word.</returns>
    public uint SMinus(int index) => index switch
    {
        0 => _m0,
        1 => _m1,
        _ => throw new ArgumentOutOfRangeException(nameof(index)),
    };

    /// <summary>Builds a tag: 100 * sync + 10 * shell + trunk.</summary>
    /// <param name="trunk">0..9.</param>
    /// <param name="shell">0..9.</param>
    /// <param name="sync">The sync digit.</param>
    /// <returns>The tag.</returns>
    public static byte MakeTag(int trunk, int shell, bool sync)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(trunk);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(trunk, 9);
        ArgumentOutOfRangeException.ThrowIfNegative(shell);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(shell, 9);
        return (byte)((sync ? 100 : 0) + (10 * shell) + trunk);
    }

    /// <summary>The idle pilot frame of a trunk (never all zeros): tag 100 + trunk, every word <see cref="IdleWord"/>.</summary>
    /// <param name="trunk">0..9.</param>
    /// <returns>The idle frame.</returns>
    public static Ubh168Frame Idle(int trunk)
    {
        byte tag = MakeTag(trunk, 0, sync: true);
        return new Ubh168Frame(tag, IdleWord, IdleWord, IdleWord, IdleWord, IdleWord);
    }

    /// <summary>Decodes 21 octets.</summary>
    /// <param name="frame">Exactly 21 bytes.</param>
    /// <returns>The frame.</returns>
    public static Ubh168Frame Unpack(ReadOnlySpan<byte> frame)
    {
        if (frame.Length != FrameBytes)
        {
            throw new ArgumentException($"A UBH-168 frame is {FrameBytes} bytes, got {frame.Length}.", nameof(frame));
        }

        return new Ubh168Frame(
            frame[0],
            BinaryPrimitives.ReadUInt32LittleEndian(frame[1..]),
            BinaryPrimitives.ReadUInt32LittleEndian(frame[9..]),
            BinaryPrimitives.ReadUInt32LittleEndian(frame[17..]),
            BinaryPrimitives.ReadUInt32BigEndian(frame[5..]),
            BinaryPrimitives.ReadUInt32BigEndian(frame[13..]));
    }

    /// <summary>Encodes into <paramref name="destination"/>.</summary>
    /// <param name="destination">At least 21 bytes.</param>
    public void WriteTo(Span<byte> destination)
    {
        if (destination.Length < FrameBytes)
        {
            throw new ArgumentException($"The destination must hold {FrameBytes} bytes.", nameof(destination));
        }

        destination[0] = Tag;
        BinaryPrimitives.WriteUInt32LittleEndian(destination[1..], _p0);
        BinaryPrimitives.WriteUInt32BigEndian(destination[5..], _m0);
        BinaryPrimitives.WriteUInt32LittleEndian(destination[9..], _p1);
        BinaryPrimitives.WriteUInt32BigEndian(destination[13..], _m1);
        BinaryPrimitives.WriteUInt32LittleEndian(destination[17..], _p2);
    }

    /// <summary>Encodes to a new 21-byte array.</summary>
    /// <returns>The octets.</returns>
    public byte[] Pack()
    {
        var bytes = new byte[FrameBytes];
        WriteTo(bytes);
        return bytes;
    }

    /// <summary>Runs the native W5 correlation resolver over this frame (words read as signed Q16.16).</summary>
    /// <returns>How the frame resolves.</returns>
    public unsafe WireTruth Resolve()
    {
        byte* f = stackalloc byte[FrameBytes];
        WriteTo(new Span<byte>(f, FrameBytes));
        NativeCall.Check(NativeMethods.WireResolve(f, out int truth), "Ubh168Frame.Resolve");
        return (WireTruth)truth;
    }

    /// <summary>Packs with the native library (<c>zxv_wire_pack</c>), for cross-checking the managed encoder.</summary>
    /// <returns>The octets the C library produces.</returns>
    public unsafe byte[] PackNative()
    {
        var bytes = new byte[FrameBytes];
        uint* sp = stackalloc uint[3] { _p0, _p1, _p2 };
        uint* sm = stackalloc uint[2] { _m0, _m1 };
        fixed (byte* o = bytes)
        {
            NativeCall.Check(NativeMethods.WirePack(Tag, sp, sm, o), "Ubh168Frame.PackNative");
        }

        return bytes;
    }

    /// <summary>Formats as lower-case hex of the 21 octets.</summary>
    /// <returns>The hex text.</returns>
    public override string ToString() => Convert.ToHexString(Pack()).ToLowerInvariant();
}
