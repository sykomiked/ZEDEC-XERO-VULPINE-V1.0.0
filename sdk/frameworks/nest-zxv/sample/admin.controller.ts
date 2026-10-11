// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Controller, HttpCode, Post } from '@nestjs/common';
import type { ZxvContext } from '@zedec/zxv';

import { InjectZxvContext } from '../src/index.js';

/** `POST /admin/halt` halts the context. Only mounted with `ZXV_SAMPLE_HALT=1`; never outside a local demo. */
@Controller('admin')
export class AdminController {
  constructor(@InjectZxvContext() private readonly ctx: ZxvContext) {}

  @Post('halt')
  @HttpCode(202)
  halt(): void {
    this.ctx.halt('operator halt from the sample');
  }
}
