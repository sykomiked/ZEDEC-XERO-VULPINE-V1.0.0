# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 conformance: the treaty, cid and aipi sections of vectors.json.
class TestAbi15TreatyCid < Minitest::Test
  include ZxvTest

  def tv = vectors.fetch("treaty")

  def test_treaty_sutra
    assert_equal tv["rec_len"], Zxv::Treaty::REC_LEN
    assert_equal tv["result_len"], Zxv::Treaty::RESULT_LEN
    tv["sutra"].each do |v|
      t = Zxv::Treaty.parse(v["sutra"])
      assert_equal "ZXV_OK", v["status"]
      assert t.valid?
      assert_equal v["record"], unhex(t.to_record)
      back = Zxv::Treaty.from_record(hex(v["record"]))
      assert_equal v["record_parses_equal"], back == t
      assert_equal v["reemit"], t.to_sutra
      assert_equal v["name"], t.name
      assert_equal v["asset"], t.asset
      assert_equal Zxv::Treaty::OPS.fetch(v["op"]), t.op
      assert_equal tv["op_names"][v["op"].to_s].downcase.to_sym, t.op
      assert_equal v["party_count"], t.party_count
      assert_equal v["owe_count"], t.obligation_count
      v["parties"].each_with_index do |p, i|
        got = t.party(i)
        assert_equal p["id"], got.id
        assert_equal u64(p["deposit"]), got.deposit
        assert_equal u64(p["claim"]), got.claim
        assert_equal p["weight"], [got.weight_num, got.weight_den]
      end
      v["owes"].each_with_index do |o, i|
        got = t.obligation(i)
        assert_equal [o["from"], o["to"], u64(o["amount"])], [got.from, got.to, got.amount]
      end
      expect_status("ZXV_E_NOT_FOUND") { t.party(v["party_count"]) }
      t.close
      back.close
    end
  end

  def test_treaty_rejects_with_fault_and_offset
    tv["reject"].each do |v|
      err = expect_status(v["status"]) { Zxv::Treaty.parse(v["sutra"]) }
      assert_instance_of Zxv::TreatyError, err
      assert_equal Zxv::Treaty::FAULTS.fetch(v["fault"]), err.fault, v["sutra"].inspect
      assert_equal v["offset"], err.offset, v["sutra"].inspect
    end
    # A failed re-parse leaves the treaty unchanged.
    t = Zxv::Treaty.parse(tv["sutra"][0]["sutra"])
    assert_raises(Zxv::TreatyError) { t.parse_sutra!("TREATY") }
    assert_equal "CORRIDOR-7", t.name
    t.clear
    refute t.valid?
    assert_raises(Zxv::TreatyError) { t.validate }
  end

  def test_treaty_record_rejects
    tv["reject_record"].each do |v|
      assert_equal "sutra[0].record", v["base"]
      rec = hex(tv["sutra"][0]["record"]).dup
      rec.setbyte(v["patch_offset"], hex(v["patch_byte"]).getbyte(0))
      err = expect_status(v["status"]) { Zxv::Treaty.from_record(rec) }
      assert_equal Zxv::Treaty::FAULTS.fetch(v["fault"]), err.fault
      assert_equal v["offset"], err.offset
    end
  end

  def test_treaty_pic
    tv["pic"].each do |v|
      a = Integer(v["a"])
      b = Integer(v["b"])
      case v["op"]
      when "div"
        got = expect_status(v["status"]) { Zxv::Treaty::Pic.divide(a, b) }
        assert_equal [Integer(v["out"]), Integer(v["rem"])], got if v["status"] == "ZXV_OK"
      else
        fn = { "add" => :add, "sub" => :sub, "mul" => :mul }.fetch(v["op"])
        got = expect_status(v["status"]) { Zxv::Treaty::Pic.public_send(fn, a, b) }
        assert_equal Integer(v["out"]), got if v["status"] == "ZXV_OK"
      end
    end
  end

  def test_treaty_run
    tv["run"].each do |v|
      t = Zxv::Treaty.parse(v["sutra"])
      r = expect_status(v["status"]) { t.run }
      if v["status"] == "ZXV_OK"
        assert_equal v["parties"], r.party_count
        assert_equal v["legs"], r.leg_count
        assert_equal u64(v["fees"]), r.fees
        v["party"].each_with_index do |p, i|
          got = r.party(i)
          assert_equal [Integer(p["net"]), Integer(p["balance"]), Integer(p["dividend"])], [got.net, got.balance, got.dividend]
        end
        v["leg"].each_with_index do |l, i|
          got = r.leg(i)
          assert_equal [l["from"], l["to"], u64(l["amount"])], [got.from, got.to, got.amount]
        end
        assert_equal v["result_record"], unhex(r.to_record)
        r.close
        assert_raises(Zxv::ZxvUsageError) { r.fees }
      else
        assert_instance_of Zxv::InsufficientFundsError, r
      end
      t.close
    end
  end

  def cv = vectors.fetch("cid")

  def check_cid(cid, v)
    assert_equal v["cid"], unhex(cid.bytes)
    assert_equal v["text_default"], cid.to_s
    if v.key?("text_58btc")
      if v["text_58btc"].start_with?("ZXV_")
        expect_status(v["text_58btc"]) { cid.to_s(:base58btc) }
      else
        assert_equal v["text_58btc"], cid.to_s(:base58btc)
      end
    end
    assert_equal v["version"], cid.version if v.key?("version")
    assert_equal v["codec"], cid.codec if v.key?("codec")
    assert_equal v["hash"], cid.hash_code if v.key?("hash")
    assert_equal v["digest"], unhex(cid.digest) if v.key?("digest")
  end

  def test_cid_compute_unixfs_text
    cv["compute"].each do |v|
      c = expect_status(v["status"]) { Zxv::Cid.compute(v["utf8"], codec: v["codec"]) }
      check_cid(c, v)
      assert c.verify(v["utf8"])
    end
    cv["unixfs"].each do |v|
      data = v["ramp"] ? Array.new(v["len"]) { |i| i & 255 }.pack("C*") : v["utf8"]
      c = expect_status(v["status"]) { Zxv::Cid.unixfs(data) }
      check_cid(c, v)
    end
    cv["text"].each do |v|
      c = expect_status(v["status"]) { Zxv::Cid.parse(v["text"]) }
      check_cid(c, v)
      assert_equal v["v1"], unhex(c.to_v1.bytes)
      assert_equal v["v1_text"], c.to_v1.to_s
      assert_equal v["provider_key"], unhex(c.provider_key)
      assert_equal c, Zxv::Cid.parse(c.to_s)
    end
    cv["text_reject"].each do |v|
      expect_status(v["status"]) { Zxv::Cid.parse(v["text"]) }
    end
  end

  def test_cid_verify
    cv["verify"].each do |v|
      expect_status(v["status"]) do
        c = Zxv::Cid.new(hex(v["cid"]))
        c.verify!(v["utf8"])
      end
    end
    good = Zxv::Cid.compute("abc")
    assert good.verify("abc")
    refute good.verify("abd")
  end

  def av = vectors.fetch("aipi")

  def fn_record(v)
    Zxv::Aipi::FunctionRecord.encode(
      intent: v["intent"], verify: v["verify"], privacy: v["privacy"], determinism: v["determinism"],
      quorum_n: v["quorum_n"], quorum_k: v["quorum_k"], fuel_max: u64(v["fuel_max"]),
      module_cid: Zxv::Cid.new(hex(av["module_cid"])),
      contract_cid: v["contract"] ? Zxv::Cid.new(hex(av["contract_cid"])) : nil,
      entry: v["entry"], checker: v["checker"], certifier: v["certifier"]
    )
  end

  def test_aipi_records
    assert_equal av["module_cid"], unhex(Zxv::Cid.compute(av["module_utf8"]).bytes)
    assert_equal av["contract_cid"], unhex(Zxv::Cid.compute(av["contract_utf8"]).bytes)
    fns = av["fn"].map do |v|
      rec = expect_status(v["status"]) { fn_record(v) }
      next nil unless v["status"] == "ZXV_OK"

      assert_equal v["record"], unhex(rec.bytes)
      assert_equal v["fn_cid"], unhex(rec.cid.bytes)
      assert_equal v["fn_cid_text"], rec.cid.to_s(:base58btc)
      d = Zxv::Aipi::FunctionRecord.new(hex(v["record"]))
      assert_equal rec, d
      assert_equal Zxv::Aipi::INTENTS.key(v["intent"]), d.intent
      assert_equal Zxv::Aipi::VERIFY_MODES.key(v["verify"]), d.verify
      assert_equal u64(v["fuel_max"]), d.fuel_max
      assert_equal [v["quorum_n"], v["quorum_k"]], [d.quorum_n, d.quorum_k]
      assert_equal v["entry"], d.entry
      assert_eq v["checker"], d.checker
      assert_eq v["certifier"], d.certifier
      assert_equal av["module_cid"], unhex(d.module_cid.bytes)
      assert_eq(v["contract"] ? av["contract_cid"] : nil, d.contract_cid && unhex(d.contract_cid.bytes))
      rec
    end
    calls = av["call"].map do |v|
      fn = v["fn"] == "module_cid" ? Zxv::Cid.new(hex(av["module_cid"])) : fns[0].cid
      assert_equal "fn[0].fn_cid", v["fn"] unless v["fn"] == "module_cid"
      if v.key?("status") && v["status"] != "ZXV_OK"
        expect_status(v["status"]) { Zxv::Aipi::CallRecord.encode(fn, v["input_utf8"]) }
        expect_status(v["status"]) { Zxv::Aipi.call_cid(fn, v["input_utf8"]) }
        next nil
      end
      rec = Zxv::Aipi::CallRecord.encode(fn, v["input_utf8"])
      assert_equal v["record"], unhex(rec.bytes)
      assert_equal v["call_cid"], unhex(rec.cid.bytes)
      assert_equal v["call_cid_text"], Zxv::Aipi.call_cid(fn, v["input_utf8"]).to_s(:base58btc)
      d = Zxv::Aipi::CallRecord.new(hex(v["record"]))
      assert_equal fn, d.fn_cid
      assert_equal v["input_utf8"].b, d.input
      rec
    end
    av["claim"].each do |v|
      assert_equal "call[1].call_cid", v["call"]
      rec = Zxv::Aipi::ClaimRecord.encode(call_cid: calls[1].cid, executor: hex(v["executor"]), output: v["output_utf8"],
                                          cert: v["cert_utf8"])
      assert_equal v["record"], unhex(rec.bytes)
      d = Zxv::Aipi::ClaimRecord.new(hex(v["record"]))
      assert_equal calls[1].cid, d.call_cid
      assert_equal hex(v["executor"]), d.executor
      assert_equal v["output_utf8"].b, d.output
      assert_equal v["cert_utf8"].b, d.cert
    end
  end

  def test_aipi_node
    with_context15 do |_ctx, cap|
      node = Zxv::Aipi::Node.new(cap, store_bytes: 1 << 16, max_blocks: 16)
      mod = node.put(av["module_utf8"])
      assert_equal av["module_cid"], unhex(mod.bytes)
      assert_equal av["module_utf8"].b, node.get(mod)
      assert_nil node.get(Zxv::Cid.compute("not held"))
      rec = fn_record(av["fn"][0])
      fn_cid = node.publish(rec)
      assert_equal av["fn"][0]["fn_cid"], unhex(fn_cid.bytes)
      assert_equal rec, node.load_function(fn_cid)
      assert_equal :local, node.plan(fn_cid, "x")
      assert_operator node.info.blocks, :>=, 2
      # Private blocks need a node key.
      expect_status("ZXV_E_STATE") { node.put("private bytes", private: true) }
      keyed = Zxv::Aipi::Node.new(cap, node_key: "\x07".b * 32)
      secret = keyed.put("private bytes", private: true)
      assert_equal "private bytes", keyed.get(secret)
      keyed.close
      node.close
      assert_raises(Zxv::ZxvUsageError) { node.info }
    end
  end
end
