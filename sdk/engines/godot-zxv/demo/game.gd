# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# The demo's game rules: a pure function of the state and the inputs. No
# floats, no clocks, no randomness, so every peer computes the same thing.
#
#   rational slot p   player p's position, moved by exactly 1/3 per tick
#   hexagram slot p   player p's claim: TRUE/negentropic when asserted,
#                     its phase advancing one step every tick
#
# Input, 2 bytes per player per tick: [direction (1 right, 255 left, 0 stay),
# assert bit].
extends RefCounted

const PLAYERS := 2
const INPUT_BYTES := 2
const HISTORY := 32

var sim := ZxvLockstep.new()
var _right: ZxvRational
var _left: ZxvRational


func _init(predict: bool) -> void:
	_right = ZxvRational.from_ints(1, 3)
	_left = ZxvRational.from_ints(-1, 3)
	_must(sim.setup(PLAYERS, PLAYERS, PLAYERS, INPUT_BYTES, HISTORY, predict), "setup")
	for p in PLAYERS:
		_must(sim.set_rational(p, ZxvRational.from_ints(p * 3, 1)), "position")
		var claim := ZxvHexagram.from_fields(
			ZxvHexagram.BASE_NEITHER, ZxvHexagram.POL_NEUTRAL, ZxvHexagram.PHASE_POTENTIAL)
		_must(sim.set_hexagram(p, claim), "claim")
	_must(sim.start(), "start")


# Called by ZxvLockstep.advance / rollback_replay for every tick it computes.
func step(_tick: int) -> int:
	for p in PLAYERS:
		var input: PackedByteArray = sim.input(p)
		var rc := ZxvLockstep.OK
		if input[0] == 1:
			rc = sim.add_rational(p, _right)
		elif input[0] == 255:
			rc = sim.add_rational(p, _left)
		if rc != ZxvLockstep.OK:
			return rc
		var claim: ZxvHexagram = sim.get_hexagram(p)
		if (input[1] & 1) != 0 and not claim.is_forbidden():
			claim = claim.with_state(ZxvHexagram.BASE_TRUE, ZxvHexagram.POL_NEGENTROPIC)
		rc = sim.set_hexagram(p, claim.advance())
		if rc != ZxvLockstep.OK:
			return rc
	return ZxvLockstep.OK


func _must(rc: int, what: String) -> void:
	if rc != ZxvLockstep.OK:
		push_error("%s: %s (%s)" % [what, ZxvLockstep.status_name(rc), sim.last_error()])
