// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Fees;

/// <summary>What one charge with carry produced.</summary>
/// <param name="Fee">The fee charged now.</param>
/// <param name="Remainder">The sub-unit remainder to store for the account's next charge (below <see cref="AssuranceFee.Denominator"/>).</param>
public readonly record struct FeeCharge(ulong Fee, ulong Remainder);

/// <summary>A fee split into its four buckets; the parts always sum to the fee exactly.</summary>
/// <param name="ReserveFloor">50% and every remainder unit (backs Vino).</param>
/// <param name="VBillDividend">25%: the V-Bill dividend pool.</param>
/// <param name="InfrastructureBounty">15%: infrastructure and node bounties.</param>
/// <param name="RegenerativeCapital">10%: regenerative capital.</param>
public readonly record struct FeeSplit(ulong ReserveFloor, ulong VBillDividend, ulong InfrastructureBounty, ulong RegenerativeCapital)
{
    /// <summary>Gets the sum of the four parts (equal to the fee that was split).</summary>
    public ulong Total => ReserveFloor + VBillDividend + InfrastructureBounty + RegenerativeCapital;
}

/// <summary>The kinds of unit a fee can be measured in (<c>ZXV_FEE_UNIT_*</c>).</summary>
public enum FeeUnit
{
    /// <summary>Minor units of a ledger asset.</summary>
    Money = 0,

    /// <summary>Token-cycles.</summary>
    Compute = 1,

    /// <summary>Bytes.</summary>
    Bandwidth = 2,

    /// <summary>Byte-hours.</summary>
    Storage = 3,
}

/// <summary>An exact rational, as two integers.</summary>
/// <param name="Numerator">The numerator.</param>
/// <param name="Denominator">The denominator.</param>
public readonly record struct FeeRatio(ulong Numerator, ulong Denominator);

/// <summary>The default fee policy (<c>zxv_fee_policy_default</c>).</summary>
/// <param name="ReservePercent">Reserve floor share: 50.</param>
/// <param name="DividendPercent">V-Bill dividend share: 25.</param>
/// <param name="InfrastructurePercent">Infrastructure share: 15.</param>
/// <param name="RegenerativePercent">Regenerative capital share: 10.</param>
/// <param name="AllowBelowFee">False: a contribution below the fee is raised to it.</param>
/// <param name="MoneyCreditRate">Posted VFV store-credit rate for a voluntary money contribution above the fee: 1/1.</param>
/// <param name="ComputeCreditRate">Same for compute: 0/1 (unset).</param>
/// <param name="BandwidthCreditRate">Same for bandwidth: 0/1 (unset).</param>
/// <param name="StorageCreditRate">Same for storage: 0/1 (unset).</param>
/// <param name="CreditMultiplier">The operator's credit multiplier: 1/1.</param>
/// <param name="CommonsCap">The no-monopoly cap on one recipient's share of the non-money commons pool: 8/21.</param>
public sealed record FeePolicy(
    uint ReservePercent,
    uint DividendPercent,
    uint InfrastructurePercent,
    uint RegenerativePercent,
    bool AllowBelowFee,
    FeeRatio MoneyCreditRate,
    FeeRatio ComputeCreditRate,
    FeeRatio BandwidthCreditRate,
    FeeRatio StorageCreditRate,
    FeeRatio CreditMultiplier,
    FeeRatio CommonsCap)
{
    /// <summary>The posted credit rate for <paramref name="unit"/>.</summary>
    /// <param name="unit">The unit kind.</param>
    /// <returns>The rate.</returns>
    public FeeRatio CreditRate(FeeUnit unit) => unit switch
    {
        FeeUnit.Money => MoneyCreditRate,
        FeeUnit.Compute => ComputeCreditRate,
        FeeUnit.Bandwidth => BandwidthCreditRate,
        FeeUnit.Storage => StorageCreditRate,
        _ => throw new ArgumentOutOfRangeException(nameof(unit)),
    };
}

/// <summary>
/// The 0.08889% assurance fee (ABI 1.5, <c>zxv_fee_*</c>): exact,
/// integer-only, charged once per transaction on its gross. Pure: no
/// context, no capability.
/// </summary>
/// <remarks>
/// <para>
/// fee(g) = floor(g * 8889 / 10,000,000), formed in 128 bits so every
/// <see cref="ulong"/> gross is exact: fee(1124) = 0, fee(1125) = 1. With
/// carry, an account's sub-unit remainder goes into its next charge, so many
/// small charges pay exactly what one charge of their sum pays.
/// </para>
/// <para>
/// HONEST LIMITS. These are the arithmetic rules only. How a fee, a reserve
/// or a dividend pool is treated for tax, securities or consumer law is a
/// question for counsel; nothing here is a licence or a certification.
/// </para>
/// </remarks>
public static class AssuranceFee
{
    /// <summary>8889 (<c>ZXV_FEE_NUM</c>).</summary>
    public const ulong Numerator = 8889;

    /// <summary>10^7 (<c>ZXV_FEE_DEN</c>); a carried remainder is always below this.</summary>
    public const ulong Denominator = 10_000_000;

    /// <summary>The fee on <paramref name="gross"/>, with no carry.</summary>
    /// <param name="gross">The gross, in the unit's own minor measure.</param>
    /// <returns>floor(gross * 8889 / 10^7).</returns>
    public static ulong Compute(ulong gross)
    {
        NativeCall.Check(NativeMethods.FeeAssure(gross, out ulong fee), "AssuranceFee.Compute");
        return fee;
    }

    /// <summary>The fee on <paramref name="gross"/> with the account's incoming remainder.</summary>
    /// <param name="remainder">The remainder stored from the account's last charge (0 to start).</param>
    /// <param name="gross">The gross.</param>
    /// <returns>The fee now and the remainder to store.</returns>
    /// <exception cref="ZxvUsageException">The remainder is not below <see cref="Denominator"/>: a corrupt carry (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static FeeCharge ComputeWithCarry(ulong remainder, ulong gross)
    {
        NativeCall.Check(NativeMethods.FeeAssureCarry(remainder, gross, out ulong fee, out ulong rem), "AssuranceFee.ComputeWithCarry");
        return new FeeCharge(fee, rem);
    }

    /// <summary>Splits <paramref name="fee"/> 50/25/15/10; the reserve floor takes every remainder unit.</summary>
    /// <param name="fee">The fee.</param>
    /// <returns>The four parts.</returns>
    public static unsafe FeeSplit Split(ulong fee)
    {
        ulong* parts = stackalloc ulong[4];
        NativeCall.Check(NativeMethods.FeeSplit(fee, parts), "AssuranceFee.Split");
        return new FeeSplit(parts[0], parts[1], parts[2], parts[3]);
    }

    /// <summary>Gets the default policy.</summary>
    public static unsafe FeePolicy DefaultPolicy
    {
        get
        {
            uint* pct = stackalloc uint[4];
            ulong* num = stackalloc ulong[4];
            ulong* den = stackalloc ulong[4];
            NativeCall.Check(
                NativeMethods.FeePolicyDefault(pct, out int allowBelow, num, den, out ulong mn, out ulong md, out ulong cn, out ulong cd),
                "AssuranceFee.DefaultPolicy");
            return new FeePolicy(
                pct[0],
                pct[1],
                pct[2],
                pct[3],
                allowBelow != 0,
                new FeeRatio(num[0], den[0]),
                new FeeRatio(num[1], den[1]),
                new FeeRatio(num[2], den[2]),
                new FeeRatio(num[3], den[3]),
                new FeeRatio(mn, md),
                new FeeRatio(cn, cd));
        }
    }
}
