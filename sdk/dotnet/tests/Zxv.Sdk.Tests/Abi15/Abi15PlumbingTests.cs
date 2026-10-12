// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text.Json;
using Zxv.Coins;
using Zxv.Interop;
using Zxv.Paradox;
using Zxv.Treaties;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 statuses, features, the buffer-growth path, handle lifetimes and capability bits.</summary>
public sealed class Abi15PlumbingTests
{
    [Fact]
    public void New_statuses_have_their_names_and_classes()
    {
        foreach (JsonElement v in V.Group("status_abi15").EnumerateArray())
        {
            var status = (ZxvStatus)v.GetProperty("code").GetInt32();
            Assert.True(Enum.IsDefined(status));
            Assert.Equal(V.Str(v, "name"), V.Name(status));
            Assert.Equal(v.GetProperty("class").GetInt32(), (int)ZxvLibrary.ClassOf(status));

            // Every one maps to the exception family its class names.
            Exception e = NativeCall.Create((int)status, "test");
            if (ZxvLibrary.ClassOf(status) == ZxvStatusClass.Usage)
            {
                Assert.IsAssignableFrom<ZxvUsageException>(e);
            }
            else
            {
                Assert.IsAssignableFrom<ZxvRefusedException>(e);
            }

            Assert.Equal(status, ((ZxvException)e).Status);
        }
    }

    [Fact]
    public void Features2_reports_every_group_and_minimum_abi_is_1_5()
    {
        Assert.True(ZxvLibrary.AbiVersion >= new Version(1, 5));
        Assert.Equal(5, ZxvLibrary.MinimumAbiMinor);
        ZxvFeatures2 all = ZxvFeatures2.Treaty | ZxvFeatures2.Aipi | ZxvFeatures2.Coin | ZxvFeatures2.Fee | ZxvFeatures2.VHub | ZxvFeatures2.Freight | ZxvFeatures2.Web4 | ZxvFeatures2.Paradox;
        Assert.Equal(all, ZxvLibrary.Features2 & all);
        Assert.True(ZxvLibrary.Supports(ZxvFeatures2.Paradox));
        Assert.Equal(ZxvLibrary.Features2, ZxvPlatform.Features2);
    }

    [Fact]
    public void Capability_bits_match_the_header()
    {
        Assert.Equal(1UL << 12, (ulong)CapabilityRights.Aipi);
        Assert.Equal(1UL << 13, (ulong)CapabilityRights.Mint);
        Assert.Equal(1UL << 14, (ulong)CapabilityRights.Paradox);
        Assert.Equal((1UL << 15) - 1, (ulong)CapabilityRights.AllAbi15);
        Assert.Equal(CapabilityRights.None, CapabilityRights.All & (CapabilityRights.Aipi | CapabilityRights.Mint | CapabilityRights.Paradox));
    }

    [Fact]
    public unsafe void Buffer_growth_path_retries_with_the_larger_size()
    {
        // A source that grows between the size query and the copy: the reader must ask again.
        int calls = 0;
        byte[] got = NativeBuffers.ReadBytes(
            (byte* b, nuint cap, out nuint len) =>
            {
                calls++;
                len = calls == 1 ? 10u : 30u;
                if (b is null || cap < len)
                {
                    return (int)ZxvStatus.BufferTooSmall;
                }

                for (int i = 0; i < (int)len; i++)
                {
                    b[i] = (byte)i;
                }

                return (int)ZxvStatus.Ok;
            },
            rc => NativeCall.Check(rc, "test"));
        Assert.Equal(3, calls);
        Assert.Equal(30, got.Length);
        Assert.Equal(29, got[29]);

        // A result that is empty needs no second call.
        calls = 0;
        Assert.Empty(NativeBuffers.ReadBytes(
            (byte* b, nuint cap, out nuint len) =>
            {
                calls++;
                len = 0;
                return (int)ZxvStatus.Ok;
            },
            rc => NativeCall.Check(rc, "test")));
        Assert.Equal(1, calls);

        // A failure is mapped, not retried.
        var e = Assert.Throws<ZxvUsageException>(() => NativeBuffers.ReadBytes(
            (byte* b, nuint cap, out nuint len) =>
            {
                len = 0;
                return (int)ZxvStatus.InvalidFormat;
            },
            rc => NativeCall.Check(rc, "test")));
        Assert.Equal(ZxvStatus.InvalidFormat, e.Status);

        // Real two-call results of every size come back whole (a 5 KiB card, a 2.4 KiB record).
        using Treaty t = Treaty.ParseSutra("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n");
        Assert.Equal(Treaty.RecordLength, t.ToRecord().Length);
    }

    [Fact]
    public void Handles_outlive_their_platform_and_release_in_any_order()
    {
        ZxvPlatform p = TestPlatform.Create();
        Capability cap = p.CreateRootCapability(CapabilityRights.AllAbi15);
        TokenWallet w = p.CreateTokenWallet(cap, 1);
        ParadoxBook book = p.CreateParadoxBook(cap, 4, 4);
        w.Mint(10);

        // R6: the native context stays alive while children hold it; the platform itself is disposed.
        p.Dispose();
        cap.Dispose();
        Assert.Throws<ObjectDisposedException>(() => p.CreateRootCapability());
        Assert.Throws<ObjectDisposedException>(() => w.Mint(1));
        Assert.Equal(10UL, w.Value);
        Assert.Equal(0, book.LogCount(ParadoxLog.Operator));
        book.Dispose();
        w.Dispose();
        Assert.Throws<ObjectDisposedException>(() => w.Value);
        Assert.Throws<ObjectDisposedException>(() => book.RegistryCount);
    }

    [Fact]
    public void Cross_platform_capabilities_are_refused()
    {
        using ZxvPlatform a = TestPlatform.Create();
        using ZxvPlatform b = TestPlatform.Create();
        using Capability capA = a.CreateRootCapability(CapabilityRights.AllAbi15);
        Assert.Throws<ArgumentException>(() => b.CreateTokenWallet(capA, 1));
        Assert.Throws<ArgumentException>(() => b.CreateParadoxBook(capA, 4, 4));
    }
}
