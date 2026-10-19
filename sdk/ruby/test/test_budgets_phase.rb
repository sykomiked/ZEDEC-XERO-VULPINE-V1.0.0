# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestBudgetsPhase < Minitest::Test
  include ZxvTest
  R = Zxv::Rational

  def test_exact_budget_and_exhaustion
    with_context do |_ctx, cap|
      budgets = Zxv::Budgets.new(cap)
      b = budgets.budget("tokens", resource: :tokens, limit: 10)
      denied = []
      b.on_exhausted { |x| denied << x.name }
      b.consume(R.of(7, 2))
      b.consume(R.of(13, 2))
      err = assert_raises(Zxv::BudgetExhaustedError) { b.consume(R.of(1, 3)) }
      assert_equal "ZXV_E_BUDGET", err.status_name
      assert_equal ["tokens"], denied
      inv = assert_raises(Zxv::ZxvUsageError) { b.consume(-1) }
      assert_equal Zxv::Status::E_RMAG_INVALID, inv.status
      assert_raises(Zxv::FloatRejectedError) { b.consume(0.5) }
      s = b.status
      assert_equal R.of(0), s.remaining
      assert_equal R.of(10), s.used
      assert_equal 0, s.epoch
      b.release(Kernel.Rational(1, 2))
      assert_equal R.of(1, 2), b.remaining
      assert_equal 1, b.status.epoch
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Budgets.new(cap.attenuate(:phase)).budget("x", resource: :memory, limit: 1) }
    end
  end

  def test_consume_async_waits_for_replenish
    with_context do |_ctx, cap|
      b = Zxv::Budgets.new(cap).budget("gpu", resource: :compute_cycles, limit: 5)
      b.consume(5)
      f = b.consume_async(3, timeout: 5)
      refute f.wait(Kernel.Rational(1, 5)), "must wait while exhausted"
      b.release(2) # 2 free: not enough yet
      refute f.wait(Kernel.Rational(1, 5))
      b.limit = 8 # now 3 free
      assert_same b, f.value(5)
      assert_equal R.of(6), b.used # 5 - 2 released + 3
    end
  end

  def test_consume_async_timeout_cancel_and_invalid
    with_context do |_ctx, cap|
      b = Zxv::Budgets.new(cap).budget("io", resource: :bandwidth, limit: 1)
      b.consume(1)
      assert_raises(Zxv::TimeoutError) { b.consume_async(1, timeout: Kernel.Rational(1, 5)).value }
      f = b.consume_async(1)
      f.cancel
      assert_raises(Zxv::CancelledError) { f.value(5) }
      assert_raises(Zxv::ZxvUsageError) { b.consume_async(-1).value(5) }
    end
  end

  def test_phase_deferral_manual_clock
    with_context do |_ctx, cap|
      clock = Zxv::PhaseClock.new(cap)
      cycle = Zxv::PhaseClock::CYCLE
      assert_equal vectors["phase_cycle"], cycle
      ev = clock.defer(cycle)
      assert_equal cycle, clock.next_due
      waiter = ev.await_cleared(timeout: 5)
      assert_equal 0, clock.advance(cycle - 1)
      assert_equal :pending, ev.state
      refute waiter.wait(Kernel.Rational(1, 10))
      assert_equal 1, clock.advance(1)
      assert_same ev, waiter.value(5)
      assert ev.cleared?
      assert_equal cycle, clock.now
      assert_nil clock.next_due

      a = clock.admit(6, 1, Zxv::PhaseClock::STEP_S0_PENDING)
      assert_equal :defer, a.decision
      assert_equal 0, a.token
      clock.set_health(6, :healthy)
      a = clock.admit(6, 2, Zxv::PhaseClock::STEP_REQUIRES_HEALTH)
      assert a.admitted?
      refute_equal 0, a.token

      ev2 = clock.defer(clock.now + 10)
      w2 = ev2.await_cleared
      ev2.cancel
      assert_raises(Zxv::CancelledError) { w2.value(5) }
    end
  end

  def test_phase_realtime_driver
    with_context do |_ctx, cap|
      clock = Zxv::PhaseClock.new(cap)
      ev = clock.defer(50)
      clock.start_realtime(1000)
      assert_same ev, ev.await_cleared(timeout: 5).value
      clock.stop_realtime
      refute clock.realtime?
      assert_operator clock.now, :>=, 50
    end
  end
end
