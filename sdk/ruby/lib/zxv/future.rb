# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # A small future: work runs on its own Ruby thread; #value waits for it.
  # Returned by Budget#consume_async, DeferredEvent#await_cleared and
  # Session#generate_async.
  #
  #   f = budget.consume_async(5, timeout: 2)
  #   f.value          # waits; raises what the work raised
  #   f.cancel         # the waiter stops and #value raises CancelledError
  class Future
    # @api private
    # The block receives the future (to poll #cancelled?).
    def initialize(&work)
      @cancelled = false
      @on_cancel = []
      @lock = Mutex.new
      @thread = Thread.new do
        Thread.current.report_on_exception = false
        work.call(self)
      end
    end

    # Wait for the result. +timeout+ (seconds, Integer or ::Rational) limits
    # this wait only.
    # @raise [TimeoutError] if it does not finish in time
    # @raise [CancelledError] after #cancel
    def value(timeout = nil)
      t = timeout.nil? ? nil : seconds(timeout)
      raise TimeoutError, "future did not complete in #{timeout} s" if @thread.join(t).nil?

      @thread.value
    end

    # Wait without fetching the value. @return [Boolean] finished in time
    def wait(timeout = nil)
      !@thread.join(timeout.nil? ? nil : seconds(timeout)).nil?
    end

    # @return [Boolean]
    def done? = !@thread.alive?

    # Ask the work to stop. It stops at its next wake-up and the future
    # completes with CancelledError.
    def cancel
      hooks = @lock.synchronize do
        @cancelled = true
        @on_cancel.dup
      end
      hooks.each(&:call)
      self
    end

    # @return [Boolean]
    def cancelled? = @cancelled

    # @api private: run +hook+ when cancelled (to wake a waiter).
    def on_cancel(&hook)
      run_now = @lock.synchronize do
        @on_cancel << hook unless @cancelled
        @cancelled
      end
      hook.call if run_now
    end

    private

    def seconds(x)
      raise FloatRejectedError, "timeout: pass an Integer or ::Rational number of seconds" if x.is_a?(Float)

      x
    end
  end
end
