# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Owns native handles created under one context and frees them in reverse
  # creation order (children before parents, as R6 requires). Shared with
  # the context's finalizer, so it must not reference the Context itself.
  # @api private
  class HandleRegistry
    def initialize
      @lock = Mutex.new
      @entries = {} # id => [destroy_fn, pointer]
      @next = 0
    end

    def add(destroy_fn, pointer)
      @lock.synchronize do
        id = (@next += 1)
        @entries[id] = [destroy_fn, pointer]
        id
      end
    end

    def release(id)
      entry = @lock.synchronize { @entries.delete(id) }
      Native.lib.public_send(entry[0], entry[1]) if entry
    end

    def release_all
      entries = @lock.synchronize do
        e = @entries.sort_by { |k, _| -k }.map(&:last)
        @entries.clear
        e
      end
      entries.each { |fn, ptr| Native.lib.public_send(fn, ptr) }
    end
  end

  # Base of every wrapper of a native handle created under a Context.
  # Call #close (or use the block forms) to free it early; anything left is
  # freed when its Context closes.
  class NativeHandle
    # @return [Context] the owning context
    attr_reader :context

    # @api private
    def initialize(context, pointer, destroy_fn)
      @context = context
      @pointer = pointer
      @reg_id = context.send(:registry).add(destroy_fn, pointer)
    end

    # Free the native handle now. Idempotent.
    def close
      return if @pointer.nil?

      @context.send(:registry).release(@reg_id)
      @pointer = nil
    end

    # @return [Boolean]
    def closed? = @pointer.nil?

    # @api private
    def ptr
      raise ZxvUsageError.new(Status::E_STATE, "#{self.class.name} is closed") if @pointer.nil?

      @context.send(:ensure_alive!)
      @pointer
    end
  end

  # One platform instance: a ledger, its currencies, and every handle made
  # from it. Thread-safe (the C context is internally serialized).
  #
  #   Zxv::Context.open do |ctx|
  #     cap = ctx.root_capability
  #     ...
  #   end
  class Context
    class << self
      # Create a context; with a block, yield it and close it afterwards.
      def open
        ctx = new
        return ctx unless block_given?

        begin
          yield ctx
        ensure
          ctx.close
        end
      end
    end

    def initialize
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_ctx_create(out))
      @ptr = out.read_pointer
      @registry = HandleRegistry.new
      @dead = false
      @retained = []
      ObjectSpace.define_finalizer(self, self.class.send(:finalizer, @registry, @ptr))
    end

    # Keep +obj+ (an FFI callback a native handle holds) referenced for as
    # long as this context, so the GC never frees code libzxv may still call.
    # @api private
    def retain(obj)
      @retained << obj
      obj
    end

    # A root capability holding +caps+ (default: :all, the ABI 1.1 bits;
    # pass :all_1_4 or add :hw_matrix / :hw_reconf for the hardware group).
    # @param caps [Symbol, Array<Symbol>, Integer] e.g. :all, :all_1_4, [:ledger_read, :evm]
    # @param process_id [Integer] recorded in the cap
    # @param identity [String, nil] 21 bytes (168-bit identity), or nil for zeros
    # @return [Capability]
    def root_capability(caps = :all, process_id: Process.pid, identity: nil)
      Capability.send(:create, self, Capability.bits(caps), process_id, identity)
    end

    # @return [Boolean] true once the context halted (FATAL)
    def poisoned?
      return true if @dead

      raise ZxvUsageError.new(Status::E_STATE, "context is closed") if @ptr.nil?

      out = Call.out(:int32)
      Call.check(Native.lib.zxv_ctx_is_poisoned(@ptr, out), self)
      @dead = true if out.read_int32 == 1
      @dead
    end

    # Halt the context on purpose (operator kill switch). Every later call
    # raises ZxvFatal. +reason+ <= 150 bytes.
    def halt(reason = nil)
      Call.check(Native.lib.zxv_ctx_halt(raw, reason && Call.text!(reason, "reason")), self)
      mark_dead!
      self
    end

    # Free the context and every handle made from it. Idempotent.
    def close
      return if @ptr.nil?

      ObjectSpace.undefine_finalizer(self)
      @registry.release_all
      Native.lib.zxv_ctx_destroy(@ptr)
      @ptr = nil
    end

    # @return [Boolean]
    def closed? = @ptr.nil?

    # --- currencies -----------------------------------------------------

    # Enable an ISO 4217 currency for ledger accounts. +minor_units+ only
    # asserts the expected value (a mismatch raises ZXV_E_CURRENCY).
    # +cap+ needs :ledger_post and must belong to this context (else
    # CapabilityDeniedError). Without +cap+ this is deprecated since ABI 1.2.
    def enable_currency(alpha, minor_units: nil, cap: nil)
      code = Call.text!(alpha, "alpha")
      mu = minor_units.nil? ? nil : Call.int!(minor_units, "minor_units", min: 0, max: 255)
      rc = if cap
             c = own_cap!(cap)
             mu.nil? ? Native.lib.zxv_ccy_enable_cap(c, code) : Native.lib.zxv_ccy_enable_with_minor_cap(c, code, mu)
           else
             deprecated_no_cap("enable_currency")
             mu.nil? ? Native.lib.zxv_ccy_enable(raw, code) : Native.lib.zxv_ccy_enable_with_minor(raw, code, mu)
           end
      Call.check(rc, self)
      currency(code)
    end

    # Register a platform-private unit (e.g. "VFV", 2). Usable in ledger
    # accounts, refused as an ISO 20022 currency. +cap+ needs :ledger_post;
    # without it this is deprecated since ABI 1.2.
    def register_private_currency(code, minor_units, cap: nil)
      c = Call.text!(code, "code")
      mu = Call.int!(minor_units, "minor_units", min: 0, max: 255)
      rc = if cap
             Native.lib.zxv_ccy_register_private_cap(own_cap!(cap), c, mu)
           else
             deprecated_no_cap("register_private_currency")
             Native.lib.zxv_ccy_register_private(raw, c, mu)
           end
      Call.check(rc, self)
      currency(c)
    end

    # An enabled currency, or nil.
    # @return [Currency, nil]
    def currency(code)
      minor = Call.out(:uint8)
      flags = Call.out(:uint32)
      rc = Native.lib.zxv_ccy_get(raw, Call.text!(code, "code"), minor, flags)
      return nil if rc == Status::E_NOT_FOUND || rc == Status::E_CURRENCY

      Call.check(rc, self)
      Currency.new(code, minor.read_uint8, flags.read_uint32)
    end

    # @return [Array<Currency>] the enabled currencies
    def currencies
      n = Call.out(:uint32)
      Call.check(Native.lib.zxv_ccy_enabled_count(raw, n), self)
      Array.new(n.read_uint32) do |i|
        rc, code = Call.text(raw, i) { |*a| Native.lib.zxv_ccy_enabled_at(*a) }
        Call.check(rc, self)
        currency(code)
      end
    end

    def inspect = "#<Zxv::Context#{' closed' if closed?}#{' POISONED' if @dead}>"

    # @api private
    def raw
      ensure_alive!
      @ptr
    end

    private

    attr_reader :registry

    # The pointer of +cap+, after checking it belongs to this context.
    def own_cap!(cap)
      raise ZxvUsageError.new(Status::E_ARG, "cap must be a Zxv::Capability") unless cap.is_a?(Capability)
      unless cap.context.equal?(self)
        raise CapabilityDeniedError.new(Status::E_CAP, "the capability belongs to a different context")
      end

      ensure_alive!
      cap.ptr
    end

    def deprecated_no_cap(what)
      warn("Zxv::Context##{what} without cap: is deprecated since ABI 1.2: pass cap: " \
           "(a Capability with :ledger_post)", uplevel: 2, category: :deprecated)
    end

    # Closed: a usage error. Dead (halted): ZxvFatal, raised by the SDK
    # itself, so even the few C calls that do not consult the poison flag
    # (creating budgets, clocks, registries, swarms, messages; reading their
    # state) are refused once the context halted.
    def ensure_alive!
      raise ZxvUsageError.new(Status::E_STATE, "context is closed") if @ptr.nil?
      return unless @dead

      err = ZxvFatal.new(Status::E_POISONED, "context halted earlier; this handle is dead")
      Zxv.send(:on_fatal, err)
      raise err
    end

    # After a FATAL status the SDK keeps the native handle (so close still
    # frees it) but refuses every call on it and its children.
    def mark_dead!
      @dead = true
    end

    def self.finalizer(registry, ptr)
      proc do
        registry.release_all
        Native.lib.zxv_ctx_destroy(ptr)
      end
    end
    private_class_method :finalizer
  end

  # A capability: what a holder may do, checked by libzxv on EVERY gated
  # call. Objects that make gated calls (Ledger, Budgets, Swarm, ...) take
  # one at construction. Narrow with #attenuate; there is no widening and no
  # global "current capability".
  #
  # Honest limit: inside one process this is least-privilege discipline, not
  # a sandbox.
  class Capability < NativeHandle
    # Capability bits, by name.
    BITS = {
      ledger_read: 1 << 0,
      ledger_post: 1 << 1,
      crypto_sign: 1 << 2,
      crypto_kem: 1 << 3,
      ai_inference: 1 << 4,
      budget: 1 << 5,
      phase: 1 << 6,
      attest: 1 << 7,
      swarm: 1 << 8,
      evm: 1 << 9,
      hw_matrix: 1 << 10, # ABI 1.4: build and call the emulation matrix
      hw_reconf: 1 << 11  # ABI 1.4: load hardware images and contracts
    }.freeze
    # ZXV_CAP_ALL: the ten bits of ABI 1.1, a released value that never
    # changes (so +:all+ never silently grows).
    ALL = (1 << 10) - 1
    # ZXV_CAP_ALL_1_4: every bit as of ABI 1.4 (+:all_1_4+).
    ALL_1_4 = (1 << 12) - 1

    # Snapshot of a capability (zxv_cap_info).
    Info = Struct.new(:bits, :names, :process_id, :attestation, :revoked, keyword_init: true)

    # Bit mask from symbols (:all, :ledger_read, ...), an Array of them, or an Integer.
    def self.bits(spec)
      case spec
      when :all then ALL
      when :all_1_4 then ALL_1_4
      when Symbol then BITS.fetch(spec) { raise ZxvUsageError.new(Status::E_ARG, "unknown capability #{spec.inspect}") }
      when Array then spec.reduce(0) { |m, s| m | bits(s) }
      else Call.uint64!(spec, "caps")
      end
    end

    def self.create(context, bits, process_id, identity)
      id = identity && Call.mem(Call.bytes!(identity, "identity", size: 21))
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_cap_create(context.raw, Call.uint32!(process_id, "process_id"), id, bits, out), context)
      new(context, out.read_pointer)
    end
    private_class_method :create

    # @api private
    def initialize(context, pointer)
      super(context, pointer, :zxv_cap_destroy)
    end

    # A child holding (self & keep). The parent must stay open while the
    # child is used.
    # @param keep [Symbol, Array<Symbol>, Integer]
    # @return [Capability]
    def attenuate(*keep)
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_cap_attenuate(ptr, Capability.bits(keep.flatten), out), context)
      Capability.new(context, out.read_pointer)
    end

    # Permanently revoke this capability and every descendant.
    def revoke
      Call.check(Native.lib.zxv_cap_revoke(ptr), context)
      self
    end

    # Set the attestation. Anything but Truth::TRUE blocks gated calls.
    # @param truth [Truth]
    def attestation=(truth)
      raise ZxvUsageError.new(Status::E_ARG, "attestation must be a Zxv::Truth") unless truth.is_a?(Truth)

      Call.check(Native.lib.zxv_cap_set_attestation(ptr, truth.value), context)
    end

    # @return [Truth]
    def attestation = info.attestation

    # @return [Info]
    def info
      caps = Call.out(:uint64)
      pid = Call.out(:uint32)
      att = Call.out(:int32)
      rev = Call.out(:int32)
      Call.check(Native.lib.zxv_cap_info(ptr, caps, pid, att, rev), context)
      b = caps.read_uint64
      Info.new(bits: b, names: BITS.select { |_, v| b & v != 0 }.keys, process_id: pid.read_uint32,
               attestation: Truth.of(att.read_int32), revoked: rev.read_int32 == 1)
    end

    # Raise CapabilityDeniedError unless a call needing +needed+ would pass.
    def check(*needed)
      Call.check(Native.lib.zxv_cap_check(ptr, Capability.bits(needed.flatten)), context)
      self
    end

    # @return [Boolean] whether a call needing +needed+ would pass
    def allows?(*needed)
      Native.lib.zxv_cap_check(ptr, Capability.bits(needed.flatten)) == Status::OK
    end

    def inspect = closed? ? "#<Zxv::Capability closed>" : "#<Zxv::Capability #{info.names.join(',')}>"
  end
end
