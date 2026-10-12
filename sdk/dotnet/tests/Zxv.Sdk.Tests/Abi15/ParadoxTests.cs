// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Paradox;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 paradox engine against <c>vectors.json</c> "paradox": every step in order on one book.</summary>
public sealed class ParadoxTests : IDisposable
{
    private static readonly JsonElement P = V.Group("paradox");

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    private static Hexagram Tag(JsonElement e) => Hexagram.FromTag(e.GetInt32());

    private static long[] Longs(JsonElement e) => e.EnumerateArray().Select(V.I64).ToArray();

    private static byte[] Id(string letter) => Enumerable.Repeat((byte)letter[0], 32).ToArray();

    private static void Heads(ParadoxBook book, JsonElement heads)
    {
        foreach ((string name, ParadoxLog log) in new[] { ("ops", ParadoxLog.Operator), ("frontier", ParadoxLog.Frontier), ("quarantine", ParadoxLog.Quarantine) })
        {
            Assert.Equal(heads.GetProperty(name).GetProperty("count").GetInt32(), book.LogCount(log));
            Assert.Equal(V.Str(heads.GetProperty(name), "head"), V.ToHex(book.LogHead(log)));
        }
    }

    [Fact]
    public void Pure_tables_and_names()
    {
        string[] sigs = P.GetProperty("signature").EnumerateArray().Select(s => V.Str(s, "sig")).ToArray();
        string[] got = P.GetProperty("signature").EnumerateArray().Select(s => V.ToHex(ParadoxEngine.Signature(V.Utf8(s.GetProperty("utf8"))))).ToArray();
        Assert.Equal(sigs, got);

        Assert.Equal(P.GetProperty("kind_names").EnumerateArray().Select(e => e.GetString()), Enumerable.Range(0, 5).Select(k => ParadoxEngine.KindName((ParadoxKind)k)));
        Assert.Equal(P.GetProperty("outcome_names").EnumerateArray().Select(e => e.GetString()), Enumerable.Range(0, 14).Select(k => ParadoxEngine.OutcomeName((ParadoxOutcome)k)));
        Assert.Equal(P.GetProperty("node_state_names").EnumerateArray().Select(e => e.GetString()), Enumerable.Range(0, 10).Select(k => ParadoxEngine.NodeStateName((ParadoxNodeState)k)));
        Assert.Equal("INVALID", ParadoxEngine.KindName((ParadoxKind)99));

        int[] kot = Abi11.Conformance.Ints(P.GetProperty("kind_of_tag"));
        for (int t = 0; t < 64; t++)
        {
            Assert.Equal(kot[t], (int)ParadoxEngine.KindOf(Hexagram.FromTag(t)));
        }

        JsonElement[] action = P.GetProperty("action").EnumerateArray().ToArray();
        for (int k = 0; k < action.Length; k++)
        {
            int[] row = Abi11.Conformance.Ints(action[k]);
            for (int t = 0; t < 64; t++)
            {
                int kk = k, tt = t;
                ParadoxAction a = ParadoxAction.None;
                string status = V.StatusOf(() => a = ParadoxEngine.ActionFor((ParadoxKind)kk, Hexagram.FromTag(tt)));
                Assert.Equal(row[t] < 0 ? "ZXV_E_ARG" : "ZXV_OK", status);
                if (row[t] >= 0)
                {
                    Assert.Equal(row[t], (int)a);
                }
            }
        }

        int[] oa = Abi11.Conformance.Ints(P.GetProperty("outcome_action"));
        for (int o = 0; o < oa.Length; o++)
        {
            Assert.Equal(oa[o], (int)ParadoxEngine.OutcomeAction((ParadoxOutcome)o));
        }
    }

    [Fact]
    public void Every_step_in_order_on_one_book()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using ParadoxBook book = _p.CreateParadoxBook(cap, 64, 16);
        Heads(book, P.GetProperty("heads_fresh"));
        byte[][] sig = P.GetProperty("signature").EnumerateArray().Select(s => V.Hex(s.GetProperty("sig"))).ToArray();

