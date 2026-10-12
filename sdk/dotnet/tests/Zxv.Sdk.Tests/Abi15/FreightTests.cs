// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text;
using System.Text.Json;
using Zxv.Freight;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 freight group against <c>vectors.json</c> "freight".</summary>
public sealed class FreightTests
{
    private static readonly JsonElement F = V.Group("freight");

    [Fact]
    public void Header_serialize_and_parse()
    {
        foreach (JsonElement v in F.GetProperty("header_serialize").EnumerateArray())
        {
            FreightHeader h = default;
            bool ok = V.Expect(v, () => h = FreightHeader.FromFields(
                (byte)v.GetProperty("k").GetInt32(),
                (FreightOptions)v.GetProperty("flags").GetInt32(),
                (ushort)v.GetProperty("payload_len").GetInt32(),
                (ushort)v.GetProperty("seq").GetInt32(),
                (ushort)v.GetProperty("seq_count").GetInt32(),
                V.U64(v.GetProperty("freight_id")),
                V.U64(v.GetProperty("expand_limit")),
                V.Hex(v.GetProperty("payload_sha256"))));
            if (!ok)
            {
                continue;
            }

            Assert.Equal(V.Str(v, "header"), h.ToString());
            FreightHeader p = FreightHeader.Parse(V.Hex(v.GetProperty("header")));
            Assert.Equal(h, p);
            Assert.Equal(1, p.Version);
            Assert.Equal(v.GetProperty("k").GetInt32(), p.K);
            Assert.Equal(v.GetProperty("payload_len").GetInt32(), p.PayloadLength);
            Assert.Equal(v.GetProperty("seq").GetInt32(), p.Sequence);
            Assert.Equal(v.GetProperty("seq_count").GetInt32(), p.SequenceCount);
            Assert.Equal(V.U64(v.GetProperty("freight_id")), p.FreightId);
            Assert.Equal(V.U64(v.GetProperty("expand_limit")), p.ExpandLimit);
            Assert.Equal(V.Str(v, "payload_sha256"), V.ToHex(p.PayloadSha256));
            Assert.True(FreightHeader.IsValid(p.Bytes));
        }

        byte[] bad = new byte[64];
        Assert.False(FreightHeader.IsValid(bad));
        var e = Assert.Throws<ZxvUsageException>(() => FreightHeader.Parse(bad));
        Assert.Equal(ZxvStatus.InvalidFormat, e.Status);
    }

    [Fact]
    public void Codec_encodes_and_any_k_rows_decode()
    {
        foreach (JsonElement v in F.GetProperty("codec").EnumerateArray())
        {
            byte[] payload = V.Hex(v.GetProperty("payload"));
            FreightHeader h = FreightHeader.Create(V.U64(v.GetProperty("freight_id")), (byte)v.GetProperty("k").GetInt32(), FreightOptions.None, 0, payload);
            Assert.Equal(V.Str(v, "header"), h.ToString());
            byte[] packets = FreightCodec.Encode(h, payload);
            Assert.Equal(V.Str(v, "packets"), V.ToHex(packets));

            foreach (JsonElement d in v.GetProperty("decode").EnumerateArray())
            {
                int[] rows = Abi11.Conformance.Ints(d.GetProperty("rows"));
                byte[] picked = rows.SelectMany(r => packets.AsSpan(r * FreightCodec.PacketBytes, FreightCodec.PacketBytes).ToArray()).ToArray();
                if (d.TryGetProperty("flip", out JsonElement flip))
                {
                    picked[(flip.GetProperty("index_in_rows").GetInt32() * FreightCodec.PacketBytes) + flip.GetProperty("byte").GetInt32()] ^= V.Hex(flip.GetProperty("xor"))[0];
                }

                byte[] got = Array.Empty<byte>();
                if (V.Expect(d, () => got = FreightCodec.Decode(h, picked)))
                {
                    Assert.Equal(d.GetProperty("matches_payload").GetBoolean(), got.AsSpan().SequenceEqual(payload));
                }
            }
        }

        // Status classes: too few rows is REFUSED (ZXV_E_ERASURE); a corrupted row is a verification failure.
        JsonElement c0 = F.GetProperty("codec")[0];
        FreightHeader h0 = FreightHeader.Parse(V.Hex(c0.GetProperty("header")));
        var er = Assert.Throws<ZxvRefusedException>(() => FreightCodec.Decode(h0, Array.Empty<byte>()));
        Assert.Equal(ZxvStatus.ErasureTooFewRows, er.Status);
    }

