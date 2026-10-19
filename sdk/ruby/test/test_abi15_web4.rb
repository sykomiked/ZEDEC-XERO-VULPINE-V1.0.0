# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 conformance: the web4 section of vectors.json. Signatures are
# hedged, so signed encodings are compared by length and by the bytes
# before the trailing signature.
class TestAbi15Web4 < Minitest::Test
  include ZxvTest
  W4 = Zxv::Web4

  def wv = vectors.fetch("web4")

  def body(bytes) = bytes.byteslice(0, bytes.bytesize - W4::SIG_BYTES)

  def agents(cap)
    by_name = {}
    wv["agents"].each do |a|
      node = a["node"] && by_name.fetch(a["node"])
      ag = expect_status(a["status"]) do
        W4::Agent.new(cap, name: a["name"], now_ms: u64(a["now_ms"]), seed: hex(a["seed_byte"]) * 32, node: node)
      end
      by_name[a["name"]] = ag
    end
    by_name
  end

  def test_agents_and_cards
    assert_equal wv["sig_bytes"], W4::SIG_BYTES
    with_context15 do |_ctx, cap|
      ag = agents(cap)
      wv["agents"].each do |a|
        h = ag.fetch(a["name"])
        assert_equal a["agent_id"], unhex(h.agent_id)
        assert_equal a["peer_id"], unhex(h.peer_id)
        assert_equal a["pk_sha3_256"], unhex(sha3(h.public_key))
        card = h.card
        assert_equal a["card_len"], card.bytesize
        assert_equal a["card_signed_body_sha3_256"], unhex(sha3(body(card)))
        info = expect_status(a["card_verify"]) { W4.verify_card(card) }
        assert_equal h.agent_id, info.agent_id
        assert_equal h.peer_id, info.peer_id
        assert_equal a["name"], info.name
        assert_equal u64(a["now_ms"]), info.bound_ms
        assert_equal a["node"].nil?, info.self_bound?
        flipped = card.dup
        flipped.setbyte(-1, flipped.getbyte(-1) ^ 1)
        expect_status(a["card_verify_flipped_last_byte"]) { W4.verify_card(flipped) }
      end
      dir = W4::Directory.new(capacity: 8)
      cards = ag.transform_values(&:card)
      cards.each_value { |c| dir.add(c) }
      assert_equal 4, dir.count
      assert_equal cards["bob"], dir.find(ag["bob"].agent_id)
      assert_nil dir.find("\x00".b * 32)
      dir.close
      ag.each_value(&:close)
    end
  end

  def test_action_digests
    ad = wv["action_digest"]
    ad["cases"].each do |c|
      payload = c.key?("payload_ramp_len") ? Array.new(c["payload_ramp_len"]) { |i| i & 255 }.pack("C*") : c["payload_utf8"]
      got = expect_status(c["status"]) do
        W4.action_digest(tool: c["tool"], from: hex(ad["from"]), to: hex(ad["to"]), req_id: u64(c["req_id"]), payload: payload)
      end
      assert_equal c["digest"], unhex(got) if c["status"] == "ZXV_OK"
    end
  end

  def test_manifest
    mv = wv["manifest"]
    with_context15 do |_ctx, cap|
      ag = agents(cap)
      m = W4::Manifest.new(version: mv["version"], issued_ms: u64(mv["issued_ms"]), expires_ms: u64(mv["expires_ms"]))
      mv["tools"].each do |t|
        expect_status(t["status"]) do
          m.add_tool(t["name"], price_vfv: u64(t["price_vfv"]), rate_per_min: t["rate_per_min"], burst: t["burst"],
                                flags: t["flags"], max_input: t["max_input"])
        end
      end
      expect_status(mv["add_duplicate"]) { m.add_tool("pay", price_vfv: 1, rate_per_min: 1, burst: 1) }
      expect_status(mv["encode_unsigned"]) { m.encode }
      refute m.signed?
      m.sign(ag.fetch(mv["signer"]))
      assert m.signed?
      enc = m.encode
      assert_equal mv["encoding_len"], enc.bytesize
      assert_equal mv["encoding_signed_body"], unhex(body(enc))
      assert_equal mv["json"], m.to_json
      d = expect_status(mv["decode"]) { W4::Manifest.decode(enc) }
      assert_equal 3, d.info.tool_count
      assert_equal 2, d.find("stream.feed")
      assert_equal [:stream], d.tool(2).flags
      assert_equal 150, d.tool(0).price_vfv
      assert_nil d.find("nope")
      mv["verify"].each do |v|
        expect_status(v["status"]) { d.verify!(ag.fetch(v["key"]).public_key, now_ms: u64(v["now_ms"])) }
      end
      assert d.valid?(ag["alice"].public_key, now_ms: u64(mv["issued_ms"]))
      refute d.valid?(ag["bob"].public_key, now_ms: u64(mv["issued_ms"]))
      [m, d].each(&:close)
    end
  end

  def test_consent
    cv = wv["consent"]
    ad = wv["action_digest"]
    with_context15 do |_ctx, cap|
      ag = agents(cap)
      human = ag.fetch(cv["human"])
      agent = ag.fetch(cv["agent"])
      assert_equal "action_digest.cases[1].digest", cv["action"]
      action = hex(ad["cases"][1]["digest"])
      issue = lambda do |nonce|
        human.issue_consent(agent_id: agent.agent_id, action: action, scope: cv["scope"], max_vfv: u64(cv["max_vfv"]),
                            issued_ms: u64(cv["issued_ms"]), expires_ms: u64(cv["expires_ms"]), nonce: nonce)
      end
      nonce = hex(cv["nonce"])
      tok = expect_status(cv["status"]) { issue.call(nonce) }
      assert_equal cv["token_len"], tok.bytesize
      assert_equal cv["token_signed_body"], unhex(body(tok))
      info = W4.consent_info(tok)
      assert_equal [human.agent_id, agent.agent_id, action], [info.human_id, info.agent_id, info.action]
      assert_equal u64(cv["max_vfv"]), info.max_vfv
      n2 = nonce.dup
      n2.setbyte(0, n2.getbyte(0) ^ 0xFF)
      tok2 = issue.call(n2)
      log = W4::ConsentLog.new(capacity: 16)
      cv["check"].each do |c|
        second = c["what"].start_with?("second token")
        log.revoke(tok2, now_ms: u64(c["now_ms"])) if second
        expect_status(c["status"]) do
          log.check!(second ? tok2 : tok, human_public_key: human.public_key, agent_id: ag.fetch(c["agent"]).agent_id,
                                          action: action, need: c["need"], amount: u64(c["amount"]), now_ms: u64(c["now_ms"]))
        end
      end
      assert_kind_of Integer, log.prune(u64(cv["expires_ms"]))
      log.close
    end
  end

  def test_envelopes
    ev = wv["envelopes"]
    with_context15 do |_ctx, cap|
      ag = agents(cap)
      s = ev[0]
      sender = ag.fetch(s["sender"])
      req = expect_status(s["status"]) do
        sender.seal(to: ag.fetch(s["to"]).agent_id, kind: s["kind"], tool: s["tool"], req_id: u64(s["req_id"]),
                    chunk: s["chunk"], final: s["flags"].anybits?(W4::MF_FINAL), payload: s["payload_utf8"],
                    now_ms: u64(s["now_ms"]))
      end
      assert_equal s["env_len"], req.bytesize
      assert_equal s["env_signed_body"], unhex(body(req))
      env = W4::Envelope.new(req)
      expect_status(s["verify_sender"]) { env.verify!(sender.public_key) }
      expect_status(s["verify_other"]) { env.verify!(ag.fetch(s["to"]).public_key) }
      assert_equal [:request, "pay", 7, "amount=100"], [env.kind, env.tool, env.req_id, env.payload]
      r = ev[1]
      replier = ag.fetch(r["sender"])
      rsp = expect_status(r["status"]) do
        replier.reply(req, kind: r["kind"], chunk: r["chunk"], final: r["flags"].anybits?(W4::MF_FINAL),
                           payload: r["payload_utf8"], now_ms: u64(r["now_ms"]))
      end
      assert_equal r["env_len"], rsp.bytesize
      assert_equal r["env_signed_body"], unhex(body(rsp))
      renv = W4::Envelope.new(rsp)
      expect_status(r["verify_sender"]) { renv.verify!(replier.public_key) }
      assert renv.final?
      assert_equal ag.fetch(r["to"]).agent_id, renv.to
      # Only the addressee may reply.
      expect_status("ZXV_E_ARG") { ag["human"].reply(req, kind: :response, now_ms: 1) }
    end
  end
end
