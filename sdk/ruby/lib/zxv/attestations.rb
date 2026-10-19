# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # An attestation registry (kernel lpres): named facts with four-valued
  # state and evidence counts. Actuator authority requires a clear #gate.
  #
  #   att = Zxv::Attestations.new(cap)
  #   i = att.attest("model.gguf", subject: 6, state: Zxv::Truth::TRUE, attester: "loader")
  #   att.evidence(i, positive: false)
  #   att.state(i).truth   # => Zxv::Truth::BOTH
  #   att.gate_clear?      # => false
  class Attestations < NativeHandle
    # State of one attestation.
    State = Struct.new(:truth, :positive, :negative, keyword_init: true)

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :attest for attest/evidence
    def initialize(capability)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_lpres_create_cap(capability.ptr, out), capability.context)
      super(capability.context, out.read_pointer, :zxv_lpres_destroy)
    end

    # Record an attestation; returns its index.
    # @param name [String] <= 31 bytes
    # @param subject [Integer] lpres_subject_type_t (0 SOURCE .. 14 FIRMWARE)
    # @param state [Truth]
    # @param attester [String] <= 31 bytes
    # @param digest [String, nil] <= 32 bytes
    def attest(name, subject:, state:, attester:, digest: nil)
      raise ZxvUsageError.new(Status::E_ARG, "state must be a Zxv::Truth") unless state.is_a?(Truth)

      d = digest ? Call.bytes!(digest, "digest") : "".b
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_lpres_attest(capability.ptr, ptr, Call.text!(name, "name"), Call.int!(subject, "subject", min: 0, max: 2**31 - 1),
                                             state.value, Call.text!(attester, "attester"), Call.mem(d, allow_null: true),
                                             d.bytesize, out), context)
      out.read_uint32
    end

    # Add one piece of evidence (positive or negative).
    def evidence(index, positive:)
      Call.check(Native.lib.zxv_lpres_evidence(capability.ptr, ptr, Call.uint32!(index, "index"), positive ? 1 : 0), context)
      self
    end

    # Index of a named attestation, or nil.
    def find(name)
      out = Call.out(:uint32)
      rc = Native.lib.zxv_lpres_find(ptr, Call.text!(name, "name"), out)
      return nil if rc == Status::E_NOT_FOUND

      Call.check(rc, context)
      out.read_uint32
    end

    # @return [State]
    def state(index)
      st = Call.out(:int32)
      pos = Call.out(:uint32)
      neg = Call.out(:uint32)
      Call.check(Native.lib.zxv_lpres_state(ptr, Call.uint32!(index, "index"), st, pos, neg), context)
      State.new(truth: Truth.of(st.read_int32), positive: pos.read_uint32, negative: neg.read_uint32)
    end

    # True when every attestation is certain (TRUE or FALSE): no BOTH, no NEITHER.
    def gate_clear?
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_lpres_gate(ptr, out), context)
      out.read_int32 == 1
    end
  end
end