    [Fact]
    public void Header_init_rejects_set_counts_and_split()
    {
        foreach (JsonElement v in F.GetProperty("header_init_reject").EnumerateArray())
        {
            ulong limit = V.Has(v, "expand_limit") ? V.U64(v.GetProperty("expand_limit")) : 0;
            byte[] payload = new byte[v.GetProperty("len").GetInt32()];
            V.Expect(v, () => FreightHeader.Create(1, (byte)v.GetProperty("k").GetInt32(), FreightOptions.None, limit, payload));
        }

        foreach (JsonElement v in F.GetProperty("set_count").EnumerateArray())
        {
            int count = 0;
            if (V.Expect(v, () => count = FreightCodec.SetCount((long)V.U64(v.GetProperty("len")), (byte)v.GetProperty("k").GetInt32())))
            {
                Assert.Equal(v.GetProperty("count").GetInt32(), count);
            }
        }

        JsonElement s = F.GetProperty("split");
        byte[] data = V.Ramp(s.GetProperty("len").GetInt32());
        byte k = (byte)s.GetProperty("k").GetInt32();
        ulong id = V.U64(s.GetProperty("freight_id"));
        int members = FreightCodec.SetCount(data.Length, k);
        using var join = new FreightJoin(data.Length);
        foreach (JsonElement m in s.GetProperty("members").EnumerateArray().Reverse())
        {
            FreightMember member = null!;
            if (!V.Expect(m, () => member = FreightCodec.Split(id, k, FreightOptions.None, 0, data, m.GetProperty("seq").GetInt32())))
            {
                continue;
            }

            Assert.Equal(V.Str(m, "header"), member.Header.ToString());
            Assert.Equal(V.Str(m, "packets_sha3_256"), V.ToHex(Hashes.Sha3256(member.Packets)));

            // Only the last k rows of each member: the join still rebuilds it.
            join.Add(member.Header, member.Packets.AsSpan((FreightCodec.Rows - k) * FreightCodec.PacketBytes).ToArray());
            join.Add(member.Header, member.Packets);
        }

        FreightJoinStatus st = join.Status;
        Assert.Equal(new FreightJoinStatus(members, members, true, data.Length), st);
        Assert.Equal(data, join.GetResult());
    }

    [Fact]
    public void Join_refuses_result_before_complete_and_after_dispose()
    {
        var join = new FreightJoin(100);
        var e = Assert.Throws<ZxvRefusedException>(() => join.GetResult());
        Assert.Equal(ZxvStatus.InvalidState, e.Status);
        join.Dispose();
        Assert.Throws<ObjectDisposedException>(() => join.Status);
        join.Dispose();
    }

    [Fact]
    public void Op_streams_encode_bound_expand_and_generate()
    {
        foreach (JsonElement v in F.GetProperty("ops_encode").EnumerateArray())
        {
            byte[] input = V.Utf8(v.GetProperty("utf8"));
            bool sha = v.GetProperty("with_sha").GetBoolean();
            byte[] ops = FreightOps.Encode(input, sha);
            Assert.Equal(V.Str(v, "ops"), V.ToHex(ops));
            Assert.Equal(V.U64(v.GetProperty("bound")), FreightOps.Bound((ulong)input.Length, sha));
            byte[] back = Array.Empty<byte>();
            V.Expect(V.Str(v, "expand_status"), () => back = FreightOps.Expand(ops, FreightOps.MaxBytes));
            Assert.Equal(input, back);
            Assert.Equal(new FreightOpsInfo((ulong)input.Length, sha), FreightOps.Peek(ops));
        }

        foreach (JsonElement v in F.GetProperty("gen_run").EnumerateArray())
        {
            byte[] got = Array.Empty<byte>();
            if (V.Expect(v, () => got = FreightOps.RunGenerator((FreightGenerator)v.GetProperty("gen").GetInt32(), V.Hex(v.GetProperty("params")), v.GetProperty("len").GetInt32())))
            {
                Assert.Equal(V.Str(v, "out"), V.ToHex(got));
            }
        }
    }

