# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 conformance: the paradox section of vectors.json. Every book
# step runs in order on ONE book (log capacity 64, registry capacity 16),
# exactly as the generator ran them, so the log heads must match.
class TestAbi15Paradox < Minitest::Test
  include ZxvTest
  P = Zxv::Paradox

  def pv = vectors.fetch("paradox")

  def kind(i) = P::KINDS.fetch(i)
  def outcome(i) = P::OUTCOMES.fetch(i)
  def sig_ref(ref) = ref && hex(pv["signature"][Integer(ref[/\d+/])]["sig"])
  def id(letter) = letter * 32

  def test_pure_tables
    pv["signature"].each { |v| assert_equal v["sig"], unhex(P.signature(v["utf8"])) }
    pv["kind_names"].each_with_index { |n, i| assert_equal n, P.kind_name(i) }
    pv["outcome_names"].each_with_index { |n, i| assert_equal n, P.outcome_name(i) }
    pv["node_state_names"].each_with_index { |n, i| assert_equal n, P.node_state_name(i) }
    assert_equal pv["kind_names"].map { |n| n.tr("-", "_").to_sym }, P::KINDS
    assert_equal pv["outcome_names"].map { |n| n.tr("-", "_").to_sym }, P::OUTCOMES
    assert_equal pv["node_state_names"].map { |n| n.tr("-", "_").to_sym }, P::NODE_STATES
    pv["kind_of_tag"].each_with_index { |k, t| assert_equal kind(k), P.kind_of_tag(t) }
    pv["action"].each_with_index do |row, k|
      row.each_with_index do |d, t|
        if d == -1
          expect_status("ZXV_E_ARG") { P.action(k, t) }
        else
          assert_equal P::DOS.fetch(d), P.action(k, t)
        end
      end
    end
    pv["outcome_action"].each_with_index do |a, o|
      assert_equal Zxv::Hexagram::ACTIONS.fetch(a), P.outcome_action(o)
    end
  end

  def heads(book, expected)
    expected.each do |w, h|
      assert_equal h["count"], book.log_count(w.to_sym), w
      assert_equal h["head"], unhex(book.log_head(w.to_sym)), w
    end
  end

  def test_book_scenario
    with_context15 do |_ctx, cap|
      book = P::Book.new(cap, log_capacity: 64, registry_capacity: 16)
      heads(book, pv["heads_fresh"])
      book_classify(book)
      heads(book, pv["heads_after_classify"])
      book_constructive(cap, book)
      book_destructive(book)
      book_neutral(cap, book)
      book_special(book)
      book_graphs(cap, book)
      heads(book, pv["heads_final"])
      pv["logs"].each do |w, recs|
        assert_equal recs, book.log_records(w.to_sym).map { |r| unhex(r.bytes) }, w
      end
      q = book.log_record(:quarantine, 0)
      assert_equal 0, q.seq
      assert_equal %i[steps cycle], q.reason
      assert_equal 7, q.submitter
      op = book.log_record(:ops, 0)
      assert_equal [0, :destructive], [op.seq, op.kind]
      pv["log_verify"].zip(P::LOGS.keys).each do |st, w|
        expect_status(st) { book.verify_log(w) }
      end
      book.close
    end
  end

  def book_classify(book)
    pv["classify"].each do |v|
      if (r = v["registry_add"])
        idx = expect_status(r["status"]) do
          book.registry_add(sig_ref(r["sig"]), kind: r["kind"], reason: r["reason"], flags: r["flags"], owner_ok: r["owner_ok"])
        end
        assert_equal r["index"], idx
        next
      end
      c = expect_status(v["status"]) do
        book.classify(v["tag"], evidence: v["evidence"], stale: v["stale"], signature: sig_ref(v["sig"]))
      end
      assert_equal kind(v["kind"]), c.kind
      assert_equal v["out_tag"], c.tag.tag
      assert_eq(v["reg_index"].negative? ? nil : v["reg_index"], c.registry_index)
      # Without a registry hit, the bookless classifier agrees.
      assert_equal c.to_h, P.classify(v["tag"], evidence: v["evidence"], stale: v["stale"]).to_h unless v["sig"]
    end
    sig_m = P.signature("money:drain")
    pv["registry"].each do |v|
      idx = expect_status(v["status"]) do
        book.registry_add(sig_m, kind: v["kind"], reason: 2, flags: v["flags"], owner_ok: v["owner_ok"])
      end
      assert_equal v["index"], idx if v["status"] == "ZXV_OK"
    end
    assert_equal 1, book.registry_find(sig_m)
    assert_nil book.registry_find(P.signature("unknown"))
    e = book.registry_entry(1)
    assert_equal [sig_m, :neutral, 2, [:money]], [e.signature, e.kind, e.reason, e.flags]
  end

  def book_constructive(cap, book)
    c = pv["constructive"]
    lo, hi, init = %w[lo hi initial].map { |k| c[k].map { |x| Integer(x) } }
    pols = { "plain" => P::Policy.new(cap, lo: lo, hi: hi, initial: init),
             "money" => P::Policy.new(cap, lo: lo, hi: hi, initial: init, flags: :money) }
    assert_equal [lo, hi], pols["plain"].bounds
    assert_equal [:money], pols["money"].flags
    c["steps"].each_with_index do |s, i|
      pol = pols.fetch(s["policy"])
      synth = s["synth"]&.map { |x| Integer(x) }
      got = expect_status(s["status"]) do
        book.constructive(pol, kind: s["kind"], tag: s["tag"], synth: synth, tension: s["tension"])
      end
      assert_equal outcome(s["outcome"]), got if s["status"] == "ZXV_OK"
      assert_equal s["values"].map { |x| Integer(x) }, pol.values, "step #{i}"
      assert_equal s["prev"].map { |x| Integer(x) }, pol.previous, "step #{i}"
      assert_equal s["armed"], pol.armed?, "step #{i}"
      assert_equal s["tension_at"], pol.tension_at, "step #{i}"
      assert_equal s["proposal"], unhex(pol.proposal(synth)) if s.key?("proposal")
    end
    pols.each_value(&:close)
  end

  def book_destructive(book)
    pv["destructive"].each do |d|
      r = expect_status(d["status"]) do
        book.destructive(kind: d["kind"], tag: d["tag"], steps: d["steps"], step_limit: d["step_limit"], spent: d["spent"],
                         budget: d["budget"], trace: d["trace"].map { |x| u64(x) }, invariant_ok: d["invariant_ok"],
                         state: d["state_utf8"], checkpoint: d["checkpoint_utf8"], pattern: d["pattern_utf8"],
                         submitter: d["submitter"])
      end
      assert_equal outcome(d["outcome"]), r.outcome
      assert_equal d["state_after"], unhex(r.state)
      next assert_nil(r.proof) unless d.key?("repair_proof")

      assert_equal d["repair_proof"], unhex(r.proof.bytes)
      assert_equal d["repair_verify"] == "ZXV_OK", r.proof.valid?
      assert_equal P.signature(d["pattern_utf8"]), r.proof.signature
      assert_equal [10, 3, 7], [r.proof.steps, r.proof.spent, r.proof.submitter]
      flipped = r.proof.bytes.dup
      flipped.setbyte(0, flipped.getbyte(0) ^ 1)
      assert_equal d["repair_verify_flipped_byte0"] == "ZXV_OK", P::RepairProof.new(flipped).valid?
    end
  end

  def book_neutral(cap, book)
    n = pv["neutral"]
    hold = P::Hold.new(cap, capacity: n["capacity"], decisive: n["decisive"])
    b0, b1 = n["branches"].map { |x| hex(x) }
    hold.add(b0).add(b1)
    hold.evidence(b0, positive: true).evidence(b1, positive: true)
    assert_equal [[b0, 1, 0], [b1, 1, 0]], hold.branches.map { |b| [b.id, b.for, b.against] }
    assert_nil hold.decisive_branch # 1 for each: neither leads by 2
    n["steps"].drop(1).each do |s|
      s["add_for_branch0"].times { hold.evidence(b0, positive: true) }
      pick = s["owner_pick"].negative? ? nil : s["owner_pick"]
      oc, settled = expect_status(s["status"]) { book.neutral(hold, kind: s["kind"], tag: s["tag"], owner_pick: pick) }
      assert_equal outcome(s["outcome"]), oc
      assert_eq(s["settled"].negative? ? nil : s["settled"], settled)
    end
    assert_equal 0, hold.settled
    hold.close
  end

  def book_special(book)
    s = pv["special"]
    assert_equal s["sig"], unhex(P.signature(s["sig_utf8"]))
    oc, rec = expect_status(s["status"]) do
      book.special(kind: s["kind"], tag: s["tag"], signature: hex(s["sig"]), subject: hex(s["subject"]))
    end
    assert_equal [outcome(s["outcome"]), s["rec"]], [oc, rec]
    drafts = %w[draft draft_money].to_h do |k|
      d = s[k]
      got = expect_status(d["status"]) { book.draft(rec, kind: d["kind"], reason: d["reason"], flags: d["flags"]) }
      assert_equal d["bytes"], unhex(got.bytes)
      [k, got]
    end
    assert_equal [:destructive, 3, []], [drafts["draft"].kind, drafts["draft"].reason, drafts["draft"].flags]
    assert_equal [:money], drafts["draft_money"].flags
    s["review"].each do |r|
      got = expect_status(r["status"]) do
        book.review(drafts.fetch(r["draft"]), owner_ok: r["owner_ok"], consensus_ok: r["consensus_ok"])
      end
      assert_equal outcome(r["outcome"]), got
    end
    assert_equal s["registry_count_after"], book.registry_size
  end

  def book_graphs(cap, book)
    pv["graphs"].each do |gv|
      g = P::Graph.new(cap, step_limit: gv["step_limit"])
      gv["nodes"].each do |n|
        expect_status(n["status"]) { g.add_node(id(n["id"]), kind: n["kind"], tag: n["tag"], mag: n["mag"], dir: n["dir"]) }
      end
      gv["deps"].each do |d|
        expect_status(d["status"]) { g.add_dependency(id(d["id"]), id(d["dep"])) }
      end
      gv["passes"].each do |p|
        got = expect_status(p["status"]) { book.pass(g) }
        assert_equal outcome(p["outcome"]), got if p["status"] == "ZXV_OK"
        info = g.info
        assert_equal [p["pass"], p["total_mag"], p["total_dir"], p["escalated"]],
                     [info.passes, info.total_mag, info.total_dir, info.escalated], gv["label"]
        assert_equal p["chain"], unhex(info.chain)
        p["states"].each { |letter, st| assert_equal P::NODE_STATES.fetch(st), g.state(id(letter)), "#{gv['label']} #{letter}" }
      end
      assert_equal gv["nodes"].size, g.nodes.size
      g.close
    end
  end
end
