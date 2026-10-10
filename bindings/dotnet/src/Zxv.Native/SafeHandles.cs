// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>Owns a native <c>zxv_ctx*</c>. Released with <c>zxv_ctx_destroy</c>.</summary>
/// <remarks>
/// The marshaller adds a reference for the duration of every native call, so
/// disposing the handle on one thread while another thread is inside a call is
/// safe: the native object is destroyed after the last call returns.
/// </remarks>
public sealed class ZxvContextHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvContextHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.CtxDestroy(handle);
        return true;
    }
}

/// <summary>Owns a native <c>zxv_msg*</c> (one ISO 20022 message being built).</summary>
public sealed class ZxvMessageHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvMessageHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.MsgDestroy(handle);
        return true;
    }
}

/// <summary>Owns a native <c>zxv_netting*</c> (one netting cycle).</summary>
public sealed class ZxvNettingHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvNettingHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.NettingDestroy(handle);
        return true;
    }
}
