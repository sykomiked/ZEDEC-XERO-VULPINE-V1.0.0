// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers;
using System.Runtime.InteropServices;
using System.Text;

namespace Zxv.Interop;

/// <summary>A native call that writes UTF-8 text into a caller-owned buffer (R4 of zxv_api.h).</summary>
internal unsafe delegate int NativeTextCall(byte* buffer, nuint capacity, out nuint length);

/// <summary>
/// Implements the two-call pattern of <c>zxv_api.h</c>: try a stack buffer,
/// and on <see cref="ZxvStatus.BufferTooSmall"/> retry with a pooled buffer of
/// exactly the reported length plus the NUL.
/// </summary>
internal static unsafe class NativeText
{
    private const int StackSize = 256;

    /// <summary>Calls <paramref name="call"/> and decodes its UTF-8 output.</summary>
    /// <returns>The native status; <paramref name="value"/> is set only on <see cref="ZxvStatus.Ok"/>.</returns>
    internal static int TryRead(NativeTextCall call, out string value)
    {
        byte* stack = stackalloc byte[StackSize];
        int rc = call(stack, StackSize, out nuint length);
        if (rc == (int)ZxvStatus.Ok)
        {
            value = Encoding.UTF8.GetString(stack, checked((int)length));
            return rc;
        }

        // The required length can change between calls only if another thread
        // mutates the source; retry a bounded number of times.
        for (int attempt = 0; rc == (int)ZxvStatus.BufferTooSmall && attempt < 4; attempt++)
        {
            int size = checked((int)length + 1);
            byte[] rented = ArrayPool<byte>.Shared.Rent(size);
            try
            {
                fixed (byte* p = rented)
                {
                    rc = call(p, (nuint)rented.Length, out length);
                    if (rc == (int)ZxvStatus.Ok)
                    {
                        value = Encoding.UTF8.GetString(p, checked((int)length));
                        return rc;
                    }
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(rented);
            }
        }

        value = string.Empty;
        return rc;
    }

    /// <summary>Thread-local detail text of the last failed native call on this thread.</summary>
    internal static string LastError()
    {
        return TryRead(NativeMethods.LastError, out string text) == (int)ZxvStatus.Ok ? text : string.Empty;
    }

    /// <summary>Static symbolic name of a status code, e.g. <c>ZXV_E_FUNDS</c>.</summary>
    internal static string StatusName(int status)
    {
        byte* p = NativeMethods.StatusName(status);
        return p == null ? "ZXV_E_UNKNOWN" : Marshal.PtrToStringUTF8((IntPtr)p) ?? "ZXV_E_UNKNOWN";
    }
}
