// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Zxv.Hardware;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi14;

/// <summary>ABI 1.4: ZHP-1 profiles, the emulation matrix and hardware updates, mirroring <c>test_zxv_abi14.c</c>.</summary>
public sealed class HwTests
{
    private const string Sha256Abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    private const string HmacCase2 = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";

    private static string Hex(ReadOnlySpan<byte> b) => Convert.ToHexString(b).ToLowerInvariant();

    private static byte[] MacInput()
    {
        var m = new byte[34];
        m[0] = 4;
        Encoding.ASCII.GetBytes("Jefe").CopyTo(m, 2);
        Encoding.ASCII.GetBytes("what do ya want for nothing?").CopyTo(m, 6);
        return m;
    }

    private static byte[] GcdInput()
    {
        var g = new byte[16];
        BinaryPrimitives.WriteUInt64LittleEndian(g, 12);
        BinaryPrimitives.WriteUInt64LittleEndian(g.AsSpan(8), 18);
        return g;
    }

    private static HwImplementation AddK(byte k, int[] bad) => (op, input, output) =>
    {
        if (bad[0] == 2)
        {
            throw new ZxvSystemException(ZxvStatus.IOError, "device", "simulated device error");
        }

        if (bad[0] == 3)
        {
            throw new InvalidOperationException("a bug in the implementation");
        }

        for (int i = 0; i < input.Length; i++)
        {
            output[i] = (byte)(input[i] + k + (bad[0] == 1 && input[i] == 9 ? 1 : 0));
        }

        return input.Length;
    };

    [Fact]
    public void Profiles_reference_host_presentation_and_extension()
    {
        Assert.True(ZxvLibrary.Supports(ZxvFeatures.Hw));
        using HwProfile host = HwProfile.ProbeHost();
        using HwProfile r1 = HwProfile.Reference();
        Assert.Equal(16, r1.NodeCount);
        Assert.Equal(16, r1.Nodes.Count);
        Assert.True(host.NodeCount >= 2);
        Assert.True(host.TryGetCap(2, HwCapKeys.WordBits, out ulong word));
        Assert.Equal((ulong)IntPtr.Size * 8, word);
        Assert.True(host.TryGetCap(2, HwCapKeys.Count, out ulong cores) && cores >= 1);

        HwMatch m = HwProfile.Match(host, r1);
        Assert.True(m.NativeCount >= 1 && m.NativeCount + m.EmulatedCount == 14, m.ToString());
        HwPairing p = HwProfile.Pair(host, r1, 1);
        Assert.Equal((2u, HwPairKind.Group), (p.TargetId, p.Kind));
        p = HwProfile.Pair(host, r1, 2);
        Assert.Equal((3u, HwPairKind.Native, 2u), (p.TargetId, p.Kind, p.HostId));
        Assert.Equal(HwPairKind.Emulate, HwProfile.Pair(host, r1, 7).Kind);
        Assert.Equal(ZxvStatus.NotFound, Assert.Throws<ZxvUsageException>(() => HwProfile.Pair(host, r1, 99)).Status);
        Assert.True(r1.TryGetCap(6, HwCapKeys.ContractId, out ulong contract));
        Assert.Equal(HwFunctions.Tensor, (uint)contract);

        using (HwProfile pres = HwProfile.Present(host, r1))
        {
            Assert.True(pres.TryGetCap(3, HwCapKeys.Kind, out ulong k3));
            Assert.Equal((ulong)HwKind.Hard, k3);
            Assert.True(pres.TryGetCap(8, HwCapKeys.Kind, out ulong k8));
            Assert.Equal((ulong)HwKind.Soft, k8);
            Assert.False(pres.TryGetCap(1, HwCapKeys.Kind, out _));
        }

        byte[] enc = r1.Encode();
        using (HwProfile copy = HwProfile.Decode(enc))
        {
            Assert.Equal(r1.Digest(), copy.Digest());
        }

        Assert.Equal(ZxvStatus.InvalidFormat, Assert.Throws<ZxvUsageException>(() => HwProfile.Decode(enc.AsSpan(0, enc.Length - 1))).Status);

        using HwProfile mine = HwProfile.Create();
        mine.AddNode(1, HwProfile.Root, HwClasses.Machine);
        mine.AddNode(2, 1, HwClasses.TernaryUnit);
        mine.SetCap(2, HwCapKeys.Radix, 3);
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.Throws<ZxvUsageException>(() => mine.AddNode(2, 1, HwClasses.Cache)).Status);
        Assert.Equal(ZxvStatus.NotFound, Assert.ThrowsAny<ZxvException>(() => mine.SetCap(9, HwCapKeys.Radix, 3)).Status);
        mine.AddNode(3, 1, HwClasses.Private | 1);
        Assert.Equal(new HwNode(3, 1, HwClasses.Private | 1, 0), mine.GetNode(2));
        HwPairing t = HwProfile.Pair(mine, r1, 7);
        Assert.Equal((HwPairKind.Native, 2u), (t.Kind, t.HostId));

