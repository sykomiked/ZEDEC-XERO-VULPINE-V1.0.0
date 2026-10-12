// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv-react: offline-first React hooks for ZXV. No runtime
// dependency besides React: the backend (z-web, the @zedec/zxv addon,
// zxvd) is injected.

export {
  ZxvProvider, useZxv, useGenerate, useBudget, useRational, useLedgerBalance,
} from './hooks.js';
export type {
  ZxvProviderProps, UseZxv, UseGenerate, GenerateOptions, GenerateStatus, UseBudget, UseRational, UseLedgerBalance, BalanceStatus,
} from './hooks.js';
export { ZxvClient } from './client.js';
export type {
  QueueStorage, ClientOptions, CallOptions, ClientState, Transfer, Posted, Balance, BudgetState, ReplayEvent,
} from './client.js';
export {
  BackendUnavailableError, ZxvCallError, NotSupportedError, OutcomeUnknownError, IdempotencyConflictError, isIdempotencyKey, Reachability,
  E_DUPLICATE,
} from './backend.js';
export type {
  ZxvBackend, Feature, GenerateRequest, GenerateChunk, GenerateOutcome, BudgetSnapshot, BalanceSnapshot, PostRequest, PostOutcome,
  StatusClassName,
} from './backend.js';
export { ExactRational, FloatRejectedError, RationalError, intToWire, intFromWire, I64_MAX } from './rational.js';
export type { RationalInput } from './rational.js';
export { createZWebBackend } from './backends/zweb.js';
export type { ZWebClientLike, ZWebBackendOptions } from './backends/zweb.js';
export { createSdkBackend } from './backends/sdk.js';
export type { SdkBackendOptions, SdkLedgerLike, SdkSessionLike, SdkBudgetLike } from './backends/sdk.js';
export { createZxvdBackend, createLineTransport, lineSplitter } from './backends/zxvd.js';
export type { ZxvdBackendOptions, JsonRpcTransport, LineConnection, LineTransportOptions } from './backends/zxvd.js';