    [Fact]
    public void Op_stream_writer_and_cidref_resolver()
    {
        JsonElement w0 = F.GetProperty("ops_writer")[0];
        using (var w = new FreightOpsWriter(V.U64(w0.GetProperty("declared_total")), default, FreightOps.MaxBytes))
        {
            foreach (JsonElement op in w0.GetProperty("ops_list").EnumerateArray())
            {
                V.Expect(op, () =>
                {
                    switch (V.Str(op, "op"))
                    {
                        case "literal": w.PutLiteral(V.Utf8(op.GetProperty("utf8"))); break;
                        case "copy": w.PutCopy(V.U64(op.GetProperty("distance")), V.U64(op.GetProperty("len"))); break;
                        default: w.PutSeed((FreightGenerator)op.GetProperty("gen").GetInt32(), V.Hex(op.GetProperty("params")), V.U64(op.GetProperty("len"))); break;
                    }
                });
            }

            byte[] ops = Array.Empty<byte>();
            V.Expect(V.Str(w0, "finish_status"), () => ops = w.Finish());
            Assert.Equal(V.Str(w0, "ops"), V.ToHex(ops));
            Assert.Equal(new FreightOpsInfo(V.U64(w0.GetProperty("peek_total")), w0.GetProperty("peek_has_sha").GetBoolean()), FreightOps.Peek(ops));
            Assert.Equal(V.Str(w0, "expanded"), V.ToHex(FreightOps.Expand(ops, FreightOps.MaxBytes)));
            ulong total = V.U64(w0.GetProperty("peek_total"));
            V.Expect(V.Str(w0, "expand_limit_too_small"), () => FreightOps.Expand(ops, (long)total - 1));
            V.Expect(V.Str(w0, "expand_truncated"), () => FreightOps.Expand(ops.AsSpan(0, ops.Length - 1), FreightOps.MaxBytes));
        }

        JsonElement w1 = F.GetProperty("ops_writer")[1];
        byte[] cid = V.Hex(w1.GetProperty("ops_list")[1].GetProperty("cid"));
        byte[] resolved = V.Utf8(w1.GetProperty("ops_list")[1].GetProperty("resolves_to_utf8"));
        using (var w = new FreightOpsWriter(V.U64(w1.GetProperty("declared_total")), default, 1024))
        {
            w.PutLiteral("<"u8);
            w.PutCidRef(cid, (ulong)resolved.Length);
            w.PutLiteral(">"u8);
            byte[] ops = w.Finish();
            Assert.Equal(V.Str(w1, "ops"), V.ToHex(ops));
            V.Expect(V.Str(w1, "expand_without_resolver"), () => FreightOps.Expand(ops, 1024));

            int calls = 0;
            byte[] expanded = FreightOps.Expand(ops, 1024, (c, output) =>
            {
                calls++;
                Assert.Equal(cid, c.ToArray());
                resolved.CopyTo(output);
                return true;
            });
            Assert.Equal(V.Str(w1, "expanded_with_resolver_utf8"), Encoding.UTF8.GetString(expanded));
            Assert.True(calls >= 1);

            // A resolver that declines is NOT_FOUND; one that throws never unwinds through C: the exception comes back here.
            var nf = Assert.Throws<ZxvUsageException>(() => FreightOps.Expand(ops, 1024, (_, _) => false));
            Assert.Equal(ZxvStatus.NotFound, nf.Status);
            var boom = Assert.Throws<InvalidOperationException>(() => FreightOps.Expand(ops, 1024, (_, _) => throw new InvalidOperationException("resolver bug")));
            Assert.Equal("resolver bug", boom.Message);
        }

        // The first failing put is sticky.
        using var sticky = new FreightOpsWriter(4, default, 64);
        Assert.Throws<ZxvUsageException>(() => sticky.PutCopy(1, 1));
        Assert.Throws<ZxvUsageException>(() => sticky.PutLiteral("ab"u8));
        Assert.Throws<ZxvUsageException>(() => sticky.Finish());
    }
}
