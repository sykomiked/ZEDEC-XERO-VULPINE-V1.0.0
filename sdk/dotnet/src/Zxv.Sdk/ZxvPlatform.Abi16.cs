// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

namespace Zxv;

/// <content>Factories for the ABI 1.6 (Vino Embed) objects that belong to a platform.</content>
public sealed partial class ZxvPlatform
{
    /// <summary>
    /// Creates an empty Vino play-lane sandbox (ABI 1.6, <c>zxv_vino_play</c>):
    /// issuers, holders and vouchers in play units, which are not money and
    /// can never become live value.
    /// </summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Vino"/>; every call on the sandbox goes through it.</param>
    /// <returns>The sandbox; dispose it when done (its play units are then gone).</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Vino"/>.</exception>
    /// <exception cref="System.ArgumentException">The capability belongs to another platform.</exception>
    public Vino.PlaySandbox CreatePlaySandbox(Capability capability) =>
        new(RequireOwn(capability));

    /// <summary>Creates one device's daily spend counter (ABI 1.6, <c>zxv_vino_meter</c>). A counter, not a legal check.</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Vino"/>.</param>
    /// <param name="dailyCap">The cap on each day's total, 1..<see cref="Vino.SpendMeter.MaxAmount"/>.</param>
    /// <returns>The meter; dispose it when done.</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Vino"/>.</exception>
    /// <exception cref="ZxvUsageException">The cap is 0 or too large (<see cref="Interop.ZxvStatus.AmountOutOfRange"/>).</exception>
    /// <exception cref="System.ArgumentException">The capability belongs to another platform.</exception>
    public Vino.SpendMeter CreateSpendMeter(Capability capability, ulong dailyCap) =>
        new(RequireOwn(capability), dailyCap);
}
