// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Web4;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 Web 4 agent layer against <c>vectors.json</c> "web4".</summary>
public sealed class Web4Tests : IDisposable
{
    private const int Sig = Web4Agent.SignatureBytes;
    private static readonly JsonElement W = V.Group("web4");

    private readonly ZxvPlatform _p = TestPlatform.Create();
    private readonly Capability _cap;
    private readonly Dictionary<string, Web4Agent> _agents = new();

    public Web4Tests()
    {
        _cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        foreach (JsonElement a in W.GetProperty("agents").EnumerateArray())
        {
            byte[] seed = Enumerable.Repeat(V.Hex(a.GetProperty("seed_byte"))[0], 32).ToArray();
            Web4Agent? node = V.Has(a, "node") ? _agents[V.Str(a, "node")] : null;
            _agents[V.Str(a, "name")] = _p.CreateWeb4Agent(_cap, V.Str(a, "name"), V.U64(a.GetProperty("now_ms")), seed, node);
        }
    }

    public void Dispose()
    {
        foreach (Web4Agent a in _agents.Values)
        {
            a.Dispose();
        }

        _cap.Dispose();
        _p.Dispose();
    }

    private static string Body(byte[] signed) => V.ToHex(signed.AsSpan(0, signed.Length - Sig));

    [Fact]
    public void Agents_ids_and_cards()
    {
        Assert.Equal(Sig, W.GetProperty("sig_bytes").GetInt32());
        foreach (JsonElement a in W.GetProperty("agents").EnumerateArray())
        {
            Web4Agent agent = _agents[V.Str(a, "name")];
            Assert.Equal(V.Str(a, "agent_id"), V.ToHex(agent.AgentId));
            Assert.Equal(V.Str(a, "peer_id"), V.ToHex(agent.PeerId));
            Assert.Equal(V.Str(a, "pk_sha3_256"), V.ToHex(Hashes.Sha3256(agent.PublicKey)));
            byte[] card = agent.Card;
            Assert.Equal(a.GetProperty("card_len").GetInt32(), card.Length);
            Assert.Equal(V.Str(a, "card_signed_body_sha3_256"), V.ToHex(Hashes.Sha3256(card.AsSpan(0, card.Length - Sig))));

            AgentCardInfo info = null!;
            V.Expect(V.Str(a, "card_verify"), () => info = AgentCard.Verify(card));
            Assert.Equal(V.Str(a, "name"), info.Name);
            Assert.Equal(agent.AgentId, info.AgentId);
            Assert.Equal(agent.PeerId, info.PeerId);
            Assert.Equal(V.U64(a.GetProperty("now_ms")), info.BoundMs);

            byte[] flipped = (byte[])card.Clone();
            flipped[^1] ^= 1;
            V.Expect(V.Str(a, "card_verify_flipped_last_byte"), () => AgentCard.Verify(flipped));
            Assert.Throws<VerificationFailedException>(() => AgentCard.Verify(flipped));

            // No member exposes the private key, and ToString shows only the public id.
            Assert.Contains(V.Str(a, "agent_id"), agent.ToString(), StringComparison.Ordinal);
        }
    }

    [Fact]
    public void Directory_holds_verified_cards()
    {
        using var dir = new AgentDirectory(8);
        foreach (Web4Agent a in _agents.Values)
        {
            dir.Add(a.Card);
        }

        Assert.Equal(_agents.Count, dir.Count);
        Web4Agent alice = _agents["alice"];
        Assert.Equal(alice.Card, dir.Find(alice.AgentId));
        Assert.False(dir.TryFind(new byte[32], out _));
        byte[] bad = alice.Card;
        bad[^1] ^= 1;
        Assert.Throws<VerificationFailedException>(() => dir.Add(bad));
    }

    [Fact]
    public void Action_digests()
    {
        JsonElement d = W.GetProperty("action_digest");
        byte[] from = V.Hex(d.GetProperty("from")), to = V.Hex(d.GetProperty("to"));
        foreach (JsonElement c in d.GetProperty("cases").EnumerateArray())
        {
            byte[] payload = V.Has(c, "payload_ramp_len") ? V.Ramp(c.GetProperty("payload_ramp_len").GetInt32()) : V.Utf8(c.GetProperty("payload_utf8"));
            string tool = V.Str(c, "tool");
            byte[] got = Array.Empty<byte>();
            string status = V.StatusOf(() => got = Consent.ActionDigest(tool, from, to, V.U64(c.GetProperty("req_id")), payload));
            Assert.Equal(V.Str(c, "status"), status);
            if (status == "ZXV_OK")
            {
                Assert.Equal(V.Str(c, "digest"), V.ToHex(got));
            }
        }
    }

