// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv.Hardware;

/// <summary>
/// A reconfigurable region of ABI 1.4: hardware updates delivered as signed
/// software. An image (signed with ML-DSA-87 AND SLH-DSA-SHAKE-256s by
/// <see cref="HwPackageSigner"/>) is verified and loaded into the inactive
/// slot (<see cref="Stage"/>), switched to and proven against the matrix's
/// golden vectors (<see cref="Trial"/>), then made permanent
/// (<see cref="Commit"/>) or rolled back. Failures are quarantined (S0) and the
/// image is blocked forever. Every step raises <see cref="StepRecorded"/> and
/// appends a record to a hash-chained audit log (<see cref="GetLog"/>).
/// </summary>
/// <remarks>
/// Create one with <see cref="ZxvPlatform.CreateHwRegion"/>; every call needs
/// <see cref="CapabilityRights.HwReconf"/>. The device is reached only through
/// an <see cref="IHwRegionDevice"/>, called on the calling thread inside the
/// region call and kept alive until the region is released. Device exceptions
/// never unwind into native code: a <see cref="ZxvException"/> becomes its
/// status, anything else is rethrown from the call that ran it. Handler
/// exceptions of <see cref="StepRecorded"/> are traced and dropped. Thread-safe.
/// </remarks>
public sealed class HwRegion : IDisposable
{
    private readonly Capability _cap;
    private readonly ZxvHwRegionHandle _handle;

    internal unsafe HwRegion(Capability capability, HwMatrix? matrix, uint provider, uint regionId, uint targetClass, ReadOnlySpan<byte> targetDigest, IHwRegionDevice device, uint logCapacity)
    {
        ArgumentNullException.ThrowIfNull(device);
        if (!targetDigest.IsEmpty && targetDigest.Length != 32)
        {
            throw new ArgumentException("A target digest is 32 bytes, or empty for none.", nameof(targetDigest));
        }

        if (matrix is not null && !ReferenceEquals(matrix.Capability.Context, capability.Context))
        {
            throw new ArgumentException("The matrix belongs to a different platform.", nameof(matrix));
        }

        _cap = capability;
        capability.Enter("HwRegion.Create");
        matrix?.Enter("HwRegion.Create");
        HwCallbacks.Clear();

        // The device is reached through a GCHandle that the region handle
        // adopts on success and frees only after zxv_hw_region_destroy.
        GCHandle deviceHandle = GCHandle.Alloc(device);
        bool matrixRef = false;
        ZxvHwRegionHandle? handle = null;
        try
        {
            matrix?.Handle.DangerousAddRef(ref matrixRef);
            IntPtr mx = matrix is null ? IntPtr.Zero : matrix.Handle.DangerousGetHandle();
            int rc;
            fixed (byte* td = targetDigest)
            {
                rc = NativeMethods.HwRegionCreate(
                    capability.Handle,
                    mx,
                    provider,
                    regionId,
                    targetClass,
                    targetDigest.IsEmpty ? null : td,
                    &OnLoad,
                    &OnReadBack,
                    &OnActivate,
                    (void*)GCHandle.ToIntPtr(deviceHandle),
                    logCapacity,
                    out handle);
            }

            if (rc != (int)ZxvStatus.Ok)
            {
                handle.Dispose();
                handle = null;
                HwCallbacks.Check(rc, capability, "HwRegion.Create");
            }

            handle!.Adopt(deviceHandle);
        }
        catch
        {
            if (handle is null)
            {
                deviceHandle.Free();
            }

            throw;
        }
        finally
        {
            if (matrixRef)
            {
                matrix!.Handle.DangerousRelease();
            }
        }

        _handle = handle!;
        _handle.AddDependency(capability.Context.Handle);
        if (matrix is not null)
        {
            _handle.AddDependency(matrix.Handle);
        }

        // Events go to this object through a weak handle, so an undisposed
        // region can still be collected (its handle's finalizer then frees it).
        try
        {
            IntPtr self = _handle.Pin(this, GCHandleType.Weak);
            Enter("HwRegion.Create");
            Done(NativeMethods.HwRegionEvents(_cap.Handle, _handle, &OnEvent, (void*)self), "HwRegion.Create");
        }
        catch
        {
            _handle.Dispose();
            throw;
        }
    }

    /// <summary>Raised after each recorded step, once the region call has released its locks.</summary>
    public event EventHandler<HwRegionEventArgs>? StepRecorded;

    /// <summary>Trusts a release key (copied; at most 4 per region).</summary>
    /// <param name="publicKey"><see cref="HwPackageSigner.PublicKeyBytes"/> bytes.</param>
    public unsafe void TrustKey(ReadOnlySpan<byte> publicKey)
    {
        HwPackageSigner.RequireLength(publicKey, HwPackageSigner.PublicKeyBytes, nameof(publicKey));
        Enter("HwRegion.TrustKey");
        fixed (byte* p = publicKey)
        {
            Done(NativeMethods.HwRegionTrustKey(_cap.Handle, _handle, p), "HwRegion.TrustKey");
        }
    }

