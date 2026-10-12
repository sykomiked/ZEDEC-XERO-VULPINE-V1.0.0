# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Base of the ABI 1.5 wrappers of native handles that hold NO context
  # (treaties, freight joins and op-stream writers, Web 4 directories,
  # manifests and consent logs). They are plain data: no capability is
  # needed, a halted context does not affect them, and the GC frees them
  # (through an FFI::AutoPointer) if #close is never called.
  #
  # Use after #close is refused with ZxvUsageError (ZXV_E_STATE), never a
  # crash.
  class PureHandle
    # Free the native handle now. Idempotent.
    # @return [void]
    def close
      return if @ptr.nil?

      @ptr.free
      @ptr = nil
    end

    # @return [Boolean] true once #close ran
    def closed? = @ptr.nil?

    # @api private
    def ptr
      raise ZxvUsageError.new(Status::E_STATE, "#{self.class.name} is closed") if @ptr.nil?

      @ptr
    end

    class << self
      private

      # Run +block+ with a fresh out-pointer, check its status and wrap the
      # handle it wrote in a new instance (without calling #initialize).
      def from_out(destroy_fn)
        out = Call.out(:pointer)
        Call.check(yield(out))
        allocate.tap { |h| h.send(:adopt, out.read_pointer, destroy_fn) }
      end
    end

    private

    def adopt(raw, destroy_fn)
      @ptr = FFI::AutoPointer.new(raw, Native.lib.method(destroy_fn))
      self
    end

    def create_handle(destroy_fn)
      out = Call.out(:pointer)
      Call.check(yield(out))
      adopt(out.read_pointer, destroy_fn)
    end
  end
end