    [Fact]
    public void Manifest_signs_encodes_decodes_and_verifies()
    {
        JsonElement m = W.GetProperty("manifest");
        Web4Agent signer = _agents[V.Str(m, "signer")];
        using var man = new ToolManifest(m.GetProperty("version").GetUInt32(), V.U64(m.GetProperty("issued_ms")), V.U64(m.GetProperty("expires_ms")));
        foreach (JsonElement t in m.GetProperty("tools").EnumerateArray())
        {
            V.Expect(t, () => man.AddTool(V.Str(t, "name"), V.U64(t.GetProperty("price_vfv")), t.GetProperty("rate_per_min").GetUInt32(), t.GetProperty("burst").GetUInt32(), (ToolTraits)t.GetProperty("flags").GetUInt32(), t.GetProperty("max_input").GetUInt32()));
        }

        JsonElement t0 = m.GetProperty("tools")[0];
        V.Expect(V.Str(m, "add_duplicate"), () => man.AddTool(V.Str(t0, "name"), 1, 1, 1, ToolTraits.None, 0));
        V.Expect(V.Str(m, "encode_unsigned"), () => man.Encode());
        Assert.False(man.Info.IsSigned);

        signer.Sign(man);
        ManifestInfo info = man.Info;
        Assert.True(info.IsSigned);
        Assert.Equal(signer.AgentId, info.AgentId);
        Assert.Equal(3, info.ToolCount);
        Assert.Equal(1, man.IndexOf("profile.read"));
        Assert.Equal(-1, man.IndexOf("missing"));
        Assert.Equal(new ToolInfo("pay", 150, 60, 10, ToolTraits.Money, 256), man.Tools[0]);

        byte[] enc = man.Encode();
        Assert.Equal(m.GetProperty("encoding_len").GetInt32(), enc.Length);
        Assert.Equal(V.Str(m, "encoding_signed_body"), Body(enc));
        Assert.Equal(V.Str(m, "json"), man.ToJson());

        ToolManifest? decoded = null;
        V.Expect(V.Str(m, "decode"), () => decoded = ToolManifest.Decode(enc));
        using ToolManifest back = decoded!;
        Assert.Equal(enc, back.Encode());
        foreach (JsonElement v in m.GetProperty("verify").EnumerateArray())
        {
            byte[] key = _agents[V.Str(v, "key")].PublicKey;
            V.Expect(v, () => back.Verify(key, V.U64(v.GetProperty("now_ms"))));
        }
    }

    [Fact]
    public void Consent_tokens_are_single_use_and_checked()
    {
        JsonElement c = W.GetProperty("consent");
        Web4Agent human = _agents[V.Str(c, "human")];
        Web4Agent agent = _agents[V.Str(c, "agent")];
        byte[] action = V.Hex(W.GetProperty("action_digest").GetProperty("cases")[1].GetProperty("digest"));
        byte[] nonce = V.Hex(c.GetProperty("nonce"));
        ulong issued = V.U64(c.GetProperty("issued_ms")), expires = V.U64(c.GetProperty("expires_ms"));
        ConsentToken token = default;
        V.Expect(c, () => token = human.IssueConsent(agent.AgentId, action, (ConsentScope)c.GetProperty("scope").GetUInt32(), V.U64(c.GetProperty("max_vfv")), issued, expires, nonce));
        Assert.Equal(c.GetProperty("token_len").GetInt32(), token.Bytes.Length);
        Assert.Equal(V.Str(c, "token_signed_body"), Body(token.ToArray()));
        ConsentInfo info = token.Info;
        Assert.Equal(human.AgentId, info.HumanId);
        Assert.Equal(agent.AgentId, info.AgentId);
        Assert.Equal(action, info.Action);
        Assert.Equal(nonce, info.Nonce);
        Assert.Equal(token, ConsentToken.FromBytes(token.Bytes));

        nonce[0] ^= 0xff;
        ConsentToken second = human.IssueConsent(agent.AgentId, action, ConsentScope.Money, 1000, issued, expires, nonce);

        using var log = new ConsentLog(16);
        JsonElement[] checks = c.GetProperty("check").EnumerateArray().ToArray();
        for (int i = 0; i < checks.Length; i++)
        {
            JsonElement k = checks[i];
            ConsentToken use = token;
            if (i == checks.Length - 1)
            {
                log.Revoke(second, V.U64(k.GetProperty("now_ms")));
                use = second;
            }

            V.Expect(k, () => log.Check(use, human.PublicKey, _agents[V.Str(k, "agent")].AgentId, action, (ConsentScope)k.GetProperty("need").GetUInt32(), V.U64(k.GetProperty("amount")), V.U64(k.GetProperty("now_ms"))));
        }

        // ZXV_E_CONSENT is REFUSED; no token at all is refused the same way.
        var e = Assert.Throws<ZxvRefusedException>(() => log.Check(default, human.PublicKey, agent.AgentId, action, ConsentScope.Money, 1, issued + 1));
        Assert.Equal(ZxvStatus.ConsentRequired, e.Status);
        Assert.True(log.Prune(expires + 1) >= 0);
    }

