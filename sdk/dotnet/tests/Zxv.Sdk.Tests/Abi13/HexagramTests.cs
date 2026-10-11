// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Zxv.Interop;
using Cf = Zxv.Sdk.Tests.Abi11.Conformance;

namespace Zxv.Sdk.Tests.Abi13;

/// <summary>ABI 1.3: the 64-state hexagram tag and quarantine log against <c>vectors.json</c> key <c>hex</c>.</summary>
public sealed class HexagramTests
{
    private static readonly JsonElement Hx = Cf.Vectors.GetProperty("hex");
    private static readonly Hexagram[] All = Enumerable.Range(0, 64).Select(Hexagram.FromTag).ToArray();

    private static string[] Lower<T>()
        where T : struct, Enum => Enum.GetNames<T>().Select(n => n.ToLowerInvariant()).ToArray();

    [Fact]
    public void Tags_match_vectors()
    {
        Assert.True(ZxvLibrary.Supports(ZxvFeatures.Hex));
        Assert.Equal(Hx.GetProperty("base_names").EnumerateArray().Select(e => e.GetString()), Lower<HexBase>());
        Assert.Equal(Hx.GetProperty("polarity_names").EnumerateArray().Select(e => e.GetString()), Lower<HexPolarity>());
        Assert.Equal(Hx.GetProperty("phase_names").EnumerateArray().Select(e => e.GetString()), Lower<HexPhase>());
        Assert.Equal(Hx.GetProperty("action_names").EnumerateArray().Select(e => e.GetString()), Lower<HexAction>());
        foreach (JsonElement v in Hx.GetProperty("tags").EnumerateArray())
        {
            Hexagram h = All[v.GetProperty("tag").GetInt32()];
            Assert.Equal(v.GetProperty("name").GetString(), h.Name);
            Assert.Equal(v.GetProperty("base").GetInt32(), (int)h.Base);
            Assert.Equal(v.GetProperty("polarity").GetInt32(), (int)h.Polarity);
            Assert.Equal(v.GetProperty("phase").GetInt32(), (int)h.Phase);
            Assert.Equal(v.GetProperty("action").GetInt32(), (int)h.Action);
            Assert.Equal(h, new Hexagram(h.Base, h.Polarity, h.Phase));
        }

        int[] next = Cf.Ints(Hx.GetProperty("phase_next"));
        foreach (HexPhase p in Enum.GetValues<HexPhase>())
        {
            Assert.Equal(next[(int)p], (int)Hexagram.NextPhase(p));
        }
    }

    [Fact]
    public void Logic_tables()
    {
        int[] not = Cf.Ints(Hx.GetProperty("not"));
        JsonElement and = Hx.GetProperty("and"), or = Hx.GetProperty("or");
        foreach (Hexagram a in All)
        {
            Assert.Equal(not[a.Tag], (~a).Tag);
            int[] ar = Cf.Ints(and[a.Tag]), orr = Cf.Ints(or[a.Tag]);
            foreach (Hexagram b in All)
            {
                Assert.Equal(ar[b.Tag], (a & b).Tag);
                Assert.Equal(orr[b.Tag], (a | b).Tag);
            }
        }
    }

    [Fact]
    public void Step_and_set()
    {
        JsonElement step = Hx.GetProperty("step"), set = Hx.GetProperty("set");
        foreach (Hexagram h in All)
        {
            int[] st = Cf.Ints(step[h.Tag]), se = Cf.Ints(set[h.Tag]);
            foreach (HexPhase p in Enum.GetValues<HexPhase>())
            {
                if (st[(int)p] < 0)
                {
                    var e = Assert.Throws<ZxvRefusedException>(() => h.Step(p));
                    Assert.Equal(ZxvStatus.InvalidState, e.Status);
                }
                else
                {
                    Assert.Equal(st[(int)p], h.Step(p).Tag);
                }
            }

            foreach (HexBase b in Enum.GetValues<HexBase>())
            {
                foreach (HexPolarity pol in Enum.GetValues<HexPolarity>())
                {
                    int want = se[((int)b * 4) + (int)pol];
                    if (want < 0)
                    {
                        Assert.Throws<ZxvRefusedException>(() => h.WithState(b, pol));
                    }
                    else
                    {
                        Assert.Equal(want, h.WithState(b, pol).Tag);
                    }
                }
            }

            Assert.Equal(new Hexagram(h.Base, HexPolarity.Forbidden, h.Phase), h.Forbid());
            Assert.Equal(Hexagram.NextPhase(h.Phase), h.Advance().Phase);
        }
    }

    [Fact]
    public void Polarity_and_truth_bridge()
    {
        foreach (JsonElement c in Hx.GetProperty("polarity").EnumerateArray())
        {
            HexPolarity got = Hexagram.PolarityFromEvidence(c.GetProperty("pos0").GetUInt32(), c.GetProperty("neg0").GetUInt32(),
                c.GetProperty("pos1").GetUInt32(), c.GetProperty("neg1").GetUInt32(), c.GetProperty("stale").GetBoolean());
            Assert.Equal(c.GetProperty("polarity").GetInt32(), (int)got);
        }

        int[] lpToBase = Cf.Ints(Hx.GetProperty("lp_to_base")), baseToLp = Cf.Ints(Hx.GetProperty("base_to_lp"));
        foreach (Truth t in Enum.GetValues<Truth>())
        {
            Hexagram h = Hexagram.FromTruth(t, HexPolarity.Neutral, HexPhase.Potential);
            Assert.Equal(lpToBase[(int)t], (int)h.Base);
            Assert.Equal(t, h.ToTruth());
        }

        foreach (HexBase b in Enum.GetValues<HexBase>())
        {
            Assert.Equal(baseToLp[(int)b], (int)new Hexagram(b, HexPolarity.Neutral, HexPhase.Potential).ToTruth());
        }

        // The numbering differs on purpose.
        Assert.Equal(1, (int)Truth.True);
        Assert.Equal(HexBase.True, Hexagram.FromTruth(Truth.True, HexPolarity.Neutral, HexPhase.Potential).Base);
        Assert.Equal(3, (int)HexBase.True);
        Assert.Throws<ZxvUsageException>(() => Hexagram.FromTruth((Truth)7, HexPolarity.Neutral, HexPhase.Potential));
    }

