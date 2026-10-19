// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { type DynamicModule, Module } from '@nestjs/common';
import { APP_FILTER, APP_INTERCEPTOR } from '@nestjs/core';
import { TerminusModule } from '@nestjs/terminus';

import { ExactJsonInterceptor, ZxvExceptionFilter, ZxvModule } from '../src/index.js';
import { AdminController } from './admin.controller.js';
import { AppController, HealthController } from './app.controller.js';
import { TreasuryService } from './treasury.service.js';

/** Options of the sample. */
export interface SampleOptions {
  /** Mount `POST /admin/halt` (local demos and tests only). */
  readonly haltEndpoint?: boolean;
}

@Module({})
export class AppModule {
  static register(options: SampleOptions = {}): DynamicModule {
    return {
      module: AppModule,
      imports: [
        ZxvModule.forRoot({
          currencies: ['NGN'],
          rootRights: ['LEDGER_READ', 'LEDGER_POST'],
          capabilities: {
            teller: ['LEDGER_READ', 'LEDGER_POST'],
            auditor: 'LEDGER_READ',
          },
        }),
        TerminusModule,
      ],
      controllers: [AppController, HealthController, ...(options.haltEndpoint === true ? [AdminController] : [])],
      providers: [
        TreasuryService,
        { provide: APP_FILTER, useExisting: ZxvExceptionFilter },
        { provide: APP_INTERCEPTOR, useExisting: ExactJsonInterceptor },
      ],
    };
  }
}
