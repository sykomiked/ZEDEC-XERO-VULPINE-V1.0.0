// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv-web/node: the same worker and protocol on Node's
// worker_threads, for tests and for servers that want generation off the
// event loop with the exact behaviour of the browser build. Not part of
// the browser module graph.

import { Worker } from 'node:worker_threads';

import type { WorkerLike } from './protocol.js';

type Listener = (event: { data?: unknown; message?: string }) => void;
type Fn = (...a: unknown[]) => void;

/** Present a worker_threads Worker as a {@link WorkerLike}. */
export function adaptNodeWorker(worker: Worker): WorkerLike {
  const wrapped = new Map<Listener, Map<string, Fn>>();
  const errorText = (e: unknown): string => (e instanceof Error ? e.message : String(e));
  return {
    postMessage(message, transfer) {
      worker.postMessage(message, transfer as unknown as readonly ArrayBuffer[]);
    },
    addEventListener(type, listener) {
      const byType = wrapped.get(listener) ?? new Map<string, Fn>();
      wrapped.set(listener, byType);
      const on = (name: string, f: Fn): void => {
        byType.set(name, f);
        worker.on(name, f);
      };
      if (type === 'message') on('message', (data) => listener({ data }));
      else if (type === 'messageerror') on('messageerror', (e) => listener({ message: errorText(e) }));
      else {
        on('error', (e) => listener({ message: errorText(e) }));
        on('exit', (code) => listener({ message: `exited with code ${String(code)}` }));
      }
    },
    removeEventListener(type, listener) {
      const byType = wrapped.get(listener);
      if (!byType) return;
      for (const n of type === 'error' ? ['error', 'exit'] : [type]) {
        const f = byType.get(n);
        if (f) worker.off(n, f);
        byType.delete(n);
      }
    },
    terminate() {
      void worker.terminate();
    },
  };
}

/** Start the zxv worker (wasm backend) on a worker thread. */
export function startNodeWorker(): WorkerLike {
  return adaptNodeWorker(new Worker(new URL('./node-worker.js', import.meta.url)));
}
