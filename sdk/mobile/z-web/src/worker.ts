// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The Web Worker entry: `new Worker(new URL('@zedec/zxv-web/worker', ...),
// { type: 'module' })`, or let createZxvWeb() start it. It serves
// zxv-web/1 with the WebAssembly build of @zedec/zxv; the module bytes
// come from the page (already checked against their SHA3-256), so the
// worker itself fetches nothing.

import * as sdk from '@zedec/zxv/wasm';

import type { Endpoint } from './protocol.js';
import { serve } from './worker-core.js';

serve(globalThis as unknown as Endpoint, sdk);
