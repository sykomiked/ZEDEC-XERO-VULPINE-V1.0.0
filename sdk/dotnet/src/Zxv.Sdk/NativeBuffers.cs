// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// The byte and text forms of the two-call buffer pattern (R4 of zxv_api.h)
/// used by the ABI 1.5 groups, and NUL-terminated UTF-8 for strings in.
/// </summary>
internal static unsafe class NativeBuffers
{
    /// <summary>
    /// Reads a byte result ("R4 for bytes": no NUL). Asks for the length with
    /// no buffer first, then calls again with exactly that many bytes.
    /// </summary>
    /// <param name="call">The native call.</param>
    /// <param name="check">Throws the SDK exception for a failing status.</param>
    /// <returns>The bytes; empty when the result is empty.</returns>
    internal static byte[] ReadBytes(NativeTextCall call, Action<int> check)
    {
        int rc = call(null, 0, out nuint length);
        if (rc == (int)ZxvStatus.Ok)
        {
            return Array.Empty<byte>();
        }

        // The needed length can grow between calls only if another thread
        // changes the source; retry a bounded number of times.
        for (int attempt = 0; rc == (int)ZxvStatus.BufferTooSmall && attempt < 4; attempt++)
        {
            var buffer = new byte[Math.Max(checked((int)length), 1)];
            fixed (byte* p = buffer)
            {
                rc = call(p, (nuint)buffer.Length, out length);
            }

            if (rc == (int)ZxvStatus.Ok)
            {
                int n = checked((int)length);
                return n == buffer.Length ? buffer : buffer.AsSpan(0, n).ToArray();
            }
        }

        check(rc);
        throw new ZxvSystemException(ZxvStatus.Internal, "NativeBuffers.ReadBytes", "the native call kept asking for a larger buffer");
    }

    /// <summary>Reads a UTF-8 text result (R4: NUL-terminated).</summary>
    /// <param name="call">The native call.</param>
    /// <param name="check">Throws the SDK exception for a failing status.</param>
    /// <returns>The text.</returns>
    internal static string ReadText(NativeTextCall call, Action<int> check)
    {
        int rc = NativeText.TryRead(call, out string value);
        if (rc != (int)ZxvStatus.Ok)
        {
            check(rc);
        }

        return value;
    }

    /// <summary>NUL-terminated UTF-8 of <paramref name="value"/>, refusing an embedded NUL (which C would silently truncate).</summary>
    /// <param name="value">The text.</param>
    /// <param name="paramName">The parameter name for the exception.</param>
    /// <param name="allowEmpty">Whether an empty string is acceptable here.</param>
    /// <returns>The bytes, ending in one 0.</returns>
    internal static byte[] Utf8Z(string? value, string paramName, bool allowEmpty = false)
    {
        Guard.Text(value, paramName, allowEmpty);
        int n = Encoding.UTF8.GetByteCount(value!);
        var bytes = new byte[n + 1];
        Encoding.UTF8.GetBytes(value!, bytes);
        return bytes;
    }

    /// <summary>Decodes a NUL-terminated name from a fixed native array.</summary>
    /// <param name="bytes">The array.</param>
    /// <returns>The text before the first NUL.</returns>
    internal static string FromZ(ReadOnlySpan<byte> bytes)
    {
        int end = bytes.IndexOf((byte)0);
        return Encoding.UTF8.GetString(end < 0 ? bytes : bytes[..end]);
    }

    /// <summary>Throws <see cref="ArgumentException"/> unless <paramref name="value"/> is exactly <paramref name="length"/> bytes.</summary>
    /// <param name="value">The bytes.</param>
    /// <param name="length">The required length.</param>
    /// <param name="paramName">The parameter name.</param>
    internal static void RequireLength(ReadOnlySpan<byte> value, int length, string paramName)
    {
        if (value.Length != length)
        {
            throw new ArgumentException($"Must be exactly {length} bytes (got {value.Length}).", paramName);
        }
    }
}
