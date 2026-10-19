# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # An ISO 20022 message under construction (pacs.008, camt.053; other kinds
  # raise Zxv::NotImplementedError until the kernel builds them).
  # Serialization only: libzxv has no network transport.
  #
  #   msg = Zxv::Message.new(cap, :pacs008)   # cap needs :ledger_read
  #   msg[:msg_id] = "M1"; msg[:ccy] = "NGN"; ...
  #   msg.amount(:settlement, 150_075, 2)
  #   xml = msg.render
  class Message < NativeHandle
    KINDS = { pacs008: 1, camt053: 2, pacs002: 3, pacs004: 4, pacs009: 5, camt056: 6, camt029: 7 }.freeze
    FIELDS = { msg_id: 1, cre_dt_tm: 2, debtor_name: 3, debtor_acct: 4, creditor_name: 5,
               creditor_acct: 6, end_to_end_id: 7, ccy: 8, acct_id: 9 }.freeze
    AMOUNTS = { settlement: 1, opening: 2, closing: 3 }.freeze
    DIRECTIONS = { credit: 0, debit: 1 }.freeze
    STATUSES = { booked: 0, pending: 1, info: 2 }.freeze

    # @return [Capability, nil] the capability the message re-checks on every call
    attr_reader :capability

    # @param owner [Capability] needs :ledger_read; the message re-checks it on
    #   every call. Passing a Context is deprecated since ABI 1.2 (no capability).
    # @param kind [Symbol] one of KINDS
    def initialize(owner, kind)
      k = KINDS.fetch(kind) { raise ZxvUsageError.new(Status::E_ARG, "unknown message kind #{kind.inspect}") }
      out = Call.out(:pointer)
      if owner.is_a?(Capability)
        @capability = owner
        Call.check(Native.lib.zxv_msg_create_cap(owner.ptr, k, out), owner.context)
        super(owner.context, out.read_pointer, :zxv_msg_destroy)
      else
        warn("Zxv::Message.new(context, kind) is deprecated since ABI 1.2: pass a Capability " \
             "with :ledger_read", uplevel: 1, category: :deprecated)
        Call.check(Native.lib.zxv_msg_create(owner.raw, k, out), owner)
        super(owner, out.read_pointer, :zxv_msg_destroy)
      end
    end

    # @api private
    def ptr
      if @capability&.closed?
        raise ZxvUsageError.new(Status::E_STATE, "the capability of this Message is closed")
      end

      super
    end

    # Set a text field (plain text; the serializer escapes XML).
    def []=(field, value)
      f = FIELDS.fetch(field) { raise ZxvUsageError.new(Status::E_ARG, "unknown field #{field.inspect}") }
      Call.check(Native.lib.zxv_msg_set_text(ptr, f, Call.text!(value, field.to_s)), context)
    end

    # Set an amount in exact minor units with +frac_digits+ decimals.
    def amount(field, units, frac_digits)
      f = AMOUNTS.fetch(field) { raise ZxvUsageError.new(Status::E_ARG, "unknown amount #{field.inspect}") }
      Call.check(Native.lib.zxv_msg_set_amount(ptr, f, Call.int!(units, "units"),
                                               Call.int!(frac_digits, "frac_digits", min: 0, max: 255)), context)
      self
    end

    # pacs.008 GrpHdr/NbOfTxs (default 1).
    def tx_count=(n)
      Call.check(Native.lib.zxv_msg_set_tx_count(ptr, Call.uint32!(n, "tx_count")), context)
    end

    # Append a camt.053 statement entry.
    def add_entry(units, frac_digits, direction: :credit, status: :booked)
      Call.check(Native.lib.zxv_msg_add_entry(ptr, Call.int!(units, "units"),
                                              Call.int!(frac_digits, "frac_digits", min: 0, max: 255),
                                              DIRECTIONS.fetch(direction), STATUSES.fetch(status)), context)
      self
    end

    # Serialize to XML.
    # @return [String]
    def render
      rc, xml = Call.text(ptr) { |*a| Native.lib.zxv_msg_render(*a) }
      Call.check(rc, context)
      xml
    end
  end

  # PAN check digits (Phoenix: Damm, Dragon: Luhn, Thunderbird: Verhoeff).
  module CardCheck
    NETWORKS = { phoenix: 1, dragon: 2, thunderbird: 3 }.freeze

    # @return [Boolean] whether +pan+'s check digit is valid for +network+
    def self.valid?(network, pan)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_card_check_digit(NETWORKS.fetch(network), Call.text!(pan, "pan"), out))
      out.read_int32 == 1
    end
  end
end
