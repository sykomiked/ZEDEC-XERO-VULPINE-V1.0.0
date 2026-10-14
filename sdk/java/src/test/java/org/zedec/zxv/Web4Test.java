// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.web4.AgentCard;
import org.zedec.zxv.web4.AgentDirectory;
import org.zedec.zxv.web4.AgentKey;
import org.zedec.zxv.web4.ConsentException;
import org.zedec.zxv.web4.ConsentLog;
import org.zedec.zxv.web4.ConsentScope;
import org.zedec.zxv.web4.ConsentToken;
import org.zedec.zxv.web4.Envelope;
import org.zedec.zxv.web4.EnvelopeKind;
import org.zedec.zxv.web4.Tool;
import org.zedec.zxv.web4.ToolManifest;
import org.zedec.zxv.web4.Web4;

/** ABI 1.5 Web 4 vectors. Signatures are hedged, so only the signed bodies are compared. */
class Web4Test {
    private static final Map<String, Object> V = V15.sec("web4");
    private static final int SIG = Web4.SIGNATURE_BYTES;

    private static String body(byte[] signed) {
        return V15.hex(Arrays.copyOf(signed, signed.length - SIG));
    }

    @Test
    void web4() {
        assertEquals(V15.i(V.get("sig_bytes")), SIG);
        Map<String, AgentKey> keys = new HashMap<>();
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                AgentDirectory dir = AgentDirectory.create(8)) {
            for (Object o : V15.l(V.get("agents"))) {
                Map<String, Object> a = V15.m(o);
                AgentKey node = a.get("node") == null ? null : keys.get((String) a.get("node"));
                AgentKey k = AgentKey.create(cap, V15.fill(V15.hex(a.get("seed_byte"))[0], 32), (String) a.get("name"),
                        node, V15.u64(a.get("now_ms")));
                keys.put((String) a.get("name"), k);
                assertEquals(a.get("agent_id"), V15.hex(k.agentId()));
                assertEquals(a.get("peer_id"), V15.hex(k.peerId()));
                assertEquals(a.get("pk_sha3_256"), V15.hex(Hashes.sha3_256(k.publicKey())));
                AgentCard card = k.card();
                byte[] cb = card.bytes();
                assertEquals(V15.i(a.get("card_len")), cb.length);
                assertEquals(a.get("card_signed_body_sha3_256"), V15.hex(Hashes.sha3_256(Arrays.copyOf(cb, cb.length - SIG))));
                AgentCard.Verified v = V15.expect(a.get("card_verify"), card::verify);
                assertEquals(a.get("name"), v.name());
                assertArrayEquals(k.agentId(), v.agentId());
                assertEquals(node == null, v.isSelfBound());
                cb[cb.length - 1] ^= 1;
                V15.expect(a.get("card_verify_flipped_last_byte"), () -> AgentCard.of(cb).verify());
                dir.add(card);
                assertFalse(k.toString().contains(V15.hex(k.publicKey()).substring(0, 64)), "toString shows no key bytes");
            }
            assertEquals(4, dir.count());
            assertEquals(keys.get("bob").card(), dir.find(keys.get("bob").agentId()).orElseThrow());
            assertTrue(dir.find(new byte[32]).isEmpty());

            // action digests
            Map<String, Object> ad = V15.m(V.get("action_digest"));
            byte[] from = V15.hex(ad.get("from")), to = V15.hex(ad.get("to"));
            for (Object o : V15.l(ad.get("cases"))) {
                Map<String, Object> c = V15.m(o);
                byte[] payload = c.containsKey("payload_utf8") ? V15.utf8(c.get("payload_utf8"))
                        : V15.ramp(V15.i(c.get("payload_ramp_len")));
                byte[] d = V15.expect(c.get("status"),
                        () -> Web4.actionDigest((String) c.get("tool"), from, to, V15.u64(c.get("req_id")), payload));
                if (d != null) assertEquals(c.get("digest"), V15.hex(d));
            }

            // manifest
            Map<String, Object> mf = V15.m(V.get("manifest"));
            AgentKey alice = keys.get("alice"), bob = keys.get("bob"), human = keys.get("human");
            try (ToolManifest m = ToolManifest.create(V15.i(mf.get("version")), V15.u64(mf.get("issued_ms")),
                    V15.u64(mf.get("expires_ms")))) {
                for (Object o : V15.l(mf.get("tools"))) {
                    Map<String, Object> t = V15.m(o);
                    V15.expectRun(t.get("status"), () -> m.addTool(new Tool((String) t.get("name"),
                            V15.u64(t.get("price_vfv")), V15.i(t.get("rate_per_min")), V15.i(t.get("burst")),
                            V15.i(t.get("flags")), V15.i(t.get("max_input")))));
                }
                V15.expectRun(mf.get("add_duplicate"), () -> m.addTool(new Tool("pay", 1, 1, 1, 0, 0)));
                V15.expect(mf.get("encode_unsigned"), m::encode);
                alice.sign(m);
                byte[] enc = m.encode();
                assertEquals(V15.i(mf.get("encoding_len")), enc.length);
                assertEquals(mf.get("encoding_signed_body"), body(enc));
                assertEquals(mf.get("json"), m.toJson());
                assertEquals(List.of("pay", "profile.read", "stream.feed"),
                        m.tools().stream().map(Tool::name).toList());
                assertEquals(1, m.find("profile.read").orElseThrow());
                assertTrue(m.info().signed());
                try (ToolManifest d = V15.expect(mf.get("decode"), () -> ToolManifest.decode(enc))) {
                    for (Object o : V15.l(mf.get("verify"))) {
                        Map<String, Object> c = V15.m(o);
                        byte[] pk = keys.get((String) c.get("key")).publicKey();
                        V15.expectRun(c.get("status"), () -> d.verify(pk, V15.u64(c.get("now_ms"))));
                    }
                }
            }

            // consent
            Map<String, Object> cs = V15.m(V.get("consent"));
            byte[] action = V15.hex(V15.m(V15.l(ad.get("cases")).get(1)).get("digest"));
            byte[] nonce = V15.hex(cs.get("nonce"));
            long issued = V15.u64(cs.get("issued_ms")), expires = V15.u64(cs.get("expires_ms"));
            ConsentToken tok = human.issueConsent(alice.agentId(), action, V15.i(cs.get("scope")),
                    V15.u64(cs.get("max_vfv")), issued, expires, nonce);
            assertEquals(V15.i(cs.get("token_len")), tok.bytes().length);
            assertEquals(cs.get("token_signed_body"), body(tok.bytes()));
            assertArrayEquals(human.agentId(), tok.humanId());
            assertEquals(ConsentScope.MONEY, tok.scope());
            byte[] nonce2 = nonce.clone();
            nonce2[0] ^= (byte) 0xFF;
            ConsentToken tok2 = human.issueConsent(alice.agentId(), action, ConsentScope.MONEY, 1000, issued, expires, nonce2);
            try (ConsentLog log = ConsentLog.create(16)) {
                List<Object> checks = V15.l(cs.get("check"));
                for (int i = 0; i < checks.size(); i++) {
                    Map<String, Object> c = V15.m(checks.get(i));
                    boolean second = i == checks.size() - 1;
                    long now = V15.u64(c.get("now_ms"));
                    if (second) log.revoke(tok2, now);
                    ConsentToken t = second ? tok2 : tok;
                    byte[] agent = keys.get((String) c.get("agent")).agentId();
                    V15.expectRun(c.get("status"), () -> log.check(t, human.publicKey(), agent, action,
                            V15.i(c.get("need")), V15.u64(c.get("amount")), now));
                }
                assertThrows(ConsentException.class,
                        () -> log.check(null, human.publicKey(), alice.agentId(), action, ConsentScope.MONEY, 1, issued));
            }

            // envelopes
            List<Object> envs = V15.l(V.get("envelopes"));
            Map<String, Object> e0 = V15.m(envs.get(0)), e1 = V15.m(envs.get(1));
            Envelope req = alice.seal(EnvelopeKind.fromCode(V15.i(e0.get("kind"))), bob.agentId(), V15.u64(e0.get("req_id")),
                    V15.i(e0.get("chunk")), (String) e0.get("tool"), V15.i(e0.get("flags")), V15.utf8(e0.get("payload_utf8")),
                    null, V15.u64(e0.get("now_ms")));
            assertEquals(V15.i(e0.get("env_len")), req.bytes().length);
            assertEquals(e0.get("env_signed_body"), body(req.bytes()));
            V15.expectRun(e0.get("verify_sender"), () -> req.verify(alice.publicKey()));
            V15.expectRun(e0.get("verify_other"), () -> req.verify(bob.publicKey()));
            assertEquals("pay", req.tool());
            assertArrayEquals(V15.utf8("amount=100"), req.payload());
            assertTrue(req.consent().isEmpty());
            Envelope rsp = bob.reply(req, EnvelopeKind.fromCode(V15.i(e1.get("kind"))), V15.i(e1.get("chunk")),
                    V15.i(e1.get("flags")), V15.utf8(e1.get("payload_utf8")), V15.u64(e1.get("now_ms")));
            assertEquals(V15.i(e1.get("env_len")), rsp.bytes().length);
            assertEquals(e1.get("env_signed_body"), body(rsp.bytes()));
            V15.expectRun(e1.get("verify_sender"), () -> rsp.verify(bob.publicKey()));
            assertTrue(rsp.isFinal());
            assertEquals(EnvelopeKind.RESPONSE, Envelope.of(rsp.bytes()).kind());

            // a request with a consent token attached
            Envelope withConsent = alice.seal(EnvelopeKind.REQUEST, bob.agentId(), 8, 0, "pay", 0, null, tok2, issued);
            assertEquals(tok2, withConsent.consent().orElseThrow());
            assertTrue((withConsent.flags() & Envelope.FLAG_CONSENT) != 0);
        } finally {
            keys.values().forEach(AgentKey::close);
        }
    }
}
