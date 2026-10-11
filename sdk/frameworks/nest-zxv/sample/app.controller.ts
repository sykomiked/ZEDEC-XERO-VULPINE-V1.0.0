// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Body, Controller, Get, HttpCode, Post, Query } from '@nestjs/common';
import { HealthCheck, HealthCheckService, type HealthCheckResult } from '@nestjs/terminus';
import type { Balance, Rational } from '@zedec/zxv';

import { type ExactBody, ExactBodyPipe, ParseRationalPipe, ZxvHealthIndicator } from '../src/index.js';
import { TreasuryService } from './treasury.service.js';

const Quote = new ExactBodyPipe({ price: 'rational', quantity: 'rational' });
const Payment = new ExactBodyPipe({ amountMinor: 'minorUnits', reference: 'string' });

/** Exact quotes and a small treasury. */
@Controller()
export class AppController {
  constructor(private readonly treasury: TreasuryService) {}

  /** `{"price":"12.75","quantity":"1/3"}` -> `{"total":"4.25"}`; a JSON float is a 400. */
  @Post('quote')
  @HttpCode(200)
  quote(@Body(Quote) q: ExactBody<typeof Quote.schema>): { total: Rational } {
    return { total: q.price.mul(q.quantity) };
  }

  /** `?a=0.1&b=0.2` -> `{"sum":"0.3"}`. */
  @Get('sum')
  sum(@Query('a', ParseRationalPipe) a: Rational, @Query('b', ParseRationalPipe) b: Rational): { sum: Rational } {
    return { sum: a.add(b) };
  }

  /** Posted with `teller`. */
  @Post('payments')
  pay(@Body(Payment) p: ExactBody<typeof Payment.schema>): { entryId: bigint } {
    return { entryId: this.treasury.payCustomer(p.amountMinor, p.reference) };
  }

  /** Attempted with `auditor`: 403 problem. */
  @Post('refunds')
  refund(@Body(Payment) p: ExactBody<typeof Payment.schema>): { entryId: bigint } {
    return { entryId: this.treasury.refundAsAuditor(p.amountMinor, p.reference) };
  }

  /** The customer's balance (bigints become exact strings). */
  @Get('balance')
  balance(): Balance {
    return this.treasury.customerBalance();
  }
}

/** Readiness: DOWN (503) when the context is halted or closed. */
@Controller('health')
export class HealthController {
  constructor(private readonly health: HealthCheckService, private readonly zxv: ZxvHealthIndicator) {}

  @Get('ready')
  @HealthCheck()
  ready(): Promise<HealthCheckResult> {
    return this.health.check([() => this.zxv.isHealthy()]);
  }

  /** Liveness does not ask ZXV: a halted context needs a restart, which readiness and `onFatal` drive. */
  @Get('live')
  live(): { status: 'ok' } {
    return { status: 'ok' };
  }
}