        byte[] d1 = mine.Digest();
        Assert.Empty(mine.GetExtension(3));
        byte[] ext = { 0xA5, 0x00, 0x5A };
        mine.SetExtension(3, ext);
        Assert.NotEqual(d1, mine.Digest());
        Assert.Equal(ext, mine.GetExtension(3));
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.ThrowsAny<ZxvException>(() => mine.SetExtension(3, new byte[HwProfile.ExtensionMax + 1])).Status);
    }

    [Fact]
    public void Standard_engines_policy_and_discovery()
    {
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability cap = p.CreateRootCapability(CapabilityRights.AllAbi14);
        using Capability noHw = cap.Attenuate(CapabilityRights.All);
        Assert.Throws<CapabilityDeniedException>(() => p.CreateHwMatrix(noHw));
        using HwMatrix mx = p.CreateHwMatrix(cap);

        Assert.Equal(Sha256Abc, Hex(mx.Call(HwFunctions.Sha256, 0, "abc"u8)));
        byte[] mac = MacInput();
        Assert.Equal(HmacCase2, Hex(mx.Call(HwFunctions.Mac, 0, mac)));
        byte[] g = GcdInput();
        Span<byte> gcd = stackalloc byte[8];
        Assert.Equal(8, mx.Call(HwFunctions.Gcd, 0, g, gcd));
        Assert.Equal(6UL, BinaryPrimitives.ReadUInt64LittleEndian(gcd));

        IReadOnlyList<HwProviderInfo> engines = mx.GetProviders();
        Assert.True(engines.Count >= 5);
        Assert.All(engines, e =>
        {
            Assert.Equal(HwKind.Soft, e.Kind);
            Assert.Equal(Truth.True, e.Proof);
            Assert.Equal(1u, e.Contract);
            Assert.False(e.Flags.HasFlag(HwProviderStatus.Fault));
        });
        uint macPv = (uint)engines.ToList().FindIndex(e => e.FunctionId == HwFunctions.Mac);
        uint gcdPv = (uint)engines.ToList().FindIndex(e => e.FunctionId == HwFunctions.Gcd);
        Assert.Equal(ZxvStatus.NotFound, Assert.ThrowsAny<ZxvException>(() => mx.GetProviderInfo((uint)engines.Count)).Status);
        HwFunctionInfo fi = mx.GetFunctionInfo(HwFunctions.Mac);
        Assert.Equal(1u, fi.Contract);
        Assert.True(fi.Policy.HasFlag(HwPolicy.KeyBearing));
        Assert.True(fi.Vectors >= 1);

        // Key-bearing: a cheaper REMOTE implementation never serves it.
        mx.AddResource(7);
        uint remote = mx.AddProvider(HwFunctions.Mac, 7, HwKind.Remote, 1, (op, input, output) =>
            throw new ZxvRefusedException(ZxvStatus.InvalidState, "remote", "must never serve key-bearing work"));
        mx.Attest(remote);
        Assert.Equal(HmacCase2, Hex(mx.Call(HwFunctions.Mac, 0, mac)));
        Assert.Equal(macPv, mx.Bound(HwFunctions.Mac));
        Assert.True(mx.GetProviderInfo(remote).Flags.HasFlag(HwProviderStatus.ExcludedByPolicy));
        Assert.Equal(ZxvStatus.PolicyRefused, Assert.Throws<ZxvRefusedException>(() => mx.SetPolicy(HwFunctions.Mac, HwPolicy.None)).Status);
        Assert.Equal(ZxvStatus.PolicyRefused, Assert.Throws<ZxvRefusedException>(() => mx.Memo(HwFunctions.Mac, 16)).Status);

        // A power budget: undeclared traits never qualify, declared ones do.
        mx.SetPolicy(HwFunctions.Gcd, HwPolicy.None, 0, 500);
        Assert.Equal(ZxvStatus.NoProvider, Assert.Throws<ZxvRefusedException>(() => mx.Call(HwFunctions.Gcd, 0, g)).Status);
        mx.SetTraits(gcdPv, 2000, 300, 4);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.ThrowsAny<ZxvException>(() => mx.SetTraits(gcdPv, 2000, 300, 16)).Status);
        Assert.Equal(6, mx.Call(HwFunctions.Gcd, 0, g)[0]);
        HwProviderInfo gi = mx.GetProviderInfo(gcdPv);
        Assert.Equal((4u, HwPowerClass.UpTo1Watt), (gi.ThroughputClass, gi.Power));
        mx.SetPolicy(HwFunctions.Gcd, HwPolicy.LocalOnly, 1000, 0);
        Assert.Equal(ZxvStatus.NoProvider, Assert.Throws<ZxvRefusedException>(() => mx.Call(HwFunctions.Gcd, 0, g)).Status);
        mx.SetPolicy(HwFunctions.Gcd, HwPolicy.None);
    }

    [Fact]
    public void Managed_implementations_proof_failover_shadow_memo_contracts()
    {
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability cap = p.CreateRootCapability(CapabilityRights.AllAbi14);
        using HwMatrix mx = p.CreateHwMatrix(cap);
        mx.AddFunction(0x100, "add8");
        mx.AddFunction(0x101, "add16");
        Assert.Equal(ZxvStatus.Duplicate, Assert.ThrowsAny<ZxvException>(() => mx.AddFunction(0x100, "again")).Status);
        mx.AddResource(1);
        mx.AddResource(2);
        byte[] input = { 0, 1, 9, 200 }, ex8 = { 8, 9, 17, 208 }, ex16 = { 16, 17, 25, 216 };
        mx.AddVector(0x100, new HwVector(0, input, ex8));
        mx.AddVector(0x101, new HwVector(0, input, ex16));
        int[] fxBad = { 0 }, swBad = { 0 };
        uint pfx = mx.AddProvider(0x100, 2, HwKind.Hard, 1, AddK(8, fxBad));
        uint psw = mx.AddProvider(0x100, 1, HwKind.Soft, 10, AddK(8, swBad));

        // add16 through two nested calls to add8 on the same matrix.
        uint p16 = mx.AddProvider(0x101, 1, HwKind.Soft, 20, (op, x, output) =>
        {
            byte[] once = mx.Call(0x100, op, x);
            return mx.Call(0x100, op, once, output);
        });

        Assert.Equal(ZxvStatus.NoProvider, Assert.Throws<ZxvRefusedException>(() => mx.Call(0x100, 0, input)).Status);
        Assert.Equal(Truth.True, mx.Attest(pfx));
        Assert.Equal(Truth.True, mx.Attest(psw));
        Assert.Equal(Truth.False, mx.Attest(p16));
        mx.AddDependency(p16, 0x100);
        Assert.Equal(Truth.True, mx.Attest(p16));
        Assert.Equal(ex16, mx.Call(0x101, 0, input));

        Assert.Equal(pfx, mx.Bound(0x100));
        mx.SetResourceState(2, HwResourceState.Faulted);
        Assert.Equal(ex8, mx.Call(0x100, 0, input));
        Assert.Equal(psw, mx.Bound(0x100));
        mx.SetResourceState(2, HwResourceState.Ok);

        // An implementation's status comes back as its own; a bug is rethrown as itself.
        fxBad[0] = 2;
        Assert.Equal(ZxvStatus.IOError, Assert.Throws<ZxvSystemException>(() => mx.Call(0x100, 0, input)).Status);
        fxBad[0] = 3;
        Assert.Equal("a bug in the implementation", Assert.Throws<InvalidOperationException>(() => mx.Call(0x100, 0, input)).Message);
        fxBad[0] = 0;
        Assert.Equal(ex8, mx.Call(0x100, 0, input));

        // Shadow: agreement, then a silent fault: dispute, nobody serves.
        Assert.True(mx.Shadow(0x100, 0, input).Checked);
        swBad[0] = 1;
        Assert.Equal(ZxvStatus.Dispute, Assert.Throws<ZxvRefusedException>(() => mx.Shadow(0x100, 0, input)).Status);
        Assert.Equal(ZxvStatus.NoProvider, Assert.Throws<ZxvRefusedException>(() => mx.Call(0x100, 0, input)).Status);
        Assert.Equal(Truth.True, mx.Attest(pfx));
        Assert.Equal(Truth.False, mx.Attest(psw));

        // Memo: a repeat skips the implementation.
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.Throws<ZxvUsageException>(() => mx.Memo(0x100, 6)).Status);
        mx.Memo(0x100, 16);
        ulong calls0 = mx.GetProviderInfo(pfx).Calls;
        mx.Call(0x100, 0, input);
        Assert.Equal(ex8, mx.Call(0x100, 0, input));
        Assert.Equal(calls0 + 1, mx.GetProviderInfo(pfx).Calls);
        Assert.Equal(new HwMemoStats(1, 1), mx.Memo(0x100, 16));

        // Versioned contracts: a new version makes everyone prove again.
        Assert.Equal(new HwFunctionInfo(0, HwPolicy.None, 1), mx.GetFunctionInfo(0x100));
        mx.StartContract(0x100, 2);
        Assert.Equal(ZxvStatus.Rollback, Assert.Throws<ZxvRefusedException>(() => mx.StartContract(0x100, 2)).Status);
        Assert.Equal(ZxvStatus.NoProvider, Assert.Throws<ZxvRefusedException>(() => mx.Call(0x100, 0, input)).Status);
        mx.AddVector(0x100, new HwVector(0, input, ex8));
        Assert.Equal(Truth.True, mx.Attest(pfx));
        Assert.Equal(2u, mx.GetProviderInfo(pfx).Contract);
        Assert.True(mx.GetProviderInfo(psw).Flags.HasFlag(HwProviderStatus.Unproven));
        mx.SetEnabled(pfx, false);
        Assert.True(mx.GetProviderInfo(pfx).Flags.HasFlag(HwProviderStatus.Disabled));
    }

    /// <summary>A simulated two-slot device: the active slot is a 256-entry lookup table.</summary>
    private sealed class Sim : IHwRegionDevice
    {
        private readonly byte[][] _slot = { new byte[256], new byte[256] };
        private readonly int[] _len = new int[2];
        private int _active = -1;

        public void Load(uint slot, ReadOnlySpan<byte> payload)
        {
            if (slot > 1 || payload.Length > 256)
            {
                throw new ZxvUsageException(ZxvStatus.AmountOutOfRange, "sim", "slot");
            }

            payload.CopyTo(_slot[slot]);
            _len[slot] = payload.Length;
        }

        public void ReadBack(uint slot, Span<byte> digest) => SHA256.HashData(_slot[slot].AsSpan(0, _len[slot]), digest);

        public void Activate(uint slot) => _active = (int)slot;

        public int Run(ReadOnlySpan<byte> input, Span<byte> output)
        {
            if (_active < 0)
            {
                throw new ZxvRefusedException(ZxvStatus.InvalidState, "sim", "no image");
            }

            for (int i = 0; i < input.Length; i++)
            {
                output[i] = _slot[_active][input[i]];
            }

            return input.Length;
        }
    }

    [Fact]
    public void Signed_update_region_events_audit_log_and_contract()
    {
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability cap = p.CreateRootCapability(CapabilityRights.AllAbi14);
        using Capability noHw = cap.Attenuate(CapabilityRights.All);
        using HwMatrix mx = p.CreateHwMatrix(cap);
        var signer = new HwPackageSigner(cap);
        using HwReleaseKey key = signer.GenerateKey();
        using HwReleaseKey other = new HwPackageSigner(noHw).GenerateKey();
        Assert.Equal(HwPackageSigner.PublicKeyBytes, key.PublicKey.Length);
        Assert.Equal((4896, 128), (key.MlDsaSecretKey.Length, key.SlhDsaSecretKey.Length));
        Assert.Equal(SecretKeyKind.SlhDsaShake256s, key.SlhDsaSecretKey.Kind);

        mx.AddFunction(0x200, "inc");
        mx.AddResource(1);
        mx.AddResource(2);
        byte[] vin = new byte[256], inc = new byte[256];
        for (int i = 0; i < 256; i++)
        {
            vin[i] = (byte)i;
            inc[i] = (byte)(i + 1);
        }

        mx.AddVector(0x200, new HwVector(0, vin, inc));
        var sim = new Sim();
        uint psoft = mx.AddProvider(0x200, 1, HwKind.Soft, 50, AddK(1, new int[1]));
        uint preg = mx.AddProvider(0x200, 2, HwKind.Fabric, 1, (op, x, output) => sim.Run(x, output));
        Assert.Equal(Truth.True, mx.Attest(psoft));

        Assert.Throws<CapabilityDeniedException>(() => p.CreateHwRegion(noHw, mx, preg, 1, 0xD0, default, sim, 16));
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.Throws<ZxvUsageException>(() => p.CreateHwRegion(cap, mx, psoft, 1, 0xD0, default, sim, 16)).Status);
        using HwRegion rg = p.CreateHwRegion(cap, mx, preg, 1, 0xD0, default, sim, 16);
        rg.TrustKey(key.PublicKey.Span);
        var events = new List<HwRegionEventArgs>();
        rg.StepRecorded += (_, e) => events.Add(e);

        // Signing 1: the wrong signer is refused and recorded.
        byte[] forged = signer.BuildImage(HwImageKind.PartialBitstream, 0xD0, default, 0x200, 1, 1, inc, other);
        Assert.Throws<VerificationFailedException>(() => rg.Stage(forged));

        // Signing 2: the update. Wrong hardware first (checked before the signature).
        byte[] good = signer.BuildImage(HwImageKind.PartialBitstream, 0xD0, default, 0x200, 1, 1, inc, key);
        Assert.Equal(HwPackageSigner.HeaderBytes + 256 + HwPackageSigner.SignatureBytes, good.Length);
        byte[] wrongHw = (byte[])good.Clone();
        BinaryPrimitives.WriteUInt32LittleEndian(wrongHw.AsSpan(12), 0xD1);
        Assert.Equal(ZxvStatus.WrongTarget, Assert.Throws<ZxvUsageException>(() => rg.Stage(wrongHw)).Status);

        rg.Stage(good);
        Assert.Equal(ZxvStatus.InvalidState, Assert.ThrowsAny<ZxvException>(rg.Commit).Status);
        rg.Trial(5);
        Assert.Equal(preg, mx.Bound(0x200));
        rg.Commit();
        HwRegionInfo info = rg.GetInfo();
        Assert.Equal((HwRegionState.Active, 1u, 1u), (info.State, info.Version, info.Floor));
        Assert.Equal(HwEvent.Rejected, events[0].Kind);
        Assert.Equal(new[] { HwEvent.Staged, HwEvent.Tested, HwEvent.Activated }, events.Skip(events.Count - 3).Select(e => e.Kind));
        Assert.Equal((1u, 0, 1u), (events[^1].RegionId, events[^1].Reason, events[^1].Version));
        Assert.Equal(inc, mx.Call(0x200, 0, vin));

        // Replay: refused, recorded.
        Assert.Equal(ZxvStatus.Rollback, Assert.Throws<ZxvRefusedException>(() => rg.Stage(good)).Status);

        HwAuditHead head = rg.GetLog();
        Assert.Equal((uint)events.Count, head.Count);
        HwAuditRecord last = rg.GetLogEntry((int)head.Count - 1);
        Assert.Equal((HwEvent.Rejected, 1u), (last.Event, last.Id));
        Assert.True(last.Tag.IsForbidden);
        Assert.Equal(head.Head.ToArray(), last.Hash.ToArray());
        HwAuditRecord activated = Enumerable.Range(0, (int)head.Count).Select(rg.GetLogEntry).Single(r => r.Event == HwEvent.Activated);
        Assert.Equal(1u, activated.Version);
        Assert.False(activated.Tag.IsForbidden);
        Assert.Equal(ZxvStatus.NotFound, Assert.ThrowsAny<ZxvException>(() => rg.GetLogEntry((int)head.Count)).Status);

        // Signing 3: a contract package for "inc", applied to the matrix.
        byte[] pkg = signer.BuildContract(0x200, 7, 3, new[] { new HwVector(0, new byte[] { 5, 250 }, new byte[] { 6, 251 }) }, key);
        Assert.Throws<VerificationFailedException>(() => mx.ApplyContract(pkg));
        mx.TrustKey(key.PublicKey.Span);
        Assert.Equal(ZxvStatus.InvalidFormat, Assert.Throws<ZxvUsageException>(() => mx.ApplyContract(good)).Status);
        Assert.Equal(ZxvStatus.InvalidFormat, Assert.Throws<ZxvUsageException>(() => rg.Stage(pkg)).Status);
        Assert.Equal(2u, mx.ApplyContract(pkg));
        HwFunctionInfo fi = mx.GetFunctionInfo(0x200);
        Assert.Equal((7u, 1u), (fi.Contract, fi.Vectors));
        Assert.Equal(ZxvStatus.Rollback, Assert.Throws<ZxvRefusedException>(() => mx.ApplyContract(pkg)).Status);
        HwAuditHead mlog = mx.GetLog();
        Assert.Equal(3u, mlog.Count);
        HwAuditRecord contract = mx.GetLogEntry(1);
        Assert.Equal((HwEvent.Contract, 0x200u, 7u), (contract.Event, contract.Id, contract.Version));
    }

    [Fact]
    public void Rights_features_and_statuses()
    {
        Assert.Equal(1UL << 10, (ulong)CapabilityRights.HwMatrix);
        Assert.Equal(1UL << 11, (ulong)CapabilityRights.HwReconf);
        Assert.Equal(CapabilityRights.All | CapabilityRights.HwMatrix | CapabilityRights.HwReconf, CapabilityRights.AllAbi14);
        Assert.Equal(0x1000_0000u, (uint)ZxvFeatures.Hw);
        Assert.Equal(ZxvStatusClass.Refused, ZxvLibrary.ClassOf(ZxvStatus.PolicyRefused));
        Assert.Equal(ZxvStatusClass.Usage, ZxvLibrary.ClassOf(ZxvStatus.WrongTarget));
        Assert.Equal(ZxvStatusClass.Refused, ZxvLibrary.ClassOf(ZxvStatus.Dispute));
        foreach ((ZxvStatus s, string name) in new[]
        {
            (ZxvStatus.NoProvider, "ZXV_E_NOPROVIDER"), (ZxvStatus.Dispute, "ZXV_E_DISPUTE"), (ZxvStatus.Quarantined, "ZXV_E_QUARANTINED"),
            (ZxvStatus.Rollback, "ZXV_E_ROLLBACK"), (ZxvStatus.Blocked, "ZXV_E_BLOCKED"), (ZxvStatus.WrongTarget, "ZXV_E_TARGET"),
            (ZxvStatus.PolicyRefused, "ZXV_E_POLICY"),
        })
        {
            Assert.Equal(name, ZxvLibrary.GetStatusName(s));
        }
    }
}
