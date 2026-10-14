// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using System.Text.Json;
using Zxv.Aipi;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 CID and AIPI groups against <c>vectors.json</c> "cid" and "aipi".</summary>
public sealed class CidAipiTests : IDisposable
{
    private static readonly JsonElement C = V.Group("cid");
    private static readonly JsonElement A = V.Group("aipi");

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Compute_vectors_match_kubo()
    {
        foreach (JsonElement v in C.GetProperty("compute").EnumerateArray())
        {
            Cid cid = Cid.Compute(V.Utf8(v.GetProperty("utf8")), v.GetProperty("codec").GetUInt32());
            Assert.Equal(V.Str(v, "cid"), V.ToHex(cid.Bytes));
            Assert.Equal(V.Str(v, "text_default"), cid.ToString());
            Assert.Equal(V.Str(v, "text_58btc"), cid.ToString(CidBase.Base58Btc));
            CidInfo info = cid.Info;
            Assert.Equal(v.GetProperty("version").GetUInt32(), info.Version);
            Assert.Equal(v.GetProperty("hash").GetUInt32(), info.HashFunction);
            Assert.Equal(V.Str(v, "digest"), V.ToHex(info.Digest.Span));
            Assert.True(cid.Matches(V.Utf8(v.GetProperty("utf8"))));
        }
    }

    [Fact]
    public void Unixfs_vectors_match_ipfs_add()
    {
        foreach (JsonElement v in C.GetProperty("unixfs").EnumerateArray())
        {
            byte[] data = V.Has(v, "ramp") ? V.Ramp(v.GetProperty("len").GetInt32()) : V.Utf8(v.GetProperty("utf8"));
            Cid cid = Cid.ForFile(data);
            Assert.Equal(V.Str(v, "cid"), V.ToHex(cid.Bytes));
            Assert.Equal(V.Str(v, "text_default"), cid.ToString());
            if (V.Has(v, "codec"))
            {
                Assert.Equal(v.GetProperty("codec").GetUInt32(), cid.Info.Codec);
            }
        }
    }

    [Fact]
    public void Text_vectors_parse_convert_and_key()
    {
        foreach (JsonElement v in C.GetProperty("text").EnumerateArray())
        {
            Cid cid = Cid.Parse(V.Str(v, "text"));
            Assert.Equal(V.Str(v, "cid"), V.ToHex(cid.Bytes));
            Assert.Equal(V.Str(v, "text_default"), cid.ToString());
            string b58 = V.Str(v, "text_58btc");
            if (b58.StartsWith("ZXV_", StringComparison.Ordinal))
            {
                Assert.Equal(b58, V.StatusOf(() => cid.ToString(CidBase.Base58Btc)));
            }
            else
            {
                Assert.Equal(b58, cid.ToString(CidBase.Base58Btc));
            }

            CidInfo info = cid.Info;
            Assert.Equal(v.GetProperty("version").GetUInt32(), info.Version);
            Assert.Equal(v.GetProperty("codec").GetUInt32(), info.Codec);
            Assert.Equal(v.GetProperty("hash").GetUInt32(), info.HashFunction);
            Assert.Equal(V.Str(v, "digest"), V.ToHex(info.Digest.Span));
            Cid v1 = cid.ToV1();
            Assert.Equal(V.Str(v, "v1"), V.ToHex(v1.Bytes));
            Assert.Equal(V.Str(v, "v1_text"), v1.ToString());
            Assert.Equal(V.Str(v, "provider_key"), V.ToHex(cid.ProviderKey()));
            Assert.Equal(cid, Cid.FromBytes(V.Hex(v.GetProperty("cid"))));
        }
    }