        // classify, with the loop signature registered as destructive part way through
        foreach (JsonElement c in P.GetProperty("classify").EnumerateArray())
        {
            if (c.TryGetProperty("registry_add", out JsonElement ra))
            {
                int idx = -1;
                V.Expect(ra, () => idx = book.RegistryAdd(sig[2], (ParadoxKind)ra.GetProperty("kind").GetInt32(), (byte)ra.GetProperty("reason").GetInt32(), (ParadoxConcerns)ra.GetProperty("flags").GetInt32(), ra.GetProperty("owner_ok").GetBoolean()));
                Assert.Equal(ra.GetProperty("index").GetInt32(), idx);
                continue;
            }

            ParadoxEvidence? ev = null;
            if (c.GetProperty("evidence").ValueKind == JsonValueKind.Array)
            {
                uint[] e = c.GetProperty("evidence").EnumerateArray().Select(x => x.GetUInt32()).ToArray();
                ev = new ParadoxEvidence(e[0], e[1], e[2], e[3]);
            }

            byte[] s = V.Has(c, "sig") ? sig[2] : Array.Empty<byte>();
            ParadoxClassification r = default;
            V.Expect(c, () => r = book.Classify(Tag(c.GetProperty("tag")), ev, c.GetProperty("stale").GetBoolean(), s));
            Assert.Equal(new ParadoxClassification((ParadoxKind)c.GetProperty("kind").GetInt32(), Tag(c.GetProperty("out_tag")), c.GetProperty("reg_index").GetInt32()), r);
            if (s.Length == 0)
            {
                Assert.Equal(r, ParadoxEngine.Classify(Tag(c.GetProperty("tag")), ev, c.GetProperty("stale").GetBoolean()));
            }
        }

        foreach (JsonElement g in P.GetProperty("registry").EnumerateArray())
        {
            byte[] s = ParadoxEngine.Signature(V.Utf8(g.GetProperty("utf8")));
            int idx = -1;
            if (V.Expect(g, () => idx = book.RegistryAdd(s, (ParadoxKind)g.GetProperty("kind").GetInt32(), 0, (ParadoxConcerns)g.GetProperty("flags").GetInt32(), g.GetProperty("owner_ok").GetBoolean())))
            {
                Assert.Equal(g.GetProperty("index").GetInt32(), idx);
                Assert.Equal(idx, book.RegistryFind(s));
                ParadoxRegistryEntry entry = book.RegistryAt(idx);
                Assert.Equal(s, entry.Signature);
                Assert.Equal(ParadoxConcerns.Money, entry.Concerns);
                Assert.Equal(ParadoxBook.SeedSource, entry.Source);
            }
        }

        Heads(book, P.GetProperty("heads_after_classify"));

