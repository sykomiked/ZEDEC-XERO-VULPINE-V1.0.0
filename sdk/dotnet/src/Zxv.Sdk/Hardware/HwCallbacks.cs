// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.ExceptionServices;
using Zxv.Interop;

namespace Zxv.Hardware;

/// <summary>
/// The failure channel of the hardware callbacks. A callback never lets an
/// exception unwind into native code: a <see cref="ZxvException"/> with a
/// specific status becomes that status, anything else becomes
/// <c>ZXV_E_INTERNAL</c> and is rethrown, as itself, from the native call
/// that ran the callback (callbacks run on the calling thread).
/// </summary>
internal static class HwCallbacks
{
    [ThreadStatic]
    private static Exception? _failure;

    /// <summary>Converts a callback exception to a status. Never throws.</summary>
    internal static int Fail(Exception error)
    {
        if (error is ZxvException z && (int)z.Status < 0 && z.Status != ZxvStatus.Internal)
        {
            return (int)z.Status;
        }

        _failure ??= error;
        return (int)ZxvStatus.Internal;
    }

    /// <summary>Clears the channel before a native call that may run callbacks.</summary>
    internal static void Clear() => _failure = null;

    /// <summary>Rethrows a callback failure if the call failed, then maps the status.</summary>
    internal static void Check(int rc, Capability capability, string operation)
    {
        Exception? failure = _failure;
        _failure = null;
        if (rc != (int)ZxvStatus.Ok && failure is not null)
        {
            ExceptionDispatchInfo.Capture(failure).Throw();
        }

        capability.Check(rc, operation);
    }
}
