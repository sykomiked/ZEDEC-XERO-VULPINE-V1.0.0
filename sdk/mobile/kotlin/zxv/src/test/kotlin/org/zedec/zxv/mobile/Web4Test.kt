// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.serialization.json.JsonNull
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 Web 4 vectors: agents, cards, directory, digests, manifests, consent, envelopes. */
class Web4Test {
    private val v = V15.sec("web4")
    private val sig = Web4.SIGNATURE_BYTES

    private fun body(signed: ByteArray): String = signed.copyOf(signed.size - sig).toHex()

    @Test
    fun web4() {
        assertEquals(v.obj("sig_bytes").i(), sig)
        val keys = LinkedHashMap<String, AgentKey>()
        try {
            ZxvContext.create().use { ctx ->
                V15.root(ctx).use { cap ->
                    AgentDirectory.create(8).use { dir -> run(cap, dir, keys) }
                }
            }
        } finally {
            keys.values.forEach { it.close() }
        }
    }

    private fun run(cap: Capability, dir: AgentDirectory, keys: MutableMap<String, AgentKey>) {
        for (a in v.obj("agents").arr()) {
            val nodeName = a.opt("node")?.takeIf { it !is JsonNull }?.s()
            val node = nodeName?.let { keys.getValue(it) }
            val k = SecretKey.fromBytes(V15.fill(V15.hexOf(a.obj("seed_byte"))[0].toInt(), 32)).use { seed ->
                AgentKey.create(cap, seed, a.obj("name").s(), node, V15.u64(a.obj("now_ms")))
            }
            keys[a.obj("name").s()] = k
            assertEquals(a.obj("agent_id").s(), k.agentId.toHex())
            assertEquals(a.obj("peer_id").s(), k.peerId.toHex())
            assertEquals(a.obj("pk_sha3_256").s(), Hashes.sha3_256(k.publicKey).toHex())
            val card = k.card()
            val cb = card.bytes
            assertEquals(a.obj("card_len").i(), cb.size)
            assertEquals(a.obj("card_signed_body_sha3_256").s(), Hashes.sha3_256(cb.copyOf(cb.size - sig)).toHex())
            val vf = V15.expect(a.obj("card_verify")) { card.verify() }!!
            assertEquals(a.obj("name").s(), vf.name)
            assertContentEquals(k.agentId, vf.agentId)
            assertEquals(node == null, vf.isSelfBound)
            cb[cb.size - 1] = (cb[cb.size - 1].toInt() xor 1).toByte()
            V15.expect(a.obj("card_verify_flipped_last_byte")) { AgentCard.of(cb).verify() }
            dir.add(card)
            assertFalse(k.toString().contains(k.publicKey.toHex().substring(0, 64)), "toString shows no key bytes")
        }
        assertEquals(4, dir.count())
        val alice = keys.getValue("alice")
        val bob = keys.getValue("bob")
        val human = keys.getValue("human")
        assertEquals(bob.card(), dir.find(bob.agentId))
        assertNull(dir.find(ByteArray(32)))

        // action digests
        val ad = v.obj("action_digest")
        val from = V15.hexOf(ad.obj("from"))
        val to = V15.hexOf(ad.obj("to"))
        for (c in ad.obj("cases").arr()) {
            val payload = c.opt("payload_utf8")?.let { V15.utf8(it) } ?: V15.ramp(c.obj("payload_ramp_len").i())
            V15.expect(c.obj("status")) { Web4.actionDigest(c.obj("tool").s(), from, to, V15.u64(c.obj("req_id")), payload) }
                ?.let { assertEquals(c.obj("digest").s(), it.toHex()) }
        }

        // manifest
        val mf = v.obj("manifest")
        ToolManifest.create(mf.obj("version").l(), V15.u64(mf.obj("issued_ms")), V15.u64(mf.obj("expires_ms"))).use { m ->
            for (t in mf.obj("tools").arr()) {
                V15.expect(t.obj("status")) {
                    m.addTool(
                        Tool(t.obj("name").s(), V15.u64(t.obj("price_vfv")), t.obj("rate_per_min").l(), t.obj("burst").l(), t.obj("flags").l(), t.obj("max_input").l()),
                    )
                }
            }
            V15.expect(mf.obj("add_duplicate")) { m.addTool(Tool("pay", 1, 1, 1, 0, 0)) }
            V15.expect(mf.obj("encode_unsigned")) { m.encode() }
            alice.sign(m)
            val enc = m.encode()
            assertEquals(mf.obj("encoding_len").i(), enc.size)
            assertEquals(mf.obj("encoding_signed_body").s(), body(enc))
            assertEquals(mf.obj("json").s(), m.toJson())
            assertEquals(listOf("pay", "profile.read", "stream.feed"), m.tools().map { it.name })
            assertEquals(1, m.find("profile.read"))
            assertTrue(m.info().signed)
            V15.expect(mf.obj("decode")) { ToolManifest.decode(enc) }!!.use { d ->
                for (c in mf.obj("verify").arr()) {
                    val pk = keys.getValue(c.obj("key").s()).publicKey
                    V15.expect(c.obj("status")) { d.verify(pk, V15.u64(c.obj("now_ms"))) }
                }
            }
        }

        // consent
        val cs = v.obj("consent")
        val action = V15.hexOf(ad.obj("cases").arr()[1].obj("digest"))
        val nonce = V15.hexOf(cs.obj("nonce"))
        val issued = V15.u64(cs.obj("issued_ms"))
        val expires = V15.u64(cs.obj("expires_ms"))
        val tok = human.issueConsent(alice.agentId, action, cs.obj("scope").l(), V15.u64(cs.obj("max_vfv")), issued, expires, nonce)
        assertEquals(cs.obj("token_len").i(), tok.bytes.size)
        assertEquals(cs.obj("token_signed_body").s(), body(tok.bytes))
        assertContentEquals(human.agentId, tok.humanId)
        assertEquals(ConsentScope.MONEY.toLong(), tok.scope)
        val nonce2 = nonce.copyOf().also { it[0] = (it[0].toInt() xor 0xFF).toByte() }
        val tok2 = human.issueConsent(alice.agentId, action, ConsentScope.MONEY.toLong(), 1000, issued, expires, nonce2)
        ConsentLog.create(16).use { log ->
            val checks = cs.obj("check").arr()
            checks.forEachIndexed { i, c ->
                val second = i == checks.size - 1
                val now = V15.u64(c.obj("now_ms"))
                if (second) log.revoke(tok2, now)
                val t = if (second) tok2 else tok
                val agent = keys.getValue(c.obj("agent").s()).agentId
                V15.expect(c.obj("status")) { log.check(t, human.publicKey, agent, action, c.obj("need").l(), V15.u64(c.obj("amount")), now) }
            }
            assertFailsWith<ConsentException> {
                log.check(null, human.publicKey, alice.agentId, action, ConsentScope.MONEY.toLong(), 1, issued)
            }
        }

        // envelopes
        val envs = v.obj("envelopes").arr()
        val e0 = envs[0]
        val e1 = envs[1]
        val req = alice.seal(
            EnvelopeKind.fromCode(e0.obj("kind").i()), bob.agentId, V15.u64(e0.obj("req_id")), e0.obj("chunk").l(),
            e0.obj("tool").s(), e0.obj("flags").l(), V15.utf8(e0.obj("payload_utf8")), null, V15.u64(e0.obj("now_ms")),
        )
        assertEquals(e0.obj("env_len").i(), req.bytes.size)
        assertEquals(e0.obj("env_signed_body").s(), body(req.bytes))
        V15.expect(e0.obj("verify_sender")) { req.verify(alice.publicKey) }
        V15.expect(e0.obj("verify_other")) { req.verify(bob.publicKey) }
        assertEquals("pay", req.tool)
        assertContentEquals(V15.utf8("amount=100"), req.payload())
        assertNull(req.consent())
        val rsp = bob.reply(
            req, EnvelopeKind.fromCode(e1.obj("kind").i()), e1.obj("chunk").l(), e1.obj("flags").l(),
            V15.utf8(e1.obj("payload_utf8")), V15.u64(e1.obj("now_ms")),
        )
        assertEquals(e1.obj("env_len").i(), rsp.bytes.size)
        assertEquals(e1.obj("env_signed_body").s(), body(rsp.bytes))
        V15.expect(e1.obj("verify_sender")) { rsp.verify(bob.publicKey) }
        assertTrue(rsp.isFinal)
        assertEquals(EnvelopeKind.RESPONSE, Envelope.of(rsp.bytes).kind)

        // a request with a consent token attached
        val withConsent = alice.seal(EnvelopeKind.REQUEST, bob.agentId, 8, 0, "pay", 0, null, tok2, issued)
        assertEquals(tok2, assertNotNull(withConsent.consent()))
        assertTrue(withConsent.flags and Envelope.FLAG_CONSENT.toLong() != 0L)
    }
}