        // constructive: two policies, plain and money
        JsonElement cs = P.GetProperty("constructive");
        long[] lo = Longs(cs.GetProperty("lo")), hi = Longs(cs.GetProperty("hi")), init = Longs(cs.GetProperty("initial"));
        using ParadoxPolicy plain = _p.CreateParadoxPolicy(cap, lo, hi, init);
        using ParadoxPolicy money = _p.CreateParadoxPolicy(cap, lo, hi, init, ParadoxConcerns.Money);
        Assert.Equal(lo, plain.Lower);
        Assert.Equal(hi, plain.Upper);
        Assert.Equal(ParadoxConcerns.Money, money.Concerns);
        foreach (JsonElement st in cs.GetProperty("steps").EnumerateArray())
        {
            ParadoxPolicy pol = V.Str(st, "policy") == "money" ? money : plain;
            long[] synth = st.GetProperty("synth").ValueKind == JsonValueKind.Array ? Longs(st.GetProperty("synth")) : Array.Empty<long>();
            if (V.Has(st, "proposal"))
            {
                Assert.Equal(V.Str(st, "proposal"), V.ToHex(pol.Proposal(synth)));
            }

            ParadoxOutcome o = default;
            if (V.Expect(st, () => o = book.Constructive(pol, (ParadoxKind)st.GetProperty("kind").GetInt32(), Tag(st.GetProperty("tag")), synth, st.GetProperty("tension").GetUInt32())))
            {
                Assert.Equal(st.GetProperty("outcome").GetInt32(), (int)o);
            }

            ParadoxPolicyState ps = pol.State;
            Assert.Equal(Longs(st.GetProperty("values")), ps.Values);
            Assert.Equal(Longs(st.GetProperty("prev")), ps.Previous);
            Assert.Equal(st.GetProperty("armed").GetBoolean(), ps.IsArmed);
            Assert.Equal(st.GetProperty("tension_at").GetUInt32(), ps.TensionAtApply);
        }

        // destructive: contain and prove, then a refusal that changes nothing
        foreach (JsonElement d in P.GetProperty("destructive").EnumerateArray())
        {
            byte[] state = V.Utf8(d.GetProperty("state_utf8"));
            ulong[] trace = d.GetProperty("trace").EnumerateArray().Select(V.U64).ToArray();
            ParadoxContainment r = default;
            V.Expect(d, () => r = book.Destructive((ParadoxKind)d.GetProperty("kind").GetInt32(), Tag(d.GetProperty("tag")), d.GetProperty("steps").GetUInt32(), d.GetProperty("step_limit").GetUInt32(), d.GetProperty("spent").GetUInt32(), d.GetProperty("budget").GetUInt32(), trace, d.GetProperty("invariant_ok").GetBoolean(), state, V.Utf8(d.GetProperty("checkpoint_utf8")), V.Utf8(d.GetProperty("pattern_utf8")), d.GetProperty("submitter").GetUInt32()));
            Assert.Equal(d.GetProperty("outcome").GetInt32(), (int)r.Outcome);
            Assert.Equal(V.Str(d, "state_after"), V.ToHex(state));
            if (V.Has(d, "repair_proof"))
            {
                Assert.Equal(V.Str(d, "repair_proof"), r.Proof.ToString());
                Assert.Equal(V.Str(d, "repair_verify") == "ZXV_OK", r.Proof.Verify());
                Assert.Equal(sig[2], r.Proof.Signature.ToArray());
                Assert.True(r.Proof.Trips.HasFlag(ParadoxTrips.Steps));
                byte[] flipped = r.Proof.ToArray();
                flipped[0] ^= 1;
                Assert.Equal(V.Str(d, "repair_verify_flipped_byte0") == "ZXV_OK", RepairProof.FromBytes(flipped).Verify());
            }
            else
            {
                Assert.Equal(default, r.Proof);
            }
        }

        // neutral: hold both branches until one is decisive
        JsonElement n = P.GetProperty("neutral");
        using ParadoxHold hold = _p.CreateParadoxHold(cap, n.GetProperty("capacity").GetInt32(), n.GetProperty("decisive").GetInt32());
        byte[][] branches = n.GetProperty("branches").EnumerateArray().Select(V.Hex).ToArray();
        foreach (byte[] b in branches)
        {
            hold.Add(b);
            hold.AddEvidence(b, positive: true);
        }

        foreach (JsonElement st in n.GetProperty("steps").EnumerateArray().Skip(1))
        {
            for (int k = 0; k < st.GetProperty("add_for_branch0").GetInt32(); k++)
            {
                hold.AddEvidence(branches[0], positive: true);
            }

            ParadoxHoldResult r = default;
            V.Expect(st, () => r = book.Neutral(hold, (ParadoxKind)st.GetProperty("kind").GetInt32(), Tag(st.GetProperty("tag")), st.GetProperty("owner_pick").GetInt32()));
            Assert.Equal(new ParadoxHoldResult((ParadoxOutcome)st.GetProperty("outcome").GetInt32(), st.GetProperty("settled").GetInt32()), r);
        }

