// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Sdk.Tests.Abi15;
using Zxv.Vino;

namespace Zxv.Sdk.Tests.Abi16;

/// <summary>ABI 1.6 statuses, the feature bit, the capability bits and the Vino refusals by capability.</summary>
public sealed class Abi16PlumbingTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void New_statuses_have_their_names_classes_and_exception_families()
    {
        int seen = 0;
        foreach (JsonElement v in V.Group("status_abi16").EnumerateArray())
        {
            var status = (ZxvStatus)v.GetProperty("code").GetInt32();
            Assert.True(Enum.IsDefined(status));
            Assert.Equal(V.Str(v, "name"), V.Name(status));
            var cls = (ZxvStatusClass)v.GetProperty("class").GetInt32();
            Assert.Equal(cls, ZxvLibrary.ClassOf(status));

            Exception e = NativeCall.Create((int)status, "test");
            Assert.Equal(cls == ZxvStatusClass.Usage ? typeof(ZxvUsageException) : typeof(ZxvRefusedException), e.GetType());
            Assert.Equal(status, ((ZxvException)e).Status);
            Assert.Equal(V.Str(v, "name"), ((ZxvException)e).StatusName);
            seen++;
        }

        Assert.Equal(5, seen);
        Assert.Equal(-46, (int)ZxvStatus.WrongRail);
        Assert.Equal(-47, (int)ZxvStatus.InsufficientCoverage);
        Assert.Equal(-48, (int)ZxvStatus.WrongLane);
        Assert.Equal(-49, (int)ZxvStatus.Expired);
        Assert.Equal(-50, (int)ZxvStatus.PaidChance);
    }

    [Fact]
    public void Features2_reports_vino_and_minimum_abi_is_1_6()
    {
        Assert.Equal(0x100u, (uint)ZxvFeatures2.Vino);
        Assert.True(ZxvLibrary.Supports(ZxvFeatures2.Vino));
        Assert.True(ZxvLibrary.AbiVersion >= new Version(1, 6));
    }

    [Fact]
    public void Capability_bits_match_the_header_and_1_16_is_unknown()
    {
        Assert.Equal(1UL << 15, (ulong)CapabilityRights.Vino);
        Assert.Equal((1UL << 16) - 1, (ulong)CapabilityRights.AllAbi16);
        Assert.Equal((1UL << 15) - 1, (ulong)CapabilityRights.AllAbi15);
        Assert.Equal(CapabilityRights.None, CapabilityRights.AllAbi15 & CapabilityRights.Vino);

        // 1 << 15 is now a real right; the first unknown bit is 1 << 16.
        using (Capability vino = _p.CreateRootCapability(CapabilityRights.Vino))
        {
            Assert.Equal(CapabilityRights.Vino, vino.Rights);
        }

        var e = Assert.Throws<ZxvUsageException>(() => _p.CreateRootCapability((CapabilityRights)((ulong)CapabilityRights.AllAbi16 + 1)));
        Assert.Equal(ZxvStatus.InvalidArgument, e.Status);
    }

    [Fact]
    public void Capability_without_vino_is_refused()
    {
        using Capability all15 = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreatePlaySandbox(all15));
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateSpendMeter(all15, 10));

        // A sandbox created with VINO refuses calls once that capability is revoked.
        using Capability root = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        Capability child = root.Attenuate(CapabilityRights.Vino);
        using PlaySandbox play = _p.CreatePlaySandbox(child);
        using SpendMeter meter = _p.CreateSpendMeter(child, 10);
        Assert.Equal(0u, play.OpenIssuer(1));
        child.Revoke();
        Assert.Throws<CapabilityDeniedException>(() => play.OpenHolder(2));
        Assert.Throws<CapabilityDeniedException>(() => meter.Admit(1, 1));
        child.Dispose();
    }

    [Fact]
    public void Capability_from_another_platform_is_refused()
    {
        using ZxvPlatform other = TestPlatform.Create();
        using Capability cap = other.CreateRootCapability(CapabilityRights.AllAbi16);
        Assert.Throws<ArgumentException>(() => _p.CreatePlaySandbox(cap));
        Assert.Throws<ArgumentException>(() => _p.CreateSpendMeter(cap, 1));
    }

    [Fact]
    public void Meter_cap_out_of_range_is_refused()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.Throws<ZxvUsageException>(() => _p.CreateSpendMeter(cap, 0)).Status);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.Throws<ZxvUsageException>(() => _p.CreateSpendMeter(cap, SpendMeter.MaxAmount + 1)).Status);
        using SpendMeter max = _p.CreateSpendMeter(cap, SpendMeter.MaxAmount);
        Assert.Equal(SpendMeter.MaxAmount, max.DailyCap);
        Assert.Equal(0UL, max.Admit(1, SpendMeter.MaxAmount));
    }
}
