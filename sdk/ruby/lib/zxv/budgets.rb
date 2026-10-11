# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # A set of exact rational resource budgets (kernel rmag, K2).
  #
  #   budgets = Zxv::Budgets.new(cap)
  #   tokens = budgets.budget("tokens", resource: :tokens, limit: 10)
  #   tokens.consume(Zxv::Rational.of(7, 2))
  #   tokens.remaining   # => 13/2
  class Budgets < NativeHandle
    RESOURCES = { compute_cycles: 0, memory: 1, bandwidth: 2, energy: 3, storage: 4, tokens: 12 }.freeze
    EV_DENIED = 1
    EV_INVALID = 2
    EV_REPLENISHED = 3

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :budget
    def initialize(capability)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_rmag_create_cap(capability.ptr, out), capability.context)
      super(capability.context, out.read_pointer, :zxv_rmag_destroy)
      @budgets = {}
      @lock = Mutex.new
      @changed = ConditionVariable.new
      # Kept referenced for as long as the native rmag lives.
      @listener = FFI::Function.new(:void, %i[pointer uint32 int32]) { |_u, b, ev| dispatch(b, ev) }
      Call.check(Native.lib.zxv_rmag_listen(ptr, @listener, nil), context)
    end

    # Create a budget with an exact limit (>= 0).
    # @param resource [Symbol] one of RESOURCES
    # @param limit [Integer, Zxv::Rational, ::Rational]
    # @param enforce [Boolean] false only counts
    # @return [Budget]
    def budget(name, resource:, limit:, enforce: true)
      r = RESOURCES.fetch(resource) { raise ZxvUsageError.new(Status::E_ARG, "unknown resource #{resource.inspect}") }
      l = Rational.coerce_arg(limit)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_rmag_budget(capability.ptr, ptr, Call.text!(name, "name"), r, l.numerator, l.denominator,
                                            enforce ? 1 : 0, out), context)
      b = Budget.new(self, out.read_uint32, name)
      @lock.synchronize { @budgets[b.id] = b }
      b
    end

    # @api private
    def wait_for_change(seconds)
      @lock.synchronize { @changed.wait(@lock, seconds) }
    end

    # @api private
    def wake_waiters
      @lock.synchronize { @changed.broadcast }
    end

    private

    def dispatch(budget_id, event)
      b = @lock.synchronize { @budgets[budget_id] }
      wake_waiters if event == EV_REPLENISHED
      b&.send(:fire, event)
    rescue StandardError => e
      warn "zxv: budget listener raised #{e.class}: #{e.message}"
    end
  end

  # One budget of a Budgets set.
  class Budget
    # @return [Integer] the budget id
    attr_reader :id
    # @return [String]
    attr_reader :name
    # @return [Budgets]
    attr_reader :budgets

    # Snapshot of a budget.
    Snapshot = Struct.new(:remaining, :used, :epoch, keyword_init: true)

    # @api private
    def initialize(budgets, id, name)
      @budgets = budgets
      @id = id
      @name = name
      @on_exhausted = []
    end

    # Consume an exact amount (> 0).
    # @raise [BudgetExhaustedError] it would pass an enforced limit (nothing consumed)
    # @raise [ZxvUsageError] ZXV_E_RMAG_INVALID for a negative/overflowing amount (never retry)
    def consume(amount)
      a = Rational.coerce_arg(amount)
      Call.check(Native.lib.zxv_rmag_consume(cap.ptr, budgets.ptr, id, a.numerator, a.denominator), budgets.context)
      self
    end

    # Consume, waiting while the budget is exhausted. Returns a Future that
    # completes when the consume succeeds; it retries each time a release or
    # a larger limit replenishes the budget (the epoch moves). Fails at
    # once on an invalid amount.
    # @param timeout [Integer, ::Rational, nil] seconds before TimeoutError
    # @return [Future]
    def consume_async(amount, timeout: nil)
      a = Rational.coerce_arg(amount)
      raise FloatRejectedError, "timeout: pass Integer or ::Rational seconds" if timeout.is_a?(Float)

      deadline = timeout && (now + timeout)
      Future.new do |f|
        f.on_cancel { budgets.wake_waiters }
        loop do
          raise CancelledError, "consume_async cancelled" if f.cancelled?

          epoch = status.epoch
          begin
            break consume(a)
          rescue BudgetExhaustedError
            # wait for a replenish below
          end
          loop do
            raise CancelledError, "consume_async cancelled" if f.cancelled?

            left = deadline && (deadline - now)
            raise TimeoutError, "budget #{name} not replenished within #{timeout} s" if left && left <= 0

            budgets.wait_for_change(left ? [left, 0.25].min : 0.25)
            break if status.epoch != epoch
          end
        end
      end
    end

    # Give back an amount (clamped at zero used); wakes async waiters.
    def release(amount)
      a = Rational.coerce_arg(amount)
      Call.check(Native.lib.zxv_rmag_release(cap.ptr, budgets.ptr, id, a.numerator, a.denominator), budgets.context)
      self
    end

    # Replace the limit; a larger limit wakes async waiters.
    def limit=(limit)
      l = Rational.coerce_arg(limit)
      Call.check(Native.lib.zxv_rmag_set_limit(cap.ptr, budgets.ptr, id, l.numerator, l.denominator), budgets.context)
    end

    # @return [Snapshot] remaining, used (Zxv::Rational) and the replenish epoch
    def status
      rn = Call.out(:int64)
      rd = Call.out(:int64)
      un = Call.out(:int64)
      ud = Call.out(:int64)
      ep = Call.out(:uint64)
      Call.check(Native.lib.zxv_rmag_status(budgets.ptr, id, rn, rd, un, ud, ep), budgets.context)
      Snapshot.new(remaining: Rational.of(rn.read_int64, rd.read_int64), used: Rational.of(un.read_int64, ud.read_int64),
                 epoch: ep.read_uint64)
    end

    # @return [Zxv::Rational]
    def remaining = status.remaining
    # @return [Zxv::Rational]
    def used = status.used

    # Register a block called (with this budget) on every denied consume.
    def on_exhausted(&block)
      @on_exhausted << block
      self
    end

    private

    def cap = budgets.capability
    def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    def fire(event)
      @on_exhausted.each { |cb| cb.call(self) } if event == Budgets::EV_DENIED
    end
  end
end