        Assert.Equal(0, hold.Info.Settled);
        ParadoxBranch b0 = hold.GetBranch(0);
        Assert.Equal(branches[0], b0.Id);
        Assert.Equal((3u, 0u), (b0.Positive, b0.Negative));
        Assert.Throws<ZxvRefusedException>(() => hold.Add(new byte[32]));

        // special: frontier record, drafts and reviews
        JsonElement sp = P.GetProperty("special");
        byte[] fsig = ParadoxEngine.Signature(Encoding.UTF8.GetBytes(V.Str(sp, "sig_utf8")));
        Assert.Equal(V.Str(sp, "sig"), V.ToHex(fsig));
        ParadoxFrontierResult fr = book.Special((ParadoxKind)sp.GetProperty("kind").GetInt32(), Tag(sp.GetProperty("tag")), fsig, V.Hex(sp.GetProperty("subject")));
        Assert.Equal(new ParadoxFrontierResult((ParadoxOutcome)sp.GetProperty("outcome").GetInt32(), sp.GetProperty("rec").GetInt64()), fr);
        JsonElement dj = sp.GetProperty("draft"), dm = sp.GetProperty("draft_money");
        ParadoxDraft draft = book.Draft(fr.FrontierRecord, (ParadoxKind)dj.GetProperty("kind").GetInt32(), (byte)dj.GetProperty("reason").GetInt32(), (ParadoxConcerns)dj.GetProperty("flags").GetInt32());
        ParadoxDraft draftMoney = book.Draft(fr.FrontierRecord, (ParadoxKind)dm.GetProperty("kind").GetInt32(), (byte)dm.GetProperty("reason").GetInt32(), (ParadoxConcerns)dm.GetProperty("flags").GetInt32());
        Assert.Equal(V.Str(dj, "bytes"), draft.ToString());
        Assert.Equal(V.Str(dm, "bytes"), draftMoney.ToString());
        Assert.Equal(fsig, draft.Signature.ToArray());
        Assert.Equal(ParadoxConcerns.Money, draftMoney.Concerns);
        foreach (JsonElement rv in sp.GetProperty("review").EnumerateArray())
        {
            ParadoxDraft which = V.Str(rv, "draft") == "draft_money" ? draftMoney : draft;
            ParadoxOutcome o = default;
            V.Expect(rv, () => o = book.Review(which, rv.GetProperty("owner_ok").GetBoolean(), rv.GetProperty("consensus_ok").GetBoolean()));
            Assert.Equal(rv.GetProperty("outcome").GetInt32(), (int)o);
        }

        Assert.Equal(sp.GetProperty("registry_count_after").GetInt32(), book.RegistryCount);

