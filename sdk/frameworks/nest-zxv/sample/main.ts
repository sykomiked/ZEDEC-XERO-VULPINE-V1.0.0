// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The sample: npm run sample, then
//   curl -s localhost:3080/quote -H 'content-type: application/json' -d '{"price":"12.75","quantity":"1/3"}'

import 'reflect-metadata';

import { NestFactory } from '@nestjs/core';

import { exactJson } from '../src/index.js';
import { AppModule } from './app.module.js';

const app = await NestFactory.create(AppModule.register({ haltEndpoint: process.env['ZXV_SAMPLE_HALT'] === '1' }), { bodyParser: false });
app.use(exactJson());
app.enableShutdownHooks();
const port = Number.parseInt(process.env['PORT'] ?? '3080', 10);
await app.listen(port);
console.log(`ZXV Nest sample on http://localhost:${port}`);
