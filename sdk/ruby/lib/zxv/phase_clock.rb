# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # The K6 phase coordinator's logical clock. ZXV time is a tick count, not
  # wall-clock time; one harmonic fundamental is CYCLE (27,720) ticks.
  #
  # Two drivers: manual (#advance, for deterministic tests and simulations)
  # and real time (#start_realtime(ticks_per_second), a Ruby thread).
  #
  #   clock = Zxv::PhaseClock.new(cap)
  #   ev = clock.defer(Zxv::PhaseClock::CYCLE)
  #   done = ev.await_cleared        # a Future
  #   clock.advance(Zxv::PhaseClock::CYCLE)
  #   done.value                     # => ev
  class PhaseClock < NativeHandle
    CYCLE = 27_720
    DECISIONS = %i[admit veto defer retry].freeze
    STEP_REQUIRES_HEALTH = 0x1
    STEP_HARDWARE = 0x2
    STEP_S0_PENDING = 0x4
    HEALTH = { unknown: 0, healthy: 1, degraded: 2, unhealthy: 3 }.freeze

    # Result of #admit: +decision+ is :admit, :veto, :defer or :retry;
    # +token+ is non-zero only on :admit.
    Admission = Struct.new(:decision, :token, keyword_init: true) do
      # @return [Boolean]
      def admitted? = decision == :admit
    end

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :phase for advance/admit/defer
    def initialize(capability)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_phase_create_cap(capability.ptr, out), capability.context)
      super(capability.context, out.read_pointer, :zxv_phase_destroy)
      @lock = Mutex.new
      @changed = ConditionVariable.new
      @driver = nil
    end

    # @return [Integer] the current tick
    def now
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_phase_now(ptr, out), context)
      out.read_uint64
    end

    # Advance by +delta+ ticks; deferred events due by then clear.
    # @return [Integer] how many events cleared
    def advance(delta)
      n = Call.out(:uint64)
      c = Call.out(:uint32)
      Call.check(Native.lib.zxv_phase_advance(capability.ptr, ptr, Call.uint64!(delta, "delta"), n, c), context)
      @lock.synchronize { @changed.broadcast }
      c.read_uint32
    end

    # Ask the coordinator to admit a step of phase +phase_id+ (1..13).
    # @param flags [Integer] STEP_* bits
    # @return [Admission]
    def admit(phase_id, step_id, flags = 0)
      d = Call.out(:int32)
      t = Call.out(:uint64)
      Call.check(Native.lib.zxv_phase_admit(capability.ptr, ptr, Call.uint32!(phase_id, "phase_id"),
                                            Call.uint32!(step_id, "step_id"), Call.uint32!(flags, "flags"), d, t), context)
      Admission.new(decision: DECISIONS.fetch(d.read_int32), token: t.read_uint64)
    end

    # Report a phase's health (:unknown, :healthy, :degraded, :unhealthy).
    def set_health(phase_id, health)
      Call.check(Native.lib.zxv_phase_set_health(capability.ptr, ptr, Call.uint32!(phase_id, "phase_id"),
                                                 HEALTH.fetch(health)), context)
      self
    end

    # Park an S0 event until absolute tick +due_tick+ (> now).
    # @return [DeferredEvent]
    def defer(due_tick)
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_phase_defer(capability.ptr, ptr, Call.uint64!(due_tick, "due_tick"), out), context)
      DeferredEvent.new(self, out.read_uint64)
    end

    # Earliest due tick of a pending event, or nil.
    def next_due
      out = Call.out(:uint64)
      rc = Native.lib.zxv_phase_next_due(ptr, out)
      return nil if rc == Status::E_NOT_FOUND

      Call.check(rc, context)
      out.read_uint64
    end

    # Drive the clock from wall time: a thread advances it by
    # +ticks_per_second+ (an Integer) per second, in small steps.
    # @return [self]
    def start_realtime(ticks_per_second)
      tps = Call.int!(ticks_per_second, "ticks_per_second", min: 1)
      stop_realtime
      @driver = Thread.new do
        Thread.current.report_on_exception = false
        t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
        done = 0
        loop do
          sleep(Kernel.Rational(1, [tps, 100].min))
          elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond) - t0
          due = elapsed * tps / 1_000_000_000
          if due > done
            advance(due - done)
            done = due
          end
        end
      end
      self
    end

    # Stop the real-time driver (if any).
    def stop_realtime
      @driver&.kill&.join
      @driver = nil
      self
    end

    # @return [Boolean]
    def realtime? = !@driver.nil?

    def close
      stop_realtime
      super
    end

    # @api private
    def wait_for_change(seconds)
      @lock.synchronize { @changed.wait(@lock, seconds) }
    end

    # @api private
    def wake_waiters
      @lock.synchronize { @changed.broadcast }
    end
  end

  # An S0 event parked on a PhaseClock.
  class DeferredEvent
    STATES = %i[pending cleared cancelled].freeze

    # @return [Integer] the event id
    attr_reader :id
    # @return [PhaseClock]
    attr_reader :clock

    # @api private
    def initialize(clock, id)
      @clock = clock
      @id = id
    end

    # @return [Symbol] :pending, :cleared or :cancelled
    def state = info[0]
    # @return [Integer]
    def due_tick = info[1]
    # @return [Boolean]
    def cleared? = state == :cleared

    # Cancel the event (waiters fail with CancelledError).
    def cancel
      Call.check(Native.lib.zxv_phase_cancel(clock.capability.ptr, clock.ptr, id), clock.context)
      clock.wake_waiters
      self
    end

    # A Future that completes (with this event) when the clock passes its tick.
    # @param timeout [Integer, ::Rational, nil] seconds
    def await_cleared(timeout: nil)
      raise FloatRejectedError, "timeout: pass Integer or ::Rational seconds" if timeout.is_a?(Float)

      start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      Future.new do |f|
        f.on_cancel { clock.wake_waiters }
        loop do
          case state
          when :cleared then break self
          when :cancelled then raise CancelledError, "event #{id} was cancelled"
          end
          raise CancelledError, "await_cleared cancelled" if f.cancelled?
          if timeout && Process.clock_gettime(Process::CLOCK_MONOTONIC) - start >= timeout
            raise TimeoutError, "event #{id} not cleared within #{timeout} s"
          end

          clock.wait_for_change(0.25)
        end
      end
    end

    private

    def info
      st = Call.out(:int32)
      due = Call.out(:uint64)
      Call.check(Native.lib.zxv_phase_event(clock.ptr, id, st, due), clock.context)
      [STATES.fetch(st.read_int32), due.read_uint64]
    end
  end
end