    [Fact]
    public void Refusals_and_value_semantics()
    {
        foreach (int bad in new[] { 64, 100, 255, 256, -1 })
        {
            Assert.Throws<ZxvUsageException>(() => Hexagram.FromTag(bad));
        }

        Assert.Throws<ZxvUsageException>(() => new Hexagram((HexBase)4, HexPolarity.Neutral, HexPhase.Potential));
        Assert.Throws<ZxvUsageException>(() => All[0].Step((HexPhase)9));
        var h = new Hexagram(HexBase.True, HexPolarity.Negentropic, HexPhase.Manifest);
        Assert.Equal(63, h.Tag);
        Assert.Equal("Hexagram(true/negentropic/manifest, tag=63)", h.ToString());
        Assert.True(h == All[63] && h != All[62] && h.GetHashCode() == 63);
        Assert.Equal(All[0], default(Hexagram));
        Hexagram f = new(HexBase.True, HexPolarity.Forbidden, HexPhase.Potential);
        Assert.Equal(HexAction.Rollback, f.Action);
        for (int i = 0; i < 4; i++)
        {
            f = f.Advance();
            Assert.True(f.IsForbidden);
        }

        Assert.True((f & h).IsForbidden && (f | h).IsForbidden && (~f).IsForbidden);
        Assert.Throws<ZxvRefusedException>(() => f.WithState(HexBase.True, HexPolarity.Neutral));
        Assert.Equal(HexBase.False, f.WithState(HexBase.False, HexPolarity.Forbidden).Base);
    }

    private static byte[] Link(ulong seq, byte tag, byte reason, uint who, byte[] subject, byte[] prev)
    {
        var b = new byte[15 + 8 + 1 + 1 + 4 + 32 + 32];
        Encoding.ASCII.GetBytes("zxv/lpx-qlog/v1").CopyTo(b, 0);
        BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(15), seq);
        b[23] = tag;
        b[24] = reason;
        BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(25), who);
        subject.CopyTo(b, 29);
        prev.CopyTo(b, 61);
        return SHA256.HashData(b);
    }

    [Fact]
    public void Quarantine_chain_matches_vectors()
    {
        JsonElement q = Hx.GetProperty("qlog");
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability cap = p.CreateRootCapability();
        using QuarantineLog log = p.CreateQuarantineLog(cap, 3);
        Assert.Equal(0, log.Count);
        Assert.Equal(new byte[32], log.GetHead());
        byte[] prev = new byte[32];
        foreach (JsonElement r in q.GetProperty("records").EnumerateArray())
        {
            byte[] subject = Cf.Hex(r.GetProperty("subject").GetString()!);
            byte tag = r.GetProperty("tag").GetByte(), reason = r.GetProperty("reason").GetByte();
            uint who = r.GetProperty("submitter").GetUInt32();
            ulong seq = log.Append(Hexagram.FromTag(tag), reason, who, subject);
            Assert.Equal(r.GetProperty("seq").GetUInt64(), seq);
            byte[] want = Link(seq, tag, reason, who, subject, prev);
            Assert.Equal(r.GetProperty("hash").GetString(), Cf.ToHex(want));
            QuarantineRecord rec = log.GetRecord((int)seq);
            Assert.Equal(want, rec.Hash.ToArray());
            Assert.Equal(prev, rec.Prev.ToArray());
            Assert.Equal(subject, rec.Subject.ToArray());
            Assert.Equal((tag, reason, who), (rec.Tag.Tag, rec.Reason, rec.Submitter));
            prev = want;
        }

        Assert.Equal(q.GetProperty("head").GetString(), Cf.ToHex(log.GetHead()));
        Assert.Equal(3, log.Count);
        log.Verify();
        var full = Assert.Throws<ZxvRefusedException>(() => log.Append(All[0x3B], 0, 0, new byte[32]));
        Assert.Equal(ZxvStatus.CapacityExceeded, full.Status);
    }

    [Fact]
    public void Quarantine_refusals()
    {
        var forb = new Hexagram(HexBase.Both, HexPolarity.Forbidden, HexPhase.Emerging);
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability cap = p.CreateRootCapability();
        using Capability noAttest = cap.Attenuate(CapabilityRights.All & ~CapabilityRights.Attest);
        Assert.Throws<CapabilityDeniedException>(() => p.CreateQuarantineLog(noAttest, 4));
        Assert.Throws<ZxvUsageException>(() => p.CreateQuarantineLog(cap, 0));
        Assert.Throws<ZxvUsageException>(() => p.CreateQuarantineLog(cap, QuarantineLog.MaxRecords + 1));
        using QuarantineLog log = p.CreateQuarantineLog(cap, 4);
        Assert.Throws<ZxvUsageException>(() => log.Append(new Hexagram(HexBase.True, HexPolarity.Neutral, HexPhase.Emerging), 1, 1, new byte[32]));
        Assert.Throws<ZxvUsageException>(() => log.Append(forb, 1, 1, new byte[31]));
        Assert.Equal(0UL, log.Append(forb, 9, uint.MaxValue, new byte[32]));
        Assert.Equal(uint.MaxValue, log.GetRecord(0).Submitter);
        p.Halt("test");
        Assert.Throws<ZxvFatalException>(() => log.Count);
    }
}
