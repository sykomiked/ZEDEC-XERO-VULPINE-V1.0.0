// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A jsdom document for React DOM under node:test. Import this first in a
// test file (before React): it defines the browser globals Node lacks and
// leaves Node's own (ArrayBuffer, TextEncoder, AbortController, ...) alone.

import { JSDOM, VirtualConsole } from 'jsdom';

const dom = new JSDOM('<!doctype html><html><body></body></html>', {
  url: 'http://localhost/', pretendToBeVisual: true, virtualConsole: new VirtualConsole(), // uncaught render errors are asserted, not printed
});
const g = globalThis as Record<string, unknown>;
for (const key of Object.getOwnPropertyNames(dom.window)) {
  if (key in g || key.startsWith('_')) continue;
  try {
    Object.defineProperty(g, key, { configurable: true, get: () => (dom.window as unknown as Record<string, unknown>)[key] });
  } catch {
    // a non-configurable global: keep Node's
  }
}
g['IS_REACT_ACT_ENVIRONMENT'] = true;
