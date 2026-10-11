# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Two lockstep peers in one window. Peer A predicts its opponent's inputs,
# which reach it LAG ticks late, and rolls back when a guess was wrong; peer
# B runs strict lockstep. Left/right move player 0, space asserts its claim;
# player 1 is a scripted bot. The label compares the peers' final hashes.
#
# Time comes in as integer microseconds (Time.get_ticks_usec), never as the
# float `delta`. Positions become floats only in _draw, for the screen, and
# never go back into the simulation.
extends Node2D

const Game := preload("res://game.gd")
const LAG := 3

var a: Game
var b: Game
var _last_usec := 0
var _in_flight: Array = []  # [tick, bytes] on the way from B to A
var _rollbacks := 0
var _verdict := ZxvLockstep.TRUTH_NEITHER
var _label := Label.new()


func _ready() -> void:
	a = Game.new(true)
	b = Game.new(false)
	a.sim.clock_set_step(ZxvRational.from_ints(1, 30), 4)  # 30 ticks per second
	_label.position = Vector2(16, 16)
	add_child(_label)
	_last_usec = Time.get_ticks_usec()


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var due := a.sim.clock_feed_usec(now - _last_usec)
	_last_usec = now
	for i in maxi(due, 0):
		_tick()
	queue_redraw()


func _local_dir() -> int:
	if Input.is_key_pressed(KEY_RIGHT):
		return 1
	if Input.is_key_pressed(KEY_LEFT):
		return 255
	return 0


func _tick() -> void:
	var t := a.sim.tick() + 1
	var mine := PackedByteArray([_local_dir(), 1 if Input.is_key_pressed(KEY_SPACE) else 0])
	var bot := PackedByteArray([[1, 0, 255][(t / 20) % 3], 1 if t % 50 == 0 else 0])
	a.sim.submit_input(0, t, mine)
	b.sim.submit_input(0, t, mine)
	b.sim.submit_input(1, t, bot)
	_in_flight.push_back([t, bot])
	while not _in_flight.is_empty() and _in_flight[0][0] <= t - LAG:
		var late: Array = _in_flight.pop_front()
		a.sim.submit_input(1, late[0], late[1])
	if a.sim.rollback_pending_tick() >= 0:
		_rollbacks += 1
	a.sim.rollback_replay(a.step)
	a.sim.advance(a.step)
	b.sim.advance(b.step)
	var c := a.sim.confirmed_tick()
	_verdict = a.sim.compare(c, b.sim.state_hash(c))


func _draw() -> void:
	for p in Game.PLAYERS:
		for row in 2:
			var sim: ZxvLockstep = a.sim if row == 0 else b.sim
			# LOSSY, display only: the exact position becomes a float here.
			var x := sim.get_rational(p).to_float_lossy()
			var colour := Color.CORNFLOWER_BLUE if p == 0 else Color.CORAL
			draw_circle(Vector2(480.0 + x * 12.0, 180.0 + row * 160.0 + p * 40.0), 12.0, colour)
	var c := a.sim.confirmed_tick()
	var verdict := "in sync" if _verdict == ZxvLockstep.TRUTH_TRUE else (
		"DESYNC" if _verdict == ZxvLockstep.TRUTH_FALSE else "waiting")
	_label.text = "tick %d   confirmed %d   rollbacks %d   %s\nplayer 0 at %s (exact)   claim %s" % [
		a.sim.tick(), c, _rollbacks, verdict,
		a.sim.get_rational(0).text(), a.sim.get_hexagram(0).name()]
