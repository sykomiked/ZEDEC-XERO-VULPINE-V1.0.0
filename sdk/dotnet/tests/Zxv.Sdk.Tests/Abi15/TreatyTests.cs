// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Treaties;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 treaty group against <c>vectors.json</c> "treaty".</summary>
public sealed class TreatyTests
{
    private static readonly JsonElement T = V.Group("treaty");

    [Fact]
    public void Lengths_match_the_copybooks()
    {
        Assert.Equal(Treaty.RecordLength, T.GetProperty("rec_len").GetInt32());
        Assert.Equal(Treaty.ResultRecordLength, T.GetProperty("result_len").GetInt32());
    }

    [Fact]
    public void Sutra_vectors_parse_emit_and_round_trip()
    {
        foreach (JsonElement v in T.GetProperty("sutra").EnumerateArray())
        {
            using Treaty t = Treaty.ParseSutra(V.Str(v, "sutra"));
            Assert.Equal(TreatyFault.None, t.Validate());
            Assert.Equal(V.Str(v, "record"), V.ToHex(t.ToRecord()));
            Assert.Equal(V.Str(v, "reemit"), t.ToSutra());
            Assert.Equal(V.Str(v, "name"), t.Name);
            Assert.Equal(V.Str(v, "asset"), t.Asset);
            Assert.Equal(v.GetProperty("op").GetInt32(), (int)t.Operation);
            Assert.Equal(v.GetProperty("party_count").GetInt32(), t.PartyCount);
            Assert.Equal(v.GetProperty("owe_count").GetInt32(), t.ObligationCount);

            JsonElement[] parties = v.GetProperty("parties").EnumerateArray().ToArray();
            for (int i = 0; i < parties.Length; i++)
            {
                TreatyParty p = t.GetParty(i);
                Assert.Equal(V.Str(parties[i], "id"), p.Id);
                Assert.Equal(V.I64(parties[i].GetProperty("deposit")), p.Deposit);
                Assert.Equal(V.I64(parties[i].GetProperty("claim")), p.Claim);
                Assert.Equal(parties[i].GetProperty("weight")[0].GetUInt32(), p.WeightNumerator);
                Assert.Equal(parties[i].GetProperty("weight")[1].GetUInt32(), p.WeightDenominator);
            }

            JsonElement[] owes = v.GetProperty("owes").EnumerateArray().ToArray();
            Assert.Equal(owes.Length, t.Obligations.Count);
            for (int i = 0; i < owes.Length; i++)
            {
                Assert.Equal(new TreatyObligation(owes[i].GetProperty("from").GetInt32(), owes[i].GetProperty("to").GetInt32(), V.I64(owes[i].GetProperty("amount"))), t.GetObligation(i));
            }

            using Treaty fromRecord = Treaty.ParseRecord(V.Hex(v.GetProperty("record")));
            Assert.Equal(v.GetProperty("record_parses_equal").GetBoolean(), fromRecord.ContentEquals(t));
        }
    }

    [Fact]
    public void Rejected_sutra_reports_fault_and_offset()
    {
        foreach (JsonElement v in T.GetProperty("reject").EnumerateArray())
        {
            var e = Assert.Throws<TreatyException>(() => Treaty.ParseSutra(V.Str(v, "sutra")).Dispose());
            Assert.Equal(V.Status(V.Str(v, "status")), e.Status);
            Assert.Equal(v.GetProperty("fault").GetInt32(), (int)e.Fault);
            Assert.Equal(v.GetProperty("offset").GetInt32(), e.FaultOffset);
            Assert.Equal(ZxvStatusClass.Usage, ZxvLibrary.ClassOf(e.Status));
        }
    }

