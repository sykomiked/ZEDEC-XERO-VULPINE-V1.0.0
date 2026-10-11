// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { type ArgumentsHost, Catch, type ExceptionFilter, Injectable, Logger, Optional } from '@nestjs/common';
import { HttpAdapterHost } from '@nestjs/core';
import {
  Status, ZxvError, ZxvFatalError, ZxvRefusedError, ZxvUsageError, type ZxvStatusInfo,
} from '@zedec/zxv';

import { ZxvFatalState } from './fatal-state.js';

/** The status class of a ZXV error, as it appears in a problem body. */
export type ZxvProblemClass = 'usage' | 'refused' | 'fatal' | 'system';

/** An RFC 9457 problem body. It never carries the native detail text. */
export interface ZxvProblem {
  readonly type: string;
  readonly title: string;
  readonly status: number;
  /** The ZXV status name, e.g. `ZXV_E_CAP`. */
  readonly code: string;
  readonly class: ZxvProblemClass;
}

const TITLES: Readonly<Record<ZxvProblemClass, string>> = Object.freeze({
  usage: 'Invalid request',
  refused: 'Operation refused',
  fatal: 'Service halted',
  system: 'Internal error',
});

/** The class of a ZXV error, from its type (which the SDK chose by `zxv_status_class`). */
export function problemClassOf(e: ZxvError | ZxvFatalError): ZxvProblemClass {
  if (e instanceof ZxvFatalError) return 'fatal';
  if (e instanceof ZxvUsageError) return 'usage';
  if (e instanceof ZxvRefusedError) return 'refused';
  return 'system';
}

/**
 * The one mapping from ZXV status to HTTP status. Specific codes first,
 * then the class: USAGE 400, REFUSED 409, SYSTEM 500, FATAL 503.
 */
export function httpStatusFor(e: ZxvError | ZxvFatalError): number {
  switch (e.status) {
    case Status.E_NOT_FOUND: return 404;
    case Status.E_DUPLICATE: return 409;
    case Status.E_CAP: return 403;
    case Status.E_FUNDS:
    case Status.E_CURRENCY:
    case Status.E_DECLINED:
    case Status.E_VERIFY: return 422;
    case Status.E_BUDGET: return 429;
    case Status.E_NOTIMPL: return 501;
    case Status.E_CAPACITY:
    case Status.E_NOMEM: return 503;
    default: break;
  }
  switch (problemClassOf(e)) {
    case 'usage': return 400;
    case 'refused': return 409;
    case 'fatal': return 503;
    case 'system': return 500;
  }
}

/** The problem body for a ZXV error: type, generic title, status, code, class. Nothing else. */
export function problemFor(e: ZxvError | ZxvFatalError): ZxvProblem {
  const cls = problemClassOf(e);
  const code = /^ZXV_[A-Z0-9_]+$/.test(e.statusName) ? e.statusName : 'ZXV_E_INTERNAL';
  return Object.freeze({
    type: `urn:zxv:problem:${code.toLowerCase()}`,
    title: TITLES[cls],
    status: httpStatusFor(e),
    code,
    class: cls,
  });
}

/**
 * Answers ZXV errors as `application/problem+json` by status class, without
 * the exception message or the native detail text (which can name accounts,
 * references or currencies). Those go to the server log only.
 *
 * A {@link ZxvFatalError} (the context halted) is answered 503, logged at
 * fatal level, and trips {@link ZxvFatalState}: a halted context is never
 * answered as a normal response. Other exceptions are not caught here.
 *
 * Register it globally: `{ provide: APP_FILTER, useExisting: ZxvExceptionFilter }`
 * in a module that imports `ZxvModule`, or `app.useGlobalFilters(app.get(ZxvExceptionFilter))`.
 */
@Catch(ZxvError, ZxvFatalError)
@Injectable()
export class ZxvExceptionFilter implements ExceptionFilter<ZxvError | ZxvFatalError> {
  readonly #log = new Logger('Zxv');
  readonly #adapter: HttpAdapterHost;
  readonly #fatal: ZxvFatalState | undefined;

  constructor(adapter: HttpAdapterHost, @Optional() fatal?: ZxvFatalState) {
    this.#adapter = adapter;
    this.#fatal = fatal;
  }

  catch(e: ZxvError | ZxvFatalError, host: ArgumentsHost): void {
    if (host.getType() !== 'http') throw e;
    const problem = problemFor(e);
    this.#record(e, problem);
    const { httpAdapter } = this.#adapter;
    const res: unknown = host.switchToHttp().getResponse();
    httpAdapter.setHeader(res, 'content-type', 'application/problem+json');
    httpAdapter.setHeader(res, 'cache-control', 'no-store');
    httpAdapter.reply(res, problem, problem.status);
  }

  #record(e: ZxvStatusInfo & Error, p: ZxvProblem): void {
    const line = `${p.code} (${p.class}) -> ${p.status}: ${e.detail}`;
    switch (p.class) {
      case 'fatal':
        this.#log.fatal(line);
        this.#fatal?.trip(p.code, e instanceof ZxvFatalError ? e : undefined);
        break;
      case 'system':
        this.#log.error(line, e.stack);
        break;
      default:
        this.#log.log(line);
        break;
    }
  }
}