    [Fact]
    public void Text_rejects_and_verify_vectors()
    {
        foreach (JsonElement v in C.GetProperty("text_reject").EnumerateArray())
        {
            string text = V.Str(v, "text");
            V.Expect(v, () => Cid.Parse(text));
            Assert.False(Cid.TryParse(text, out _));
        }

        foreach (JsonElement v in C.GetProperty("verify").EnumerateArray())
        {
            byte[] raw = V.Hex(v.GetProperty("cid"));
            bool expectOk = V.Str(v, "status") == "ZXV_OK";
            string got = V.StatusOf(() =>
            {
                Cid cid = Cid.FromBytes(raw);
                if (!cid.Matches(V.Utf8(v.GetProperty("utf8"))))
                {
                    throw new ZxvRefusedException(ZxvStatus.CidMismatch, "verify", "mismatch");
                }
            });
            Assert.Equal(V.Str(v, "status"), got);
            Assert.Equal(expectOk, got == "ZXV_OK");
        }

        var e = Assert.Throws<ZxvRefusedException>(() => Cid.Parse("z"));
        Assert.Equal(ZxvStatus.CidMismatch, e.Status);
    }

    [Fact]
    public void Function_records_encode_decode_and_name_themselves()
    {
        Cid module = Cid.FromBytes(V.Hex(A.GetProperty("module_cid")));
        Cid contract = Cid.FromBytes(V.Hex(A.GetProperty("contract_cid")));
        Assert.Equal(module, Cid.Compute(Encoding.UTF8.GetBytes(V.Str(A, "module_utf8"))));
        Assert.Equal(contract, Cid.Compute(Encoding.UTF8.GetBytes(V.Str(A, "contract_utf8"))));

        foreach (JsonElement v in A.GetProperty("fn").EnumerateArray())
        {
            AipiFunction? fn = null;
            bool ok = V.Expect(v, () => fn = AipiFunction.Encode(
                (AipiIntent)v.GetProperty("intent").GetInt32(),
                (AipiVerification)v.GetProperty("verify").GetInt32(),
                (AipiPrivacy)v.GetProperty("privacy").GetInt32(),
                v.GetProperty("determinism").GetInt32() != 0,
                V.U64(v.GetProperty("fuel_max")),
                module,
                V.Str(v, "entry"),
                V.Has(v, "checker") ? V.Str(v, "checker") : null,
                V.Has(v, "certifier") ? V.Str(v, "certifier") : null,
                v.GetProperty("contract").GetBoolean() ? contract : null,
                v.GetProperty("quorum_n").GetInt32(),
                v.GetProperty("quorum_k").GetInt32()));
            if (!ok)
            {
                continue;
            }

            Assert.Equal(V.Str(v, "record"), V.ToHex(fn!.Record));
            Assert.Equal(V.Str(v, "fn_cid"), V.ToHex(fn.FunctionCid.Bytes));
            Assert.Equal(V.Str(v, "fn_cid_text"), fn.FunctionCid.ToString(CidBase.Base58Btc));
            AipiFunction back = AipiFunction.Decode(V.Hex(v.GetProperty("record")));
            Assert.Equal(V.Str(v, "entry"), back.Entry);
            Assert.Equal(module, back.Module);
            Assert.Equal(V.U64(v.GetProperty("fuel_max")), back.FuelMax);
            Assert.Equal(v.GetProperty("contract").GetBoolean(), back.Contract.HasValue);
        }
    }

