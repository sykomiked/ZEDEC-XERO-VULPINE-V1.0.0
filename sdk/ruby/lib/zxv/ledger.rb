# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # The triple ledger of a context, through a capability. Amounts are exact
  # Integer minor units (cents, kobo, ...); every posting is balanced.
  #
  #   ledger = Zxv::Ledger.new(cap)
  #   bank  = ledger.open_account("bank", "NGN", kind: :issuer)
  #   alice = ledger.open_account("alice", "NGN")
  #   ledger.post(bank, alice, 50_000, "NGN", "E2E-1")
  #   ledger.balance(alice).equity  # => 50000
  class Ledger
    # Rail balances of an account (minor units): DEBIT 555 received,
    # CREDIT 777 sent, EQUITY 888 = debit - credit (the spendable position).
    Balance = Struct.new(:debit, :credit, :equity, keyword_init: true)
    # One journal line: +amount+ is signed (+ received, - sent).
    Entry = Struct.new(:entry_id, :amount, :counterparty, :reference, keyword_init: true)
    # Account metadata.
    Account = Struct.new(:id, :name, :currency, :kind, keyword_init: true)

    KINDS = { holder: 0, issuer: 1 }.freeze
    # Largest amount per posting and largest |balance|: 2**53 - 1.
    AMOUNT_MAX = 9_007_199_254_740_991

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :ledger_read and/or :ledger_post
    def initialize(capability)
      @capability = capability
    end

    # Open an account in an enabled currency; returns its id. Needs :ledger_post.
    # @param kind [Symbol] :holder (never below zero) or :issuer
    # @return [Integer]
    def open_account(name, currency, kind: :holder)
      k = KINDS.fetch(kind) { raise ZxvUsageError.new(Status::E_ARG, "kind must be :holder or :issuer") }
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_ledger_open_account_cap(capability.ptr, Call.text!(name, "name"), Call.text!(currency, "currency"), k, out), ctx)
      out.read_uint32
    end

    # Post a balanced transfer; returns the entry id.
    # @raise [InsufficientFundsError] a holder would go below zero
    # @raise [CapabilityDeniedError] the cap lacks :ledger_post
    def post(from, to, amount, currency, reference)
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_ledger_post_cap(capability.ptr, Call.uint32!(from, "from"), Call.uint32!(to, "to"),
                                                Call.int!(amount, "amount"), Call.text!(currency, "currency"),
                                                Call.text!(reference, "reference"), out), ctx)
      out.read_uint64
    end

    # @return [Balance]
    def balance(account)
      d = Call.out(:int64)
      c = Call.out(:int64)
      e = Call.out(:int64)
      Call.check(Native.lib.zxv_ledger_balance_cap(capability.ptr, Call.uint32!(account, "account"), d, c, e), ctx)
      Balance.new(debit: d.read_int64, credit: c.read_int64, equity: e.read_int64)
    end

    # Verify the ledger invariants. On a broken invariant the context is
    # poisoned and ZxvFatal is raised.
    # @return [true]
    def check
      Call.check(Native.lib.zxv_ledger_check_cap(capability.ptr), ctx)
      true
    end

    # Name, currency and kind of an account. Needs :ledger_read.
    # @return [Account]
    def account(id)
      i = Call.uint32!(id, "account")
      rc, ccy = Call.text(capability.ptr, i) { |*a| Native.lib.zxv_ledger_account_ccy_cap(*a) }
      Call.check(rc, ctx)
      rc, name = Call.text(capability.ptr, i) { |*a| Native.lib.zxv_ledger_account_name_cap(*a) }
      Call.check(rc, ctx)
      k = Call.out(:int32)
      Call.check(Native.lib.zxv_ledger_account_kind_cap(capability.ptr, i, k), ctx)
      Account.new(id: i, name: name, currency: ccy, kind: KINDS.key(k.read_int32))
    end

    # @return [Integer] number of accounts (needs :ledger_read)
    def account_count
      n = Call.out(:uint32)
      Call.check(Native.lib.zxv_ledger_account_count_cap(capability.ptr, n), ctx)
      n.read_uint32
    end

    # The journal of one account, oldest first. Needs :ledger_read.
    # @return [Array<Entry>]
    def journal(account)
      i = Call.uint32!(account, "account")
      n = Call.out(:uint32)
      Call.check(Native.lib.zxv_ledger_entry_count_cap(capability.ptr, i, n), ctx)
      Array.new(n.read_uint32) do |k|
        id = Call.out(:uint64)
        amt = Call.out(:int64)
        cp = Call.out(:uint32)
        rc, ref = Call.text(capability.ptr, i, k) do |c, a, idx, buf, cap, len|
          Native.lib.zxv_ledger_entry_at_cap(c, a, idx, id, amt, cp, buf, cap, len)
        end
        Call.check(rc, ctx)
        Entry.new(entry_id: id.read_uint64, amount: amt.read_int64, counterparty: cp.read_uint32, reference: ref)
      end
    end

    private

    def ctx = capability.context
  end
end
