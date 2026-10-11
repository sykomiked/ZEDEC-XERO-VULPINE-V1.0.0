// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { type CallHandler, type ExecutionContext, Injectable, type NestInterceptor } from '@nestjs/common';
import { map, type Observable } from 'rxjs';

import { toExactJson } from './exact.js';

/**
 * Serializes `bigint` values in handler results as exact decimal strings
 * ({@link toExactJson}), so entry ids and balances never pass through a
 * float. Register with `@UseInterceptors(ExactJsonInterceptor)` or
 * `APP_INTERCEPTOR`.
 */
@Injectable()
export class ExactJsonInterceptor implements NestInterceptor {
  intercept(_context: ExecutionContext, next: CallHandler): Observable<unknown> {
    return next.handle().pipe(map(toExactJson));
  }
}
