# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Headless checks of the GDExtension inside a real Godot:
#   godot --headless --path demo --import          (once: registers the extension)
#   godot --headless --path demo --script res://tests/test_zxv.gd
# Exits with the number of failed checks (0 = all passed). The refusals
# below print "ERROR:" lines on purpose.
extends SceneTree

const Game := preload("res://game.gd")

var checks := 0
var failed := 0


func check(cond: bool, what: String) -> void:
	checks += 1
	if not cond:
		failed += 1
		printerr("FAIL: ", what)


func _initialize() -> void:
	test_rational()
	test_hexagram()
	test_lockstep()
	print("test_zxv.gd: %d checks, %d failed" % [checks, failed])
	quit(failed)


func test_rational() -> void:
	var a := ZxvRational.parse("0.1")
	var b := ZxvRational.parse("0.2")
	check(a.add(b).text() == "0.3", "0.1 + 0.2 == 0.3 exactly")
	check(a.add(b).equals(ZxvRational.from_ints(3, 10)), "3/10")
	check(ZxvRational.from_ints(1, 3).text() == "1/3", "1/3 stays a fraction")
	check(ZxvRational.from_ints(2, -6).fraction() == "-1/3", "reduced")
	check(str(ZxvRational.from_ints(5, 4)) == "1.25", "_to_string is exact text")
	check(ZxvRational.from_ints(1, 3).compare(ZxvRational.from_ints(1, 2)) == -1, "compare")
	# floats are refused, not truncated
	check(ZxvRational.from_ints(0.5, 1) == null, "from_ints(0.5, 1) refused")
	check(ZxvRational.from_float(0.5) == null, "from_float refused")
	check(ZxvRational.parse(0.5) == null, "parse(0.5) refused")
	check(ZxvRational.parse("1e3") == null, "exponent refused")
	check(ZxvRational.from_ints(1, 0) == null, "zero denominator refused")
	var big := ZxvRational.from_ints(9223372036854775807, 1)
	check(big.add(ZxvRational.from_ints(1, 1)) == null, "overflow refused, never wraps")
	var lossy := ZxvRational.from_ints(1, 3).to_float_lossy()
	check(lossy > 0.333 and lossy < 0.334, "to_float_lossy for drawing")


func test_hexagram() -> void:
	var h := ZxvHexagram.from_fields(ZxvHexagram.BASE_TRUE, ZxvHexagram.POL_NEGENTROPIC,
		ZxvHexagram.PHASE_MANIFEST)
	check(h.tag() == 63 and h.name() == "true/negentropic/manifest", "tag 63")
	check(h.action() == ZxvHexagram.ACT_PROCEED, "proceed")
	check(h.to_truth() == ZxvHexagram.TRUTH_TRUE, "base TRUE -> Truth TRUE (1)")
	check(ZxvHexagram.from_tag(64) == null, "tag 64 refused")
	var p := ZxvHexagram.from_fields(ZxvHexagram.BASE_TRUE, ZxvHexagram.POL_NEUTRAL,
		ZxvHexagram.PHASE_POTENTIAL)
	check(p.step(ZxvHexagram.PHASE_MANIFEST) == null, "phase jump refused")
	var f := p.forbid()
	check(f.is_forbidden() and f.action() == ZxvHexagram.ACT_ROLLBACK, "forbid -> rollback")
	check(f.with_state(ZxvHexagram.BASE_TRUE, ZxvHexagram.POL_NEUTRAL) == null, "forbidden absorbs")
	check(f.and_(p).is_forbidden() and f.or_(p).is_forbidden(), "forbidden absorbs and/or")
	for t in 64:
		var x := ZxvHexagram.from_tag(t)
		check(x.not_().not_().equals(x), "not not")


func test_lockstep() -> void:
	const LAG := 4
	const TICKS := 60
	var a := Game.new(true)
	var b := Game.new(false)
	var rollbacks := 0
	for t in range(1, TICKS + LAG + 1):
		if t <= TICKS:
			var mine := PackedByteArray([[1, 0, 255][t % 3], 1 if t % 7 == 0 else 0])
			var theirs := PackedByteArray([[255, 1, 0][(t / 5) % 3], 1 if t % 11 == 0 else 0])
			check(a.sim.submit_input(0, t, mine) == ZxvLockstep.OK, "submit a0")
			check(b.sim.submit_input(0, t, mine) == ZxvLockstep.OK, "submit b0")
			check(b.sim.submit_input(1, t, theirs) == ZxvLockstep.OK, "submit b1")
		if t > LAG:
			var u := t - LAG
			var late := PackedByteArray([[255, 1, 0][(u / 5) % 3], 1 if u % 11 == 0 else 0])
			check(a.sim.submit_input(1, u, late) == ZxvLockstep.OK, "late submit a1")
		if a.sim.rollback_pending_tick() >= 0:
			rollbacks += 1
		check(a.sim.rollback_replay(a.step) == ZxvLockstep.OK, "rollback_replay")
		if t <= TICKS:
			check(a.sim.advance(a.step) == ZxvLockstep.OK, "advance a")
			check(b.sim.advance(b.step) == ZxvLockstep.OK, "advance b")
	check(rollbacks > 0, "late inputs caused rollbacks")
	check(a.sim.confirmed_tick() == TICKS, "a confirmed to the end")
	for t in range(TICKS - 10, TICKS + 1):
		check(a.sim.state_hash(t) == b.sim.state_hash(t), "same hash at %d" % t)
		check(a.sim.compare(t, b.sim.state_hash(t)) == ZxvLockstep.TRUTH_TRUE, "compare TRUE")
	check(a.sim.get_rational(0).equals(b.sim.get_rational(0)), "same exact position")
	var bad := PackedByteArray()
	bad.resize(32)
	check(a.sim.compare(TICKS, bad) == ZxvLockstep.TRUTH_FALSE, "a wrong hash is a desync")
	check(a.sim.desync_tick() == TICKS, "desync recorded")
	# state transfer to a late joiner
	var c := ZxvLockstep.new()
	check(c.setup(Game.PLAYERS, Game.PLAYERS, Game.PLAYERS, Game.INPUT_BYTES, 8, false) == ZxvLockstep.OK, "setup c")
	check(c.import_state(b.sim.export_state()) == ZxvLockstep.OK, "import")
	check(c.state_hash(TICKS) == b.sim.state_hash(TICKS) and c.hash_is_final(TICKS), "joiner agrees")
	# a budget of one unit per tick refuses a second charge
	var d := ZxvLockstep.new()
	d.setup(1, 1, 0, 1, 4, false)
	check(d.set_tick_budget(ZxvRational.from_ints(1, 1)) == ZxvLockstep.OK, "budget")
	d.start()
	d.submit_input(0, 1, PackedByteArray([0]))
	var one := ZxvRational.from_ints(1, 1)
	var greedy := func(_t: int) -> int:
		d.charge(one)
		return d.charge(one)
	check(d.advance(greedy) == ZxvLockstep.E_BUDGET, "over budget refused")
	check(d.tick() == 0, "refused tick did not advance")
