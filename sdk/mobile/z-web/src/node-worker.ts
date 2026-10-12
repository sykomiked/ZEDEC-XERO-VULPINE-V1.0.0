// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The worker_threads entry: serves zxv-web/1 with the WebAssembly build of
// @zedec/zxv, exactly like worker.ts in a browser.

import { parentPort } from 'node:worker_threads';

import * as sdk from '@zedec/zxv/wasm';

import { serve } from './worker-core.js';

const port = parentPort;
if (!port) throw new Error('node-worker.js must run on a worker thread');
serve({
  postMessage: (message, transfer) => port.postMessage(message, transfer as unknown as readonly ArrayBuffer[]),
  addEventListener: (_type, listener) => port.on('message', (data: unknown) => listener({ data })),
}, sdk);
