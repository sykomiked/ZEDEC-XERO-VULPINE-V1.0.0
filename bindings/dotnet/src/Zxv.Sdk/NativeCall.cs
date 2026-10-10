// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>Turns native status codes into the SDK's exception model, in one place.</summary>
internal static class NativeCall
{
    /// <summary>Throws the matching exception when <paramref name="rc"/> is not <see cref="ZxvStatus.Ok"/>.</summary>
    internal static void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            throw Create(rc, operation);
        }
    }

    /// <summary>Creates (without throwing) the exception for a failed native call.</summary>
    internal static ZxvException Create(int rc, string operation)
    {
        string detail = NativeText.LastError();
        ZxvStatus status = (ZxvStatus)rc;
        return status == ZxvStatus.NotImplemented
            ? new ZxvFeatureNotAvailableException(operation, detail)
            : new ZxvException(status, operation, detail);
    }

    /// <summary>Reads native text and throws on failure.</summary>
    internal static string ReadText(NativeTextCall call, string operation)
    {
        int rc = NativeText.TryRead(call, out string value);
        Check(rc, operation);
        return value;
    }
}

/// <summary>Managed argument checks applied before crossing into native code.</summary>
internal static class Guard
{
    /// <summary>Rejects null, empty and strings with an embedded NUL (which C would silently truncate).</summary>
    internal static string Text(string? value, string paramName, bool allowEmpty = false)
    {
        ArgumentNullException.ThrowIfNull(value, paramName);
        if (!allowEmpty && value.Length == 0)
        {
            throw new ArgumentException("Value must not be empty.", paramName);
        }

        if (value.Contains('\0', StringComparison.Ordinal))
        {
            throw new ArgumentException("Value must not contain a NUL character.", paramName);
        }

        return value;
    }
}