    /// <summary>
    /// Verifies an image and loads it into the inactive slot. Refusals
    /// (<see cref="ZxvStatus.VerificationFailed"/>, <see cref="ZxvStatus.Rollback"/>)
    /// are recorded; a read-back mismatch is recorded, quarantined and blocked
    /// (<see cref="ZxvStatus.Quarantined"/>).
    /// </summary>
    /// <param name="image">The signed image.</param>
    public unsafe void Stage(ReadOnlySpan<byte> image)
    {
        Enter("HwRegion.Stage");
        fixed (byte* p = image)
        {
            Done(NativeMethods.HwRegionStage(_cap.Handle, _handle, p, (nuint)image.Length), "HwRegion.Stage");
        }
    }

    /// <summary>Switches to the staged image and proves it; passing starts a probation of <paramref name="ticks"/>.</summary>
    /// <param name="ticks">The probation length.</param>
    /// <exception cref="ZxvRefusedException">It failed (<see cref="ZxvStatus.Quarantined"/>): the previous image, or S0, is back.</exception>
    public void Trial(uint ticks)
    {
        Enter("HwRegion.Trial");
        Done(NativeMethods.HwRegionTrial(_cap.Handle, _handle, ticks), "HwRegion.Trial");
    }

    /// <summary>Makes the tested image permanent and raises the floor.</summary>
    public void Commit()
    {
        Enter("HwRegion.Commit");
        Done(NativeMethods.HwRegionCommit(_cap.Handle, _handle), "HwRegion.Commit");
    }

    /// <summary>Rolls back to the previous image (or S0).</summary>
    public void Rollback()
    {
        Enter("HwRegion.Rollback");
        Done(NativeMethods.HwRegionRollback(_cap.Handle, _handle), "HwRegion.Rollback");
    }

    /// <summary>One health tick; an expiring probation rolls back (<see cref="ZxvStatus.Quarantined"/>).</summary>
    public void Tick()
    {
        Enter("HwRegion.Tick");
        Done(NativeMethods.HwRegionTick(_cap.Handle, _handle), "HwRegion.Tick");
    }

    /// <summary>Reads the region's state, version, floor and quarantine count.</summary>
    /// <returns>The info.</returns>
    public HwRegionInfo GetInfo()
    {
        Enter("HwRegion.GetInfo");
        Done(NativeMethods.HwRegionInfo(_cap.Handle, _handle, out int state, out uint version, out uint floor, out uint quarantined), "HwRegion.GetInfo");
        return new HwRegionInfo((HwRegionState)state, version, floor, quarantined);
    }

    /// <summary>Reads the audit log's head and length.</summary>
    /// <returns>The head.</returns>
    /// <exception cref="ZxvFatalException">The chain does not verify; the platform halts.</exception>
    public unsafe HwAuditHead GetLog()
    {
        Enter("HwRegion.GetLog");
        var head = new byte[32];
        uint count;
        fixed (byte* h = head)
        {
            Done(NativeMethods.HwRegionLog(_cap.Handle, _handle, h, out count), "HwRegion.GetLog");
        }

        return new HwAuditHead(head, count);
    }

    /// <summary>Reads audit record <paramref name="index"/> (0 = oldest).</summary>
    /// <param name="index">The record.</param>
    /// <returns>The record.</returns>
    public unsafe HwAuditRecord GetLogEntry(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter("HwRegion.GetLogEntry");
        byte[] subject = new byte[32], hash = new byte[32];
        int ev;
        uint reason, id, version, tag;
        fixed (byte* s = subject)
        fixed (byte* h = hash)
        {
            Done(NativeMethods.HwRegionLogEntry(_cap.Handle, _handle, (uint)index, out ev, out reason, out id, out version, out tag, s, h), "HwRegion.GetLogEntry");
        }

        return new HwAuditRecord((HwEvent)ev, (int)reason, id, version, Hexagram.FromTag((int)tag), subject, hash);
    }

    /// <summary>Releases the region and its callbacks.</summary>
    public void Dispose() => _handle.Dispose();

#pragma warning disable CA1031 // A device or handler exception must never unwind into native code.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnLoad(void* user, uint slot, byte* payload, nuint len)
    {
        try
        {
            Device(user).Load(slot, new ReadOnlySpan<byte>(payload, checked((int)len)));
            return (int)ZxvStatus.Ok;
        }
        catch (Exception ex)
        {
            return HwCallbacks.Fail(ex);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnReadBack(void* user, uint slot, byte* digest)
    {
        try
        {
            Device(user).ReadBack(slot, new Span<byte>(digest, 32));
            return (int)ZxvStatus.Ok;
        }
        catch (Exception ex)
        {
            return HwCallbacks.Fail(ex);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnActivate(void* user, uint slot)
    {
        try
        {
            Device(user).Activate(slot);
            return (int)ZxvStatus.Ok;
        }
        catch (Exception ex)
        {
            return HwCallbacks.Fail(ex);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe void OnEvent(void* user, uint regionId, int ev, uint reason, uint version)
    {
        try
        {
            if (GCHandle.FromIntPtr((IntPtr)user).Target is HwRegion self)
            {
                self.StepRecorded?.Invoke(self, new HwRegionEventArgs(regionId, (HwEvent)ev, (int)reason, version));
            }
        }
        catch (Exception ex)
        {
            Trace.TraceError("ZXV hardware region event handler threw: {0}", ex);
        }
    }
#pragma warning restore CA1031

    private static unsafe IHwRegionDevice Device(void* user) => (IHwRegionDevice)GCHandle.FromIntPtr((IntPtr)user).Target!;

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
        HwCallbacks.Clear();
    }

    private void Done(int rc, string operation) => HwCallbacks.Check(rc, _cap, operation);
}
