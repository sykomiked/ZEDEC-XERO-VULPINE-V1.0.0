// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// The kernel's hash functions: SHA3-256 and SHAKE256 (FIPS 202) and
/// Ethereum's Keccak-256 (pre-FIPS padding). Pure functions; no capability needed.
/// </summary>
public static class Hashes
{
    /// <summary>The digest length of SHA3-256 and Keccak-256 in bytes.</summary>
    public const int DigestBytes = 32;

    /// <summary>Computes SHA3-256.</summary>
    /// <param name="data">The input.</param>
    /// <returns>The 32-byte digest.</returns>
    public static byte[] Sha3256(ReadOnlySpan<byte> data)
    {
        var digest = new byte[DigestBytes];
        Sha3256(data, digest);
        return digest;
    }

    /// <summary>Computes SHA3-256 into <paramref name="destination"/>.</summary>
    /// <param name="data">The input.</param>
    /// <param name="destination">At least 32 bytes.</param>
    public static unsafe void Sha3256(ReadOnlySpan<byte> data, Span<byte> destination)
    {
        RequireDigest(destination);
        fixed (byte* d = data)
        fixed (byte* o = destination)
        {
            NativeCall.Check(NativeMethods.Sha3_256(d, (nuint)data.Length, o), "Hashes.Sha3256");
        }
    }

    /// <summary>Computes Keccak-256 as Ethereum defines it (not SHA3-256).</summary>
    /// <param name="data">The input.</param>
    /// <returns>The 32-byte digest.</returns>
    public static byte[] Keccak256(ReadOnlySpan<byte> data)
    {
        var digest = new byte[DigestBytes];
        Keccak256(data, digest);
        return digest;
    }

    /// <summary>Computes Keccak-256 into <paramref name="destination"/>.</summary>
    /// <param name="data">The input.</param>
    /// <param name="destination">At least 32 bytes.</param>
    public static unsafe void Keccak256(ReadOnlySpan<byte> data, Span<byte> destination)
    {
        RequireDigest(destination);
        fixed (byte* d = data)
        fixed (byte* o = destination)
        {
            NativeCall.Check(NativeMethods.Keccak256(d, (nuint)data.Length, o), "Hashes.Keccak256");
        }
    }

    /// <summary>Computes SHAKE256 with an output of any length.</summary>
    /// <param name="data">The input.</param>
    /// <param name="outputLength">Bytes of output.</param>
    /// <returns>The output.</returns>
    public static byte[] Shake256(ReadOnlySpan<byte> data, int outputLength)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(outputLength);
        var output = new byte[outputLength];
        Shake256(data, output);
        return output;
    }

    /// <summary>Computes SHAKE256, filling all of <paramref name="destination"/>.</summary>
    /// <param name="data">The input.</param>
    /// <param name="destination">The output.</param>
    public static unsafe void Shake256(ReadOnlySpan<byte> data, Span<byte> destination)
    {
        if (destination.IsEmpty)
        {
            throw new ArgumentException("The destination must not be empty.", nameof(destination));
        }

        fixed (byte* d = data)
        fixed (byte* o = destination)
        {
            NativeCall.Check(NativeMethods.Shake256(d, (nuint)data.Length, o, (nuint)destination.Length), "Hashes.Shake256");
        }
    }

    private static void RequireDigest(Span<byte> destination)
    {
        if (destination.Length < DigestBytes)
        {
            throw new ArgumentException($"The destination must hold {DigestBytes} bytes.", nameof(destination));
        }
    }
}

/// <summary>The operating system's CSPRNG and a wipe the compiler cannot remove, as the native library provides them.</summary>
public static class SecureMemory
{
    /// <summary>Fills <paramref name="buffer"/> from the OS CSPRNG (getrandom / arc4random).</summary>
    /// <param name="buffer">The buffer.</param>
    public static unsafe void FillRandom(Span<byte> buffer)
    {
        if (buffer.IsEmpty)
        {
            return;
        }

        fixed (byte* p = buffer)
        {
            NativeCall.Check(NativeMethods.Random(p, (nuint)buffer.Length), "SecureMemory.FillRandom");
        }
    }

    /// <summary>Zeroes <paramref name="buffer"/> with <c>zxv_secure_zero</c>.</summary>
    /// <param name="buffer">The buffer.</param>
    public static unsafe void Zero(Span<byte> buffer)
    {
        if (buffer.IsEmpty)
        {
            return;
        }

        fixed (byte* p = buffer)
        {
            NativeCall.Check(NativeMethods.SecureZero(p, (nuint)buffer.Length), "SecureMemory.Zero");
        }
    }
}
