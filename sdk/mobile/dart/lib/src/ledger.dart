// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'context.dart';
import 'native.dart' as n;

/// Account kind (`ZXV_ACCT_*`).
enum AccountKind {
  /// May never go below zero.
  holder,

  /// May carry a credit balance (issuance, nostro).
  issuer,
}

/// One ledger account (uint32 id).
final class Account {
  const Account(this.id, this.name, this.currency, this.kind);
  final int id;
  final String name;
  final String currency;
  final AccountKind kind;

  @override
  bool operator ==(Object other) =>
      other is Account && other.id == id && other.name == name && other.currency == currency && other.kind == kind;

  @override
  int get hashCode => Object.hash(id, name, currency, kind);

  @override
  String toString() => 'Account($id, $name, $currency, ${kind.name})';
}

/// Rail balances in minor units: debit (received), credit (sent), equity = debit - credit.
final class Balance {
  const Balance(this.debitMinor, this.creditMinor, this.equityMinor);
  final int debitMinor, creditMinor, equityMinor;

  @override
  bool operator ==(Object other) =>
      other is Balance &&
      other.debitMinor == debitMinor &&
      other.creditMinor == creditMinor &&
      other.equityMinor == equityMinor;

  @override
  int get hashCode => Object.hash(debitMinor, creditMinor, equityMinor);

  @override
  String toString() => 'Balance(debit $debitMinor, credit $creditMinor, equity $equityMinor)';
}

/// One journal line: entry id, signed amount (+ received, - sent), counterparty, reference.
final class JournalEntry {
  const JournalEntry(this.entryId, this.signedMinor, this.counterparty, this.reference);
  final int entryId, signedMinor, counterparty;
  final String reference;
}

/// The context's triple ledger through a capability: every call passes
/// [capability] and needs `LEDGER_POST` (open, post) or `LEDGER_READ`
/// (balances, accounts, journals). Money is exact `int` minor units.
final class Ledger {
  Ledger(this.capability);

  /// The capability every call passes.
  final Capability capability;

  /// Largest amount in one posting and largest |balance| (2^53 - 1).
  static const int amountMax = 9007199254740991;

  n.ContextState get _st => capability.context.state;

  /// Opens an account in an enabled currency. Needs `LEDGER_POST`.
  Account openAccount(String name, String currency, [AccountKind kind = AccountKind.holder]) => using((a) {
    final o = a<Uint32>();
    n.check(
      zxvLib.ledgerOpenAccountCap(
        capability.handle.ptr,
        n.cstr(a, name, 'name'),
        n.cstr(a, currency, 'currency'),
        kind.index,
        o,
      ),
      _st,
    );
    return Account(o.value, name, currency, kind);
  });

  /// The account with this id. Needs `LEDGER_READ`.
  Account account(int id) {
    n.u32(id, 'account');
    final c = capability.handle.ptr;
    final name = n.text((b, cap, l) => zxvLib.ledgerAccountNameCap(c, id, b, cap, l), _st);
    final ccy = n.text((b, cap, l) => zxvLib.ledgerAccountCcyCap(c, id, b, cap, l), _st);
    final kind = using((a) {
      final k = a<Int32>();
      n.check(zxvLib.ledgerAccountKindCap(c, id, k), _st);
      return k.value;
    });
    return Account(id, name, ccy, kind == 1 ? AccountKind.issuer : AccountKind.holder);
  }

  /// Number of accounts. Needs `LEDGER_READ`.
  int accountCount() => using((a) {
    final o = a<Uint32>();
    n.check(zxvLib.ledgerAccountCountCap(capability.handle.ptr, o), _st);
    return o.value;
  });

  /// Posts a balanced transfer atomically and returns the entry id. Needs
  /// `LEDGER_POST`. `reference` is unique in the context (<= 35 bytes).
  /// Throws [InsufficientFundsException] if a holder would go below zero.
  int post(Account from, Account to, int amountMinor, String currency, String reference) =>
      postById(from.id, to.id, amountMinor, currency, reference);

  int postById(int fromId, int toId, int amountMinor, String currency, String reference) {
    n.u32(fromId, 'from');
    n.u32(toId, 'to');
    return using((a) {
      final o = a<Uint64>();
      n.check(
        zxvLib.ledgerPostCap(
          capability.handle.ptr,
          fromId,
          toId,
          amountMinor,
          n.cstr(a, currency, 'currency'),
          n.cstr(a, reference, 'reference'),
          o,
        ),
        _st,
      );
      return o.value;
    });
  }

  /// Balances. Needs `LEDGER_READ`.
  Balance balance(Account account) => balanceById(account.id);

  Balance balanceById(int id) {
    n.u32(id, 'account');
    return using((a) {
      final o = a<Int64>(3);
      n.check(zxvLib.ledgerBalanceCap(capability.handle.ptr, id, o, o + 1, o + 2), _st);
      return Balance(o[0], o[1], o[2]);
    });
  }

  /// The journal of an account, oldest first. Needs `LEDGER_READ`.
  List<JournalEntry> journal(Account account) => using((a) {
    final c = capability.handle.ptr;
    final cnt = a<Uint32>();
    n.check(zxvLib.ledgerEntryCountCap(c, account.id, cnt), _st);
    final id = a<Uint64>();
    final amt = a<Int64>();
    final cp = a<Uint32>();
    return [
      for (var i = 0; i < cnt.value; i++)
        () {
          final ref = n.text((b, cap, l) => zxvLib.ledgerEntryAtCap(c, account.id, i, id, amt, cp, b, cap, l), _st);
          return JournalEntry(id.value, amt.value, cp.value, ref);
        }(),
    ];
  });

  /// Verifies the ledger's invariants; a broken one poisons the context and
  /// throws [ZxvFatalError]. Needs `LEDGER_READ`.
  void check() => n.check(zxvLib.ledgerCheckCap(capability.handle.ptr), _st);
}
