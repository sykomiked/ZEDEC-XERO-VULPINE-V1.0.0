// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv-nestjs: NestJS integration over the ZXV TypeScript SDK.

export { ZxvModule } from './zxv.module.js';
export type { ZxvModuleAsyncOptions } from './zxv.module.js';
export { ZxvConfigurationError, parseRights, rightNames } from './options.js';
export type { CapabilityRight, RightsSpec, PrivateUnitOption, ZxvModuleOptions, ResolvedZxvOptions } from './options.js';
export { ZXV_CONTEXT, ZXV_LIBRARY, ZXV_OPTIONS, capabilityToken, InjectCapability, InjectZxvContext, InjectZxv } from './tokens.js';
export { ZxvCapabilities } from './capabilities.js';
export { ZxvFatalState } from './fatal-state.js';
export { ZxvHealthIndicator } from './health.js';
export type { ZxvState } from './runtime.js';
export { ZxvExceptionFilter, httpStatusFor, problemFor, problemClassOf } from './exception-filter.js';
export type { ZxvProblem, ZxvProblemClass } from './exception-filter.js';
export {
  ParseRationalPipe, ExactBodyPipe, toRational, toMinorUnits, exactJson, findInexactJsonNumber, toExactJson,
} from './exact.js';
export type { ExactBody, ExactFieldKind, ExactFieldValue, ExactJsonOptions } from './exact.js';
export { ExactJsonInterceptor } from './interceptor.js';
