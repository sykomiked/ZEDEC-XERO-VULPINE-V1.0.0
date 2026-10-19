# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"
require "digest"

# ABI 1.3: the 64-state hexagram tag and quarantine log against vectors.json "hex".
class TestHexagram < Minitest::Test
  include ZxvTest
  H = Zxv::Hexagram

  def hx = vectors["hex"]

  def test_tags_match_vectors
    assert Zxv.feature?(:hex)
    assert_equal hx["base_names"], H::BASES.map(&:to_s)
    assert_equal hx["polarity_names"], H::POLARITIES.map(&:to_s)
    assert_equal hx["phase_names"], H::PHASES.map(&:to_s)
    assert_equal hx["action_names"], H::ACTIONS.map(&:to_s)
    hx["tags"].each do |v|
      h = H.from_tag(v["tag"])
      assert_equal v["name"], h.name
      assert_equal v["base"], H::BASES.index(h.base)
      assert_equal v["polarity"], H::POLARITIES.index(h.polarity)
      assert_equal v["phase"], H::PHASES.index(h.phase)
      assert_equal v["action"], H::ACTIONS.index(h.action)
      assert_same h, H.of(h.base, h.polarity, h.phase)
      assert_equal h.polarity == :forbidden, h.forbidden?
    end
    H::PHASES.each_with_index { |p, i| assert_equal H::PHASES[hx["phase_next"][i]], H.next_phase(p) }
  end

  def test_logic_tables
    H.all.each do |a|
      assert_equal hx["not"][a.tag], a.not.tag
      assert_equal hx["not"][a.tag], (~a).tag
      H.all.each do |b|
        assert_equal hx["and"][a.tag][b.tag], (a & b).tag
        assert_equal hx["or"][a.tag][b.tag], a.or(b).tag
      end
    end
  end

  def test_step_and_with_state
    H.all.each do |h|
      H::PHASES.each_with_index do |p, i|
        want = hx["step"][h.tag][i]
        if want.negative?
          e = assert_raises(Zxv::ZxvRefusedError) { h.step(p) }
          assert_equal "ZXV_E_STATE", e.status_name
        else
          assert_equal want, h.step(p).tag
        end
      end
      H::BASES.each_with_index do |b, bi|
        H::POLARITIES.each_with_index do |pol, pi|
          want = hx["set"][h.tag][bi * 4 + pi]
          if want.negative?
            assert_raises(Zxv::ZxvRefusedError) { h.with_state(base: b, polarity: pol) }
          else
            assert_equal want, h.with_state(base: b, polarity: pol).tag
          end
        end
      end
      assert_equal H.of(h.base, :forbidden, h.phase), h.forbid
      assert_equal H.next_phase(h.phase), h.advance.phase
    end
  end

  def test_polarity_and_truth_bridge
    hx["polarity"].each do |c|
      assert_equal H::POLARITIES[c["polarity"]],
                   H.polarity_from_evidence(c["pos0"], c["neg0"], c["pos1"], c["neg1"], stale: c["stale"])
    end
    assert_raises(Zxv::ZxvUsageError) { H.polarity_from_evidence(-1, 0, 0, 0, stale: false) }
    assert_raises(Zxv::ZxvUsageError) { H.polarity_from_evidence(0, 0, 0, 0, stale: 1) }
    Zxv::Truth::ALL.each do |t|
      h = H.from_truth(t, :neutral, :potential)
      assert_equal hx["lp_to_base"][t.value], H::BASES.index(h.base)
      assert_same t, h.to_truth
    end
    H::BASES.each_with_index do |b, i|
      assert_equal hx["base_to_lp"][i], H.of(b, :neutral, :potential).to_truth.value
    end
    # The numbering differs on purpose: Truth TRUE is 1, base :true is 3.
    assert_equal 1, Zxv::Truth::TRUE.value
    assert_equal :true, H.from_truth(Zxv::Truth::TRUE, :neutral, :potential).base
  end

  def test_refusals_and_value_semantics
    [64, 100, 255].each { |bad| assert_raises(Zxv::ZxvUsageError) { H.from_tag(bad) } }
    [-1, 256].each { |bad| assert_raises(Zxv::ZxvUsageError) { H.from_tag(bad) } }
    assert_raises(Zxv::FloatRejectedError) { H.from_tag(1.0) }
    assert_raises(Zxv::ZxvUsageError) { H.of(:maybe, :neutral, :potential) }
    assert_raises(Zxv::ZxvUsageError) { H.of(3, :neutral, :potential) }
    assert_raises(Zxv::ZxvUsageError) { H.all[0] & 1 }
    h = H.of(:true, :negentropic, :manifest)
    assert_equal 63, h.tag
    assert_equal "#<Zxv::Hexagram true/negentropic/manifest tag=63>", h.inspect
    assert h.frozen?
    assert_raises(Zxv::TruthConversionError) { !h }
    assert_equal h.hash, H.all[63].hash
    refute_equal H.all[62], h
    f = H.of(:true, :forbidden, :potential)
    assert_equal :rollback, f.action
    4.times do
      f = f.advance
      assert f.forbidden?
    end
    assert((f & h).forbidden? && (f | h).forbidden? && f.not.forbidden?)
    assert_raises(Zxv::ZxvRefusedError) { f.with_state(base: :true, polarity: :neutral) }
    assert_equal :false, f.with_state(base: :false, polarity: :forbidden).base
  end

  def link(seq, tag, reason, who, subj, prev)
    Digest::SHA256.digest("zxv/lpx-qlog/v1".b + [seq].pack("Q<") + [tag, reason].pack("CC") + [who].pack("L<") + subj + prev)
  end

  def test_quarantine_chain_matches_vectors
    q = hx["qlog"]
    with_context do |_ctx, cap|
      log = Zxv::QuarantineLog.new(cap, capacity: 3)
      assert_equal 0, log.count
      assert_equal "\0".b * 32, log.head
      prev = "\0".b * 32
      q["records"].each do |r|
        subj = hex(r["subject"])
        seq = log.append(H.from_tag(r["tag"]), reason: r["reason"], submitter: r["submitter"], subject: subj)
        assert_equal r["seq"], seq
        want = link(seq, r["tag"], r["reason"], r["submitter"], subj, prev)
        assert_equal hex(r["hash"]), want
        rec = log[seq]
        assert_equal want, rec.hash
        assert_equal prev, rec.prev
        assert_equal subj, rec.subject
        assert_equal r["tag"], rec.tag.tag
        assert_equal r["reason"], rec.reason
        assert_equal r["submitter"], rec.submitter
        prev = want
      end
      assert_equal hex(q["head"]), log.head
      assert_equal 3, log.size
      assert_equal [0, 1, 2], log.map(&:seq)
      assert_same log, log.verify
      e = assert_raises(Zxv::ZxvRefusedError) { log.append(H.from_tag(0x3B), reason: 0, submitter: 0, subject: "\0".b * 32) }
      assert_equal "ZXV_E_CAPACITY", e.status_name
    end
  end

  def test_quarantine_refusals
    forb = H.of(:both, :forbidden, :emerging)
    with_context do |ctx, cap|
      no_attest = cap.attenuate(Zxv::Capability::BITS.keys - [:attest])
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::QuarantineLog.new(no_attest, capacity: 4) }
      assert_raises(Zxv::ZxvUsageError) { Zxv::QuarantineLog.new(cap, capacity: 0) }
      assert_raises(Zxv::ZxvUsageError) { Zxv::QuarantineLog.new(cap, capacity: Zxv::QuarantineLog::MAX_RECORDS + 1) }
      log = Zxv::QuarantineLog.new(cap, capacity: 4)
      assert_raises(Zxv::ZxvUsageError) do
        log.append(H.of(:true, :neutral, :emerging), reason: 1, submitter: 1, subject: "\0".b * 32)
      end
      assert_raises(Zxv::ZxvUsageError) { log.append(forb, reason: 1, submitter: 1, subject: "\0".b * 31) }
      assert_raises(Zxv::ZxvUsageError) { log.append(forb, reason: 256, submitter: 1, subject: "\0".b * 32) }
      assert_raises(Zxv::ZxvUsageError) { log.append(forb.tag, reason: 1, submitter: 1, subject: "\0".b * 32) }
      assert_equal 0, log.append(forb, reason: 9, submitter: 0xFFFF_FFFF, subject: "\0".b * 32)
      assert_equal 0xFFFF_FFFF, log[0].submitter
      ctx.halt("test")
      assert_raises(Zxv::ZxvFatal) { log.count }
    end
  end
end
