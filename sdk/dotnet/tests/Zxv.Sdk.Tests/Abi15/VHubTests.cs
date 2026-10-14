// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text.Json;
using Zxv.Interop;
using Zxv.VHub;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 V-Bill hub against <c>vectors.json</c> "vhub", run in order on one hub.</summary>
public sealed class VHubTests : IDisposable
{
    private static readonly JsonElement H = V.Group("vhub");

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Peg_messages_are_byte_exact()
    {
        foreach (JsonElement v in H.GetProperty("peg_msg").EnumerateArray())
        {
            PegMessage m = PegMessage.Create((byte)v.GetProperty("slot").GetInt32(), V.U64(v.GetProperty("num")), V.U64(v.GetProperty("den")), V.U64(v.GetProperty("epoch")));
            Assert.Equal(V.Str(v, "msg"), m.ToString());
            Assert.Equal(PegMessage.Length, m.Bytes.Length);
        }
    }

    [Fact]
    public void Scenario_runs_in_order_with_the_same_results()
    {
        JsonElement s = H.GetProperty("scenario");
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        var dsa = new MlDsa87(cap);
        KeyPair[] keys = s.GetProperty("keys").EnumerateArray().Select(k => dsa.GenerateKey(V.Hex(k.GetProperty("seed")))).ToArray();
        try
        {
            for (int k = 0; k < keys.Length; k++)
            {
                Assert.Equal(V.Str(s.GetProperty("keys")[k], "pk_sha3_256"), V.ToHex(Hashes.Sha3256(keys[k].PublicKey)));
            }

            using VBillHub hub = _p.CreateVBillHub(cap, V.Hex(s.GetProperty("hub_seed")));
            foreach (JsonElement c in s.GetProperty("currencies").EnumerateArray())
            {
                byte slot = (byte)c.GetProperty("slot").GetInt32();
                int key = V.Has(c, "issuer_key") ? c.GetProperty("issuer_key").GetInt32() : 0;
                ulong pool = V.Has(c, "pool_funding") ? V.U64(c.GetProperty("pool_funding")) : 0;
                ulong backing = V.Has(c, "vbill_backing") ? V.U64(c.GetProperty("vbill_backing")) : 0;
                if (V.Expect(c, () => hub.AddCurrency(slot, V.Str(c, "code"), 2, keys[key].PublicKey, pool, backing)))
                {
                    VHubCurrencyInfo info = hub.GetCurrency(slot);
                    Assert.Equal(c.GetProperty("issuer_account").GetUInt32(), info.IssuerAccount);
                    Assert.Equal(c.GetProperty("pool_account").GetUInt32(), info.PoolAccount);
                    Assert.Equal(c.GetProperty("vfloat_account").GetUInt32(), info.VBillFloatAccount);
                    Assert.False(info.HasPeg);
                }
            }

            foreach (JsonElement g in s.GetProperty("pegs").EnumerateArray())
            {
                byte slot = (byte)g.GetProperty("slot").GetInt32();
                ulong num = V.U64(g.GetProperty("num")), den = V.U64(g.GetProperty("den")), epoch = V.U64(g.GetProperty("epoch"));
                byte[] sig = dsa.SignDeterministic(keys[g.GetProperty("signer").GetInt32()].SecretKey, PegMessage.Create(slot, num, den, epoch).ToArray());
                Assert.Equal(V.Str(g, "sig_sha3_256"), V.ToHex(Hashes.Sha3256(sig)));
                if (V.Expect(g, () => hub.SetPeg(slot, num, den, epoch, sig)))
                {
                    VHubCurrencyInfo info = hub.GetCurrency(slot);
                    Assert.Equal((num, den, epoch), (info.PegNumerator, info.PegDenominator, info.PegEpoch));
                }
            }

            foreach (JsonElement a in s.GetProperty("accounts").EnumerateArray())
            {
                uint acct = hub.OpenAccount((byte)a.GetProperty("slot").GetInt32(), a.GetProperty("owner").GetUInt32());
                Assert.Equal(a.GetProperty("account").GetUInt32(), acct);
                if (V.Has(a, "issued"))
                {
                    hub.Issue(acct, V.U64(a.GetProperty("issued")));
                }
            }

            foreach (JsonElement q in s.GetProperty("quotes").EnumerateArray())
            {
                VHubQuote quote = default;
                if (V.Expect(q, () => quote = hub.Quote((byte)q.GetProperty("from").GetInt32(), (byte)q.GetProperty("to").GetInt32(), V.U64(q.GetProperty("want")))))
                {
                    Assert.Equal(new VHubQuote(V.U64(q.GetProperty("amount")), V.U64(q.GetProperty("vbills")), V.U64(q.GetProperty("received"))), quote);
                }
            }

            foreach (JsonElement w in s.GetProperty("swaps").EnumerateArray())
            {
                if (w.TryGetProperty("add_limit", out JsonElement lim))
                {
                    hub.AddLimit((byte)lim.GetProperty("slot").GetInt32(), V.U64(lim.GetProperty("limit")));
                }

                VHubSwap r = default;
                if (V.Expect(w, () => r = hub.Swap((byte)w.GetProperty("from").GetInt32(), (byte)w.GetProperty("to").GetInt32(), w.GetProperty("sender").GetUInt32(), w.GetProperty("receiver").GetUInt32(), V.U64(w.GetProperty("amount")), V.U64(w.GetProperty("tick")))))
                {
                    Assert.Equal(new VHubSwap(V.U64(w.GetProperty("vbills")), V.U64(w.GetProperty("received"))), r);
                }
            }

            ulong postings = 0;
            V.Expect(V.Str(s, "check"), () => postings = hub.Check());
            Assert.Equal(V.U64(s.GetProperty("postings")), postings);
            foreach (JsonElement b in s.GetProperty("balances").EnumerateArray())
            {
                Assert.Equal(new VHubBalance(V.U64(b.GetProperty("debit")), V.U64(b.GetProperty("credit")), V.I64(b.GetProperty("equity"))), hub.GetBalance(b.GetProperty("account").GetUInt32()));
            }

            // Refusals map to their classes: a stale peg and a limit are REFUSED.
            var stale = Assert.Throws<ZxvRefusedException>(() => hub.SetPeg(0, 1, 1, 1, new byte[MlDsa87.SignatureBytes]));
            Assert.True(stale.Status is ZxvStatus.PegInvalid or ZxvStatus.VerificationFailed);
        }
        finally
        {
            foreach (KeyPair k in keys)
            {
                k.Dispose();
            }
        }
    }

    [Fact]
    public void Hub_needs_ledger_rights_and_fails_closed()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using Capability readOnly = cap.Attenuate(CapabilityRights.LedgerRead);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateVBillHub(readOnly));

        VBillHub hub = _p.CreateVBillHub(cap);
        Assert.Equal(1UL, hub.NextEpoch());
        hub.Dispose();
        Assert.Throws<ObjectDisposedException>(() => hub.NextEpoch());

        using VBillHub live = _p.CreateVBillHub(cap);
        _p.Halt("test");
        Assert.Throws<ZxvFatalException>(() => live.Check());
    }
}