    [Fact]
    public void Call_and_claim_records()
    {
        Cid fn0 = Cid.FromBytes(V.Hex(A.GetProperty("fn")[0].GetProperty("fn_cid")));
        Cid module = Cid.FromBytes(V.Hex(A.GetProperty("module_cid")));
        foreach (JsonElement v in A.GetProperty("call").EnumerateArray())
        {
            Cid f = V.Str(v, "fn") == "module_cid" ? module : fn0;
            byte[] input = V.Utf8(v.GetProperty("input_utf8"));
            if (V.Has(v, "status"))
            {
                V.Expect(v, () => AipiCall.Encode(f, input));
                continue;
            }

            Assert.Equal(V.Str(v, "record"), V.ToHex(AipiCall.Encode(f, input)));
            Cid callCid = AipiCall.ComputeCid(f, input);
            Assert.Equal(V.Str(v, "call_cid"), V.ToHex(callCid.Bytes));
            Assert.Equal(V.Str(v, "call_cid_text"), callCid.ToString(CidBase.Base58Btc));
            AipiCallParts parts = AipiCall.Decode(V.Hex(v.GetProperty("record")));
            Assert.Equal(f, parts.Function);
            Assert.Equal(input, parts.Input.ToArray());
        }

        Cid call1 = Cid.FromBytes(V.Hex(A.GetProperty("call")[1].GetProperty("call_cid")));
        foreach (JsonElement v in A.GetProperty("claim").EnumerateArray())
        {
            byte[] exec = V.Hex(v.GetProperty("executor"));
            byte[] output = V.Utf8(v.GetProperty("output_utf8"));
            byte[] cert = V.Utf8(v.GetProperty("cert_utf8"));
            byte[] rec = AipiClaim.Encode(call1, exec, output, cert);
            Assert.Equal(V.Str(v, "record"), V.ToHex(rec));
            AipiClaimParts parts = AipiClaim.Decode(rec);
            Assert.Equal(call1, parts.Call);
            Assert.Equal(exec, parts.Executor.ToArray());
            Assert.Equal(output, parts.Output.ToArray());
            Assert.Equal(cert, parts.Certificate.ToArray());
        }
    }

    [Fact]
    public void Node_stores_verifies_and_plans_offline()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using AipiNode node = _p.CreateAipiNode(cap, 1 << 20, 64, new byte[32]);
        byte[] module = Encoding.UTF8.GetBytes(V.Str(A, "module_utf8"));
        Cid m = node.Put(module);
        Assert.Equal(V.Str(A, "module_cid"), V.ToHex(m.Bytes));
        Assert.Equal(module, node.Get(m));
        Assert.Equal(m, node.Put(module));

        AipiFunction fn = AipiFunction.Decode(V.Hex(A.GetProperty("fn")[0].GetProperty("record")));
        Cid fc = node.PublishFunction(fn);
        Assert.Equal(fn.FunctionCid, fc);
        Assert.Equal(V.Str(A.GetProperty("fn")[0], "record"), V.ToHex(node.LoadFunction(fc).Record));
        Assert.Equal(AipiRoute.Local, node.Plan(fc, "hi"u8));

        Cid missing = Cid.Compute("never stored"u8);
        Assert.False(node.TryGet(missing, out _));
        var nf = Assert.Throws<ZxvUsageException>(() => node.Get(missing));
        Assert.Equal(ZxvStatus.NotFound, nf.Status);

        // Private bytes: the same bytes cannot also be made public as a side effect.
        Cid secret = node.Put("private"u8, isPrivate: true);
        Assert.Equal("private"u8.ToArray(), node.Get(secret));
        var vis = Assert.Throws<ZxvRefusedException>(() => node.Put("private"u8));
        Assert.Equal(ZxvStatus.InvalidState, vis.Status);

        AipiNodeInfo info = node.Info;
        Assert.True(info.Blocks >= 3);
        Assert.True(info.VerifiedGets >= 3);
        Assert.Equal(0u, info.ForbiddenGets);
    }

    [Fact]
    public void Node_needs_the_aipi_right_and_fails_closed()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using AipiNode node = _p.CreateAipiNode(cap, 1 << 16, 8);
        using Capability noAipi = cap.Attenuate(CapabilityRights.AllAbi15 & ~CapabilityRights.Aipi);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateAipiNode(noAipi, 1 << 16, 8));

        // The default root (the ABI 1.1 rights) does not include AIPI either.
        using Capability legacy = _p.CreateRootCapability();
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateAipiNode(legacy, 1 << 16, 8));

        _p.Halt("test");
        Assert.Throws<ZxvFatalException>(() => node.Get(Cid.Compute("x"u8)));
    }

    [Fact]
    public void Disposed_node_throws()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        AipiNode node = _p.CreateAipiNode(cap, 1 << 16, 8);
        Cid c = node.Put("x"u8);
        node.Dispose();
        Assert.Throws<ObjectDisposedException>(() => node.Get(c));
        Assert.Throws<ObjectDisposedException>(() => node.Put("x"u8));
        node.Dispose();
    }
}