    [Fact]
    public void Envelopes_seal_reply_decode_and_verify()
    {
        JsonElement[] envs = W.GetProperty("envelopes").EnumerateArray().ToArray();
        JsonElement s = envs[0];
        Web4Agent alice = _agents[V.Str(s, "sender")];
        Web4Agent bob = _agents[V.Str(s, "to")];
        byte[] req = Array.Empty<byte>();
        V.Expect(s, () => req = alice.Seal((EnvelopeKind)s.GetProperty("kind").GetInt32(), bob.AgentId, V.U64(s.GetProperty("req_id")), s.GetProperty("chunk").GetUInt32(), V.Str(s, "tool"), (EnvelopeMarks)s.GetProperty("flags").GetUInt32(), V.Utf8(s.GetProperty("payload_utf8")), V.U64(s.GetProperty("now_ms"))));
        Assert.Equal(s.GetProperty("env_len").GetInt32(), req.Length);
        Assert.Equal(V.Str(s, "env_signed_body"), Body(req));
        Assert.Equal(V.Str(s, "verify_sender") == "ZXV_OK", Envelope.Verify(req, alice.PublicKey));
        Assert.Equal(V.Str(s, "verify_other") == "ZXV_OK", Envelope.Verify(req, bob.PublicKey));
        EnvelopeInfo info = Envelope.Decode(req);
        Assert.Equal(EnvelopeKind.Request, info.Kind);
        Assert.Equal(alice.AgentId, info.From);
        Assert.Equal(bob.AgentId, info.To);
        Assert.Equal("pay", info.Tool);
        Assert.Equal(V.U64(s.GetProperty("now_ms")), info.TimestampMs);
        Assert.Equal(V.Utf8(s.GetProperty("payload_utf8")), Envelope.Payload(req));
        Assert.True(Envelope.AttachedConsent(req).IsEmpty);

        JsonElement r = envs[1];
        byte[] rep = Array.Empty<byte>();
        V.Expect(r, () => rep = bob.Reply(req, (EnvelopeKind)r.GetProperty("kind").GetInt32(), r.GetProperty("chunk").GetUInt32(), (EnvelopeMarks)r.GetProperty("flags").GetUInt32(), V.Utf8(r.GetProperty("payload_utf8")), V.U64(r.GetProperty("now_ms"))));
        Assert.Equal(r.GetProperty("env_len").GetInt32(), rep.Length);
        Assert.Equal(V.Str(r, "env_signed_body"), Body(rep));
        Assert.True(Envelope.Verify(rep, bob.PublicKey));
        Assert.Equal("ok", Encoding.UTF8.GetString(Envelope.Payload(rep)));
    }

    [Fact]
    public void Signing_needs_crypto_sign_consent_needs_attest_and_halt_fails_closed()
    {
        using Capability noSign = _cap.Attenuate(CapabilityRights.AllAbi15 & ~CapabilityRights.CryptoSign);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateWeb4Agent(noSign, "x", 1));

        using Capability noAttest = _cap.Attenuate(CapabilityRights.AllAbi15 & ~CapabilityRights.Attest);
        using Web4Agent h = _p.CreateWeb4Agent(noAttest, "h", 1);
        Assert.Throws<CapabilityDeniedException>(() => h.IssueConsent(new byte[32], new byte[32], ConsentScope.Money, 1, 1, 2));

        Web4Agent gone = _p.CreateWeb4Agent(_cap, "gone", 1);
        gone.Dispose();
        Assert.Throws<ObjectDisposedException>(() => gone.PublicKey);
        Assert.Throws<ObjectDisposedException>(() => gone.Seal(EnvelopeKind.Request, new byte[32], 1, 0, "pay", EnvelopeMarks.None, default, 1));
        Assert.Equal("Web4Agent(disposed)", gone.ToString());

        _p.Halt("test");
        Assert.Throws<ZxvFatalException>(() => _agents["alice"].Seal(EnvelopeKind.Request, new byte[32], 1, 0, "pay", EnvelopeMarks.None, default, 1));
    }
}