    [Fact]
    public void Rejected_records_report_fault_and_offset()
    {
        byte[] base0 = V.Hex(T.GetProperty("sutra")[0].GetProperty("record"));
        foreach (JsonElement v in T.GetProperty("reject_record").EnumerateArray())
        {
            Assert.Equal("sutra[0].record", V.Str(v, "base"));
            byte[] rec = (byte[])base0.Clone();
            rec[v.GetProperty("patch_offset").GetInt32()] = V.Hex(v.GetProperty("patch_byte"))[0];
            using var t = new Treaty();
            t.LoadSutra(T.GetProperty("sutra")[0].GetProperty("sutra").GetString()!);
            var e = Assert.Throws<TreatyException>(() => t.LoadRecord(rec));
            Assert.Equal(v.GetProperty("fault").GetInt32(), (int)e.Fault);
            Assert.Equal(v.GetProperty("offset").GetInt32(), e.FaultOffset);

            // On failure the treaty is unchanged.
            Assert.Equal(V.ToHex(base0), V.ToHex(t.ToRecord()));
        }
    }

    [Fact]
    public void Pic18_arithmetic_matches()
    {
        foreach (JsonElement v in T.GetProperty("pic").EnumerateArray())
        {
            long a = V.I64(v.GetProperty("a"));
            long b = V.I64(v.GetProperty("b"));
            long got = 0, rem = 0;
            bool ok = V.Expect(v, () =>
            {
                switch (V.Str(v, "op"))
                {
                    case "add": got = Pic18.Add(a, b); break;
                    case "sub": got = Pic18.Subtract(a, b); break;
                    case "mul": got = Pic18.Multiply(a, b); break;
                    default: (got, rem) = Pic18.DivRem(a, b); break;
                }
            });
            if (ok)
            {
                Assert.Equal(V.I64(v.GetProperty("out")), got);
                if (V.Has(v, "rem"))
                {
                    Assert.Equal(V.I64(v.GetProperty("rem")), rem);
                }
            }
        }
    }

    [Fact]
    public void Run_vectors_net_settle_and_refuse()
    {
        foreach (JsonElement v in T.GetProperty("run").EnumerateArray())
        {
            using Treaty t = Treaty.ParseSutra(V.Str(v, "sutra"));
            TreatyResult? r = null;
            bool ok = V.Expect(v, () => r = t.Run());
            if (!ok)
            {
                Assert.Throws<InsufficientFundsException>(() => t.Run());
                continue;
            }

            using TreatyResult result = r!;
            Assert.Equal(v.GetProperty("parties").GetInt32(), result.PartyCount);
            Assert.Equal(v.GetProperty("legs").GetInt32(), result.LegCount);
            Assert.Equal(V.I64(v.GetProperty("fees")), result.TotalFees);
            JsonElement[] party = v.GetProperty("party").EnumerateArray().ToArray();
            for (int i = 0; i < party.Length; i++)
            {
                Assert.Equal(new TreatyResultParty(V.I64(party[i].GetProperty("net")), V.I64(party[i].GetProperty("balance")), V.I64(party[i].GetProperty("dividend"))), result.Parties[i]);
            }

            JsonElement[] leg = v.GetProperty("leg").EnumerateArray().ToArray();
            for (int i = 0; i < leg.Length; i++)
            {
                Assert.Equal(new TreatyLeg(leg[i].GetProperty("from").GetInt32(), leg[i].GetProperty("to").GetInt32(), V.I64(leg[i].GetProperty("amount"))), result.Legs[i]);
            }

            Assert.Equal(V.Str(v, "result_record"), V.ToHex(result.ToRecord()));
        }
    }

    [Fact]
    public void Empty_treaty_validates_as_name_fault_and_refuses_to_emit()
    {
        using var t = new Treaty();
        Assert.Equal(TreatyFault.Name, t.Validate());
        var e = Assert.Throws<TreatyException>(() => t.ToSutra());
        Assert.Equal(ZxvStatus.TreatyInvalid, e.Status);
        Assert.Equal(TreatyFault.Name, e.Fault);
    }

    [Fact]
    public void Use_after_dispose_throws_and_results_outlive_their_treaty()
    {
        var t = Treaty.ParseSutra(V.Str(T.GetProperty("run")[2], "sutra"));
        TreatyResult r = t.Run();
        t.Dispose();
        Assert.Throws<ObjectDisposedException>(() => t.ToSutra());
        Assert.Equal(2, r.PartyCount);
        r.Dispose();
        Assert.Throws<ObjectDisposedException>(() => r.PartyCount);
        t.Dispose();
        r.Dispose();
    }
}
