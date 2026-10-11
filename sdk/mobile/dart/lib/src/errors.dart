// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

/// The class of a libzxv status (`zxv_status_class`); unknown negative codes are [system].
enum StatusClass {
  ok,
  usage,
  refused,
  fatal,
  system;

  static StatusClass fromCode(int code) => code >= 0 && code < values.length ? values[code] : system;
}

/// Base of every recoverable error for a negative libzxv status. Branch on
/// the subtype or on [status], never on [detail].
///
/// The terminal case is not here: it is [ZxvFatalError], an [Error].
class ZxvException implements Exception {
  ZxvException(this.status, this.statusName, this.detail);

  /// The negative status code, e.g. -6.
  final int status;

  /// The symbolic name, e.g. `ZXV_E_FUNDS`.
  final String statusName;

  /// Diagnostic text from `zxv_last_error` (may be empty). For people only.
  final String detail;

  @override
  String toString() => detail.isEmpty ? '$runtimeType($statusName)' : '$runtimeType($statusName: $detail)';
}

/// A caller bug (USAGE class): fix the call, never retry it.
class ZxvUsageException extends ZxvException {
  ZxvUsageException(super.status, super.statusName, super.detail);
}

/// NaN or infinity given to `Rational.fromDoubleExact` (`ZXV_E_FLOAT`).
class FloatRejectedException extends ZxvUsageException {
  FloatRejectedException(String detail) : super(-24, 'ZXV_E_FLOAT', detail);
}

/// A well-formed request the platform declined (REFUSED class); it may succeed later.
class ZxvRefusedException extends ZxvException {
  ZxvRefusedException(super.status, super.statusName, super.detail);
}

/// The capability lacks the permission, is revoked or unattested, or belongs to another context.
class CapabilityDeniedException extends ZxvRefusedException {
  CapabilityDeniedException(super.status, super.statusName, super.detail);
}

/// An enforced budget would be passed; nothing was consumed.
class BudgetExhaustedException extends ZxvRefusedException {
  BudgetExhaustedException(super.status, super.statusName, super.detail);
}

/// A holder account would go below zero.
class InsufficientFundsException extends ZxvRefusedException {
  InsufficientFundsException(super.status, super.statusName, super.detail);
}

/// A signature did not verify.
class VerificationFailedException extends ZxvRefusedException {
  VerificationFailedException(super.status, super.statusName, super.detail);
}

/// The feature is not built into this libzxv; see `Zxv.features`.
class NotImplementedException extends ZxvRefusedException {
  NotImplementedException(super.status, super.statusName, super.detail);
}

/// Memory, I/O, an internal or unknown status (SYSTEM class).
class ZxvSystemException extends ZxvException {
  ZxvSystemException(super.status, super.statusName, super.detail);
}

/// The context halted (FATAL class). Terminal: never retry.
///
/// It is an [Error], not an [Exception]: `on Exception catch` and
/// `on ZxvException catch` do not catch it. (A bare `catch (e)` catches
/// everything in Dart; do not use one around ZXV calls.) The context is
/// poisoned in C too, so even a caught error cannot be retried. For an exit
/// no code can intercept, create the context with `FatalPolicy.abort`.
class ZxvFatalError extends Error {
  ZxvFatalError(this.status, this.statusName, this.detail);

  final int status;
  final String statusName;
  final String detail;

  @override
  String toString() => 'ZxvFatalError($statusName: $detail)';
}
