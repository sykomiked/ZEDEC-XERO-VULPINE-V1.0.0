// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Vino;

/// <summary>
/// One device's daily spend counter (ABI 1.6, <c>zxv_vino_meter</c>), for a
/// deployment that sets a per-device daily cap. Create one with
/// <see cref="ZxvPlatform.CreateSpendMeter(Capability, ulong)"/>.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="Admit"/> counts an amount against a day index (for example Unix
/// time / 86400) and refuses an amount that would take the day's total past
/// the cap; a refused amount is not counted. A later day starts a fresh
/// count; an earlier day than the last one is refused as the clock going
/// back.
/// </para>
/// <para>
/// HONEST LIMITS. This is a counter, not a legal check: it makes no exemption
/// or compliance claim, and it lives in process memory (a restart forgets
/// the day's total). Every call needs <see cref="CapabilityRights.Vino"/>.
/// Safe to use from several threads.
/// </para>
/// </remarks>
public sealed class SpendMeter : IDisposable
{
    /// <summary>The largest daily cap and single amount, 2^59 (<c>ZXV_VINO_AMOUNT_MAX</c>).</summary>
    public const ulong MaxAmount = 1UL << 59;

    private readonly Capability _cap;
    private readonly ZxvVinoMeterHandle _handle;

    internal SpendMeter(Capability capability, ulong dailyCap)
    {
        _cap = capability;
        capability.Enter("SpendMeter.Create");
        int rc = NativeMethods.VinoMeterCreate(capability.Handle, dailyCap, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "SpendMeter.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
        DailyCap = dailyCap;
    }

    /// <summary>Gets the cap on each day's total.</summary>
    public ulong DailyCap { get; }

    /// <summary>Counts <paramref name="amount"/> against <paramref name="day"/>.</summary>
    /// <param name="day">The day index (for example Unix time / 86400); not below the last day counted.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>.</param>
    /// <returns>What is left of the cap for that day after counting.</returns>
    /// <exception cref="ZxvRefusedException">The day's total would pass the cap (<see cref="ZxvStatus.LimitExceeded"/>); nothing was counted. Use <see cref="TryAdmit"/> to learn what is left instead.</exception>
    /// <exception cref="ZxvUsageException">The day is before the last one, or the amount is 0 or too large (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public ulong Admit(ulong day, ulong amount)
    {
        Enter("SpendMeter.Admit");
        Check(NativeMethods.VinoMeterAdmit(_cap.Handle, _handle, day, amount, out ulong left), "SpendMeter.Admit");
        return left;
    }

    /// <summary>Counts <paramref name="amount"/> against <paramref name="day"/> if it fits under the cap.</summary>
    /// <param name="day">The day index; not below the last day counted.</param>
    /// <param name="amount">1..<see cref="MaxAmount"/>.</param>
    /// <param name="left">What is left of the cap for that day after the call (whether or not it was counted).</param>
    /// <returns><see langword="true"/> if counted; <see langword="false"/> if it would pass the cap (nothing counted).</returns>
    /// <exception cref="ZxvUsageException">The day is before the last one, or the amount is 0 or too large (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public bool TryAdmit(ulong day, ulong amount, out ulong left)
    {
        Enter("SpendMeter.TryAdmit");
        int rc = NativeMethods.VinoMeterAdmit(_cap.Handle, _handle, day, amount, out left);
        if (rc == (int)ZxvStatus.LimitExceeded)
        {
            return false;
        }

        Check(rc, "SpendMeter.TryAdmit");
        return true;
    }

    /// <summary>Releases the meter. Safe to call more than once.</summary>
    public void Dispose() => _handle.Dispose();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }

    private void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, _cap.Context);
        }
    }
}