        // graphs
        foreach (JsonElement g in P.GetProperty("graphs").EnumerateArray())
        {
            using ParadoxGraph graph = _p.CreateParadoxGraph(cap, g.GetProperty("step_limit").GetInt32());
            foreach (JsonElement node in g.GetProperty("nodes").EnumerateArray())
            {
                V.Expect(node, () => graph.AddNode(Id(V.Str(node, "id")), (ParadoxKind)node.GetProperty("kind").GetInt32(), Tag(node.GetProperty("tag")), node.GetProperty("mag").GetUInt32(), node.GetProperty("dir").GetInt32()));
            }

            foreach (JsonElement dep in g.GetProperty("deps").EnumerateArray())
            {
                V.Expect(dep, () => graph.AddDependency(Id(V.Str(dep, "id")), Id(V.Str(dep, "dep"))));
            }

            foreach (JsonElement pass in g.GetProperty("passes").EnumerateArray())
            {
                ParadoxOutcome o = default;
                if (V.Expect(pass, () => o = book.Pass(graph)))
                {
                    Assert.Equal(pass.GetProperty("outcome").GetInt32(), (int)o);
                }

                ParadoxGraphInfo info = graph.Info;
                Assert.Equal(pass.GetProperty("pass").GetInt32(), info.Passes);
                Assert.Equal(pass.GetProperty("total_mag").GetUInt32(), info.TotalMagnitude);
                Assert.Equal(pass.GetProperty("total_dir").GetInt32(), info.TotalDirection);
                Assert.Equal(pass.GetProperty("escalated").GetBoolean(), info.Escalated);
                Assert.Equal(V.Str(pass, "chain"), V.ToHex(info.Chain));
                foreach (JsonProperty s in pass.GetProperty("states").EnumerateObject())
                {
                    Assert.Equal(s.Value.GetInt32(), (int)graph.StateOf(Id(s.Name)));
                }
            }

            Assert.Equal((byte)V.Str(g.GetProperty("nodes")[0], "id")[0], graph.NodeAt(0).Id[0]);
        }

        Heads(book, P.GetProperty("heads_final"));
        JsonElement logs = P.GetProperty("logs");
        foreach ((string name, ParadoxLog log) in new[] { ("ops", ParadoxLog.Operator), ("frontier", ParadoxLog.Frontier), ("quarantine", ParadoxLog.Quarantine) })
        {
            string[] want = logs.GetProperty(name).EnumerateArray().Select(e => e.GetString()!).ToArray();
            Assert.Equal(want, Enumerable.Range(0, book.LogCount(log)).Select(i => V.ToHex(book.LogRecord(log, i))).ToArray());
        }

        foreach ((string status, ParadoxLog log) in P.GetProperty("log_verify").EnumerateArray().Select((e, i) => (e.GetString()!, (ParadoxLog)i)))
        {
            V.Expect(status, () => book.VerifyLog(log));
        }
    }

    [Fact]
    public void Owner_and_step_statuses_are_refusals()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using ParadoxBook book = _p.CreateParadoxBook(cap, 8, 4);
        var owner = Assert.Throws<ZxvRefusedException>(() => book.RegistryAdd(new byte[32], ParadoxKind.Neutral, 0, ParadoxConcerns.Device, ownerApproved: false));
        Assert.Equal(ZxvStatus.ParadoxOwnerApproval, owner.Status);

        using ParadoxGraph g = _p.CreateParadoxGraph(cap, 1);
        g.AddNode(Id("S"), ParadoxKind.Special, Hexagram.FromTag(36), 1, 0);
        var step = Assert.Throws<ZxvRefusedException>(() => book.Pass(g));
        Assert.Equal(ZxvStatus.ParadoxStepLimit, step.Status);
        Assert.True(g.Info.Escalated);
    }

    [Fact]
    public void Paradox_needs_its_right_and_handles_fail_closed()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using Capability noParadox = cap.Attenuate(CapabilityRights.AllAbi15 & ~CapabilityRights.Paradox);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateParadoxBook(noParadox, 8, 4));
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateParadoxHold(noParadox, 2, 1));
        using Capability legacy = _p.CreateRootCapability();
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateParadoxGraph(legacy, 1));

        ParadoxHold h = _p.CreateParadoxHold(cap, 2, 1);
        h.Dispose();
        Assert.Throws<ObjectDisposedException>(() => h.Info);
        using ParadoxBook book = _p.CreateParadoxBook(cap, 8, 4);
        Assert.Throws<ObjectDisposedException>(() => book.Neutral(h, ParadoxKind.Neutral, Hexagram.FromTag(39)));

        _p.Halt("test");
        Assert.Throws<ZxvFatalException>(() => book.LogCount(ParadoxLog.Operator));
        Assert.Throws<ZxvFatalException>(() => book.Classify(Hexagram.FromTag(39)));
    }
}
