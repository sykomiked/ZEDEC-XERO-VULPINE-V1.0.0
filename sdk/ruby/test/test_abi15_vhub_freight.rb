# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 conformance: the vhub and freight sections of vectors.json.
class TestAbi15VhubFreight < Minitest::Test
  include ZxvTest

  def vv = vectors.fetch("vhub")

  def test_vhub_peg_messages
    vv["peg_msg"].each do |v|
      msg = Zxv::VHub.peg_message(v["slot"], num: u64(v["num"]), den: u64(v["den"]), epoch: u64(v["epoch"]))
      assert_equal v["msg"], unhex(msg)
    end
  end

  def test_vhub_scenario
    sc = vv["scenario"]
    with_context15 do |_ctx, cap|
      dsa = Zxv::MlDsa87.new(cap)
      keys = sc["keys"].map do |k|
        kp = dsa.generate(seed: hex(k["seed"]))
        assert_equal k["pk_sha3_256"], unhex(sha3(kp.public_key))
        kp
      end
      hub = Zxv::VHub.new(cap, seed: hex(sc["hub_seed"]))
      sc["currencies"].each do |c|
        key = keys[c["issuer_key"] || 0].public_key
        funding = c.key?("pool_funding") ? u64(c["pool_funding"]) : 0
        backing = c.key?("vbill_backing") ? u64(c["vbill_backing"]) : 0
        info = expect_status(c["status"]) do
          hub.add_currency(c["slot"], code: c["code"], minor_units: c["minor_units"] || 2, issuer_public_key: key,
                                      pool_funding: funding, vbill_backing: backing)
        end
        next unless c["status"] == "ZXV_OK"

        assert_equal [c["issuer_account"], c["pool_account"], c["vfloat_account"]],
                     [info.issuer_account, info.pool_account, info.vfloat_account]
        refute info.pegged?
      end
      sc["pegs"].each do |p|
        msg = Zxv::VHub.peg_message(p["slot"], num: u64(p["num"]), den: u64(p["den"]), epoch: u64(p["epoch"]))
        sig = dsa.sign(keys[p["signer"]].secret_key, msg, deterministic: true)
        assert_equal p["sig_sha3_256"], unhex(sha3(sig))
        expect_status(p["status"]) do
          hub.set_peg(p["slot"], num: u64(p["num"]), den: u64(p["den"]), epoch: u64(p["epoch"]), signature: sig)
        end
      end
      assert_equal [350, 1, 1], hub.currency(0).then { |c| [c.peg_num, c.peg_den, c.peg_epoch] }
      sc["accounts"].each do |a|
        assert_equal a["account"], hub.open_account(a["slot"], owner: a["owner"])
        hub.issue(a["account"], u64(a["issued"])) if a.key?("issued")
      end
      sc["quotes"].each do |q|
        got = expect_status(q["status"]) { hub.quote(from: q["from"], to: q["to"], want: u64(q["want"])) }
        next unless q["status"] == "ZXV_OK"

        assert_equal [u64(q["amount"]), u64(q["vbills"]), u64(q["received"])], [got.amount, got.vbills, got.received]
      end
      sc["swaps"].each do |s|
        if (lim = s["add_limit"])
          hub.add_limit(lim["slot"], u64(lim["limit"]))
        end
        got = expect_status(s["status"]) do
          hub.swap(from: s["from"], to: s["to"], sender: s["sender"], receiver: s["receiver"], amount: u64(s["amount"]),
                   tick: u64(s["tick"]))
        end
        assert_equal [u64(s["vbills"]), u64(s["received"])], [got.vbills, got.received] if s["status"] == "ZXV_OK"
      end
      assert_equal "ZXV_OK", sc["check"]
      assert_equal u64(sc["postings"]), hub.check!
      bal = hub.balances
      assert_equal sc["balances"].size, bal.size
      sc["balances"].each do |b|
        got = bal[b["account"]]
        assert_equal [u64(b["debit"]), u64(b["credit"]), Integer(b["equity"])], [got.debit, got.credit, got.equity]
        assert_equal got, hub.balance(b["account"])
      end
      keys.each { |k| k.secret_key.close }
    end
  end

  def fv = vectors.fetch("freight")

  def ramp(n) = Array.new(n) { |i| i & 255 }.pack("C*")

  def test_freight_headers
    fv["header_serialize"].each do |v|
      h = expect_status(v["status"]) do
        Zxv::Freight::Header.build(k: v["k"], flags: v["flags"], payload_len: v["payload_len"], seq: v["seq"],
                                   seq_count: v["seq_count"], freight_id: u64(v["freight_id"]),
                                   expand_limit: u64(v["expand_limit"]), payload_sha256: hex(v["payload_sha256"]))
      end
      next unless v["status"] == "ZXV_OK"

      assert_equal v["header"], unhex(h.bytes)
      back = Zxv::Freight::Header.new(hex(v["header"]))
      assert_equal [v["k"], v["flags"], v["payload_len"], v["seq"], v["seq_count"], u64(v["freight_id"]), u64(v["expand_limit"])],
                   [back.k, back.flags, back.payload_len, back.seq, back.seq_count, back.freight_id, back.expand_limit]
      assert_equal hex(v["payload_sha256"]), back.payload_sha256
    end
    fv["header_init_reject"].each do |v|
      expect_status(v["status"]) do
        Zxv::Freight::Header.for_payload(ramp(v["len"]), freight_id: 0, k: v["k"], opstream: (v["flags"] || 0) != 0,
                                                         expand_limit: u64(v["expand_limit"] || "0"))
      end
    end
    bad = hex(fv["header_serialize"][0]["header"]).dup
    bad.setbyte(0, 0)
    expect_status("ZXV_E_FORMAT") { Zxv::Freight::Header.new(bad) }
    expect_status("ZXV_E_FORMAT") { Zxv::Freight::Header.check!(bad) }
    assert Zxv::Freight::Header.check!(hex(fv["header_serialize"][0]["header"]))
  end

  def test_freight_codec
    fv["codec"].each do |c|
      payload = hex(c["payload"])
      h = Zxv::Freight::Header.for_payload(payload, freight_id: u64(c["freight_id"]), k: c["k"])
      assert_equal c["header"], unhex(h.bytes)
      packets = Zxv::Freight.encode(h, payload)
      assert_equal c["packets"], unhex(packets)
      rows = Zxv::Freight.rows(packets)
      assert_equal Zxv::Freight::ROWS, rows.size
      c["decode"].each do |d|
        pick = d["rows"].map { |i| rows[i].dup }
        if (f = d["flip"])
          row = pick[f["index_in_rows"]]
          row.setbyte(f["byte"], row.getbyte(f["byte"]) ^ hex(f["xor"]).getbyte(0))
        end
        got = expect_status(d["status"]) { Zxv::Freight.decode(h, pick) }
        assert_equal payload, got if d["matches_payload"]
        assert_equal payload, Zxv::Freight.decode(h, pick.join) if d["matches_payload"]
      end
    end
  end

  def test_freight_sets_and_join
    fv["set_count"].each do |v|
      got = expect_status(v["status"]) { Zxv::Freight.set_count(u64(v["len"]), k: v["k"]) }
      assert_equal v["count"], got if v["status"] == "ZXV_OK"
    end
    s = fv["split"]
    data = ramp(s["len"])
    s["members"].each do |m|
      got = expect_status(m["status"]) do
        Zxv::Freight.split_member(data, seq: m["seq"], freight_id: u64(s["freight_id"]), k: s["k"])
      end
      next unless m["status"] == "ZXV_OK"

      assert_equal m["header"], unhex(got[0].bytes)
      assert_equal m["packets_sha3_256"], unhex(sha3(got[1]))
    end
    members = Zxv::Freight.split(data, freight_id: u64(s["freight_id"]), k: s["k"])
    assert_equal 3, members.size
    join = Zxv::Freight::Join.new(capacity: data.bytesize)
    expect_status("ZXV_E_STATE") { join.result }
    members.reverse.each { |h, p| join.add(h, p) }
    join.add(*members[0]) # a repeat is a no-op
    st = join.status
    assert st.complete
    assert_equal [3, 3, data.bytesize], [st.received, st.count, st.total]
    assert_equal data, join.result
    join.close
  end

  def test_freight_ops
    fv["ops_encode"].each do |v|
      ops = expect_status(v["status"]) { Zxv::Freight::Ops.encode(v["utf8"], with_sha: v["with_sha"]) }
      assert_equal v["ops"], unhex(ops)
      assert_equal u64(v["bound"]), Zxv::Freight::Ops.bound(v["utf8"].bytesize, with_sha: v["with_sha"])
      got = expect_status(v["expand_status"]) { Zxv::Freight::Ops.expand(ops, limit: 256) }
      assert_equal v["utf8"].b, got
      assert_equal [v["utf8"].bytesize, v["with_sha"]], Zxv::Freight::Ops.peek(ops)
    end
    w1, w2 = fv["ops_writer"]
    w = Zxv::Freight::Ops::Writer.new(declared_total: u64(w1["declared_total"]), max_bytes: 4096)
    w1["ops_list"].each do |op|
      expect_status(op["status"]) do
        case op["op"]
        when "literal" then w.literal(op["utf8"])
        when "copy" then w.copy(distance: u64(op["distance"]), length: u64(op["len"]))
        when "seed" then w.seed(op["gen"], hex(op["params"]), length: u64(op["len"]))
        end
      end
    end
    ops = expect_status(w1["finish_status"]) { w.finish }
    w.close
    assert_equal w1["ops"], unhex(ops)
    out = expect_status(w1["expand_status"]) { Zxv::Freight::Ops.expand(ops, limit: 1024) }
    assert_equal w1["expanded"], unhex(out)
    assert_equal [u64(w1["peek_total"]), w1["peek_has_sha"]], Zxv::Freight::Ops.peek(ops)
    expect_status(w1["expand_limit_too_small"]) { Zxv::Freight::Ops.expand(ops, limit: u64(w1["declared_total"]) - 1) }
    expect_status(w1["expand_truncated"]) { Zxv::Freight::Ops.expand(ops.byteslice(0, ops.bytesize - 1), limit: 1024) }

    cid = Zxv::Cid.new(hex(w2["ops_list"][1]["cid"]))
    w = Zxv::Freight::Ops::Writer.new(declared_total: u64(w2["declared_total"]))
    w.literal(w2["ops_list"][0]["utf8"]).cidref(cid, length: u64(w2["ops_list"][1]["len"])).literal(w2["ops_list"][2]["utf8"])
    ops = w.finish
    w.close
    assert_equal w2["ops"], unhex(ops)
    expect_status(w2["expand_without_resolver"]) { Zxv::Freight::Ops.expand(ops, limit: 64) }
    seen = []
    got = Zxv::Freight::Ops.expand(ops, limit: 64) do |c, len|
      seen << [c, len]
      c == cid.bytes ? w2["ops_list"][1]["resolves_to_utf8"] : nil
    end
    assert_equal w2["expanded_with_resolver_utf8"].b, got
    assert_equal [[cid.bytes, 3]], seen
    # A resolver that raises never unwinds through C: the call fails cleanly.
    assert_raises(Zxv::StatusError) { Zxv::Freight::Ops.expand(ops, limit: 64) { raise "boom" } }
    # A resolver that returns the wrong length is refused too.
    assert_raises(Zxv::StatusError) { Zxv::Freight::Ops.expand(ops, limit: 64) { "toolong" } }

    fv["gen_run"].each do |v|
      got = expect_status(v["status"]) { Zxv::Freight::Ops.generate(v["gen"], hex(v["params"]), v["len"]) }
      assert_equal v["out"], unhex(got) if v["status"] == "ZXV_OK"
    end
  end
end
