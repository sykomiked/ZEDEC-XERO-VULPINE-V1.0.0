// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The SHA3-256-keyed offline cache with an in-memory CacheStorage and a
// fake fetch (no network): hits are verified, corrupt copies are dropped
// and fetched again, bad downloads are refused and never stored.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  AssetCache, type CacheLike, type CacheStorageLike, DigestMismatchError, type FetchLike, type ResponseLike, sha3_256Hex, VerifiedBytes,
  verifyBytes,
} from '../src/index.js';
import { wasm } from './helpers.js';

class MemoryCache implements CacheLike {
  readonly store = new Map<string, Uint8Array>();
  async match(key: string): Promise<ResponseLike | undefined> {
    const b = this.store.get(key);
    return b ? new Response(b.slice()) : undefined;
  }
  async put(key: string, response: Response): Promise<void> {
    this.store.set(key, new Uint8Array(await response.arrayBuffer()));
  }
  async delete(key: string): Promise<boolean> {
    return this.store.delete(key);
  }
}

class MemoryCaches implements CacheStorageLike {
  readonly caches = new Map<string, MemoryCache>();
  async open(name: string): Promise<MemoryCache> {
    let c = this.caches.get(name);
    if (!c) this.caches.set(name, (c = new MemoryCache()));
    return c;
  }
}

function fakeFetch(files: Record<string, Uint8Array>): FetchLike & { calls: string[]; offline: boolean } {
  const f = Object.assign(
    async (url: string, init?: { signal?: AbortSignal }): Promise<ResponseLike> => {
      init?.signal?.throwIfAborted();
      f.calls.push(url);
      if (f.offline) throw new TypeError('fetch failed (offline)');
      const b = files[url];
      return b ? new Response(b.slice()) : new Response(null, { status: 404 });
    },
    { calls: [] as string[], offline: false },
  );
  return f;
}

const model = new TextEncoder().encode('GGUF pretend model bytes');
const modelDigest = sha3_256Hex(model);
const spec = { url: 'https://example.invalid/models/tiny.gguf', sha3_256: modelDigest };

test('miss: fetch, verify, store; hit: verified copy, no fetch (offline)', async () => {
  const caches = new MemoryCaches();
  const fetch = fakeFetch({ [spec.url]: model });
  const cache = new AssetCache({ caches, fetch });
  const a = await cache.get(spec);
  assert.equal(a.source, 'network');
  assert.deepEqual(a.bytes, model);
  assert.equal(a.sha3_256, modelDigest);
  assert.deepEqual(fetch.calls, [spec.url]);
  assert.ok(await cache.has(modelDigest));
  assert.ok((await caches.open('zxv-assets-v1')).store.has(AssetCache.keyFor(modelDigest)));

  fetch.offline = true;
  const b = await cache.get(spec);
  assert.equal(b.source, 'cache');
  assert.deepEqual(b.bytes, model);
  assert.equal(fetch.calls.length, 1);
  // the key is the digest, not the URL: another URL with the same digest is a hit
  assert.equal((await cache.get({ url: 'https://mirror.invalid/x', sha3_256: modelDigest.toUpperCase() })).source, 'cache');
  assert.equal((await cache.get(spec, { offlineOnly: true })).source, 'cache');
});

test('a corrupt cached copy is dropped and fetched again', async () => {
  const caches = new MemoryCaches();
  const fetch = fakeFetch({ [spec.url]: model });
  const cache = new AssetCache({ caches, fetch });
  await cache.get(spec);
  const store = (await caches.open('zxv-assets-v1')).store;
  const tampered = model.slice();
  tampered[0] = tampered[0]! ^ 1;
  store.set(AssetCache.keyFor(modelDigest), tampered);
  const again = await cache.get(spec);
  assert.equal(again.source, 'network');
  assert.deepEqual(again.bytes, model);
  assert.equal(fetch.calls.length, 2);
  assert.deepEqual(store.get(AssetCache.keyFor(modelDigest)), model);

  // corrupt and offline: refused, never handed out
  store.set(AssetCache.keyFor(modelDigest), tampered);
  fetch.offline = true;
  await assert.rejects(cache.get(spec), /offline/);
  assert.equal(await cache.has(modelDigest), false);
});

test('a download with the wrong digest is refused and not stored', async () => {
  const caches = new MemoryCaches();
  const evil = new TextEncoder().encode('not the model');
  const cache = new AssetCache({ caches, fetch: fakeFetch({ [spec.url]: evil }) });
  await assert.rejects(cache.get(spec), (e: unknown) => {
    assert.ok(e instanceof DigestMismatchError);
    assert.equal(e.expected, modelDigest);
    assert.equal(e.actual, sha3_256Hex(evil));
    return true;
  });
  assert.equal(await cache.has(modelDigest), false);
});

test('misses, HTTP errors, aborts and bad digests', async () => {
  const fetch = fakeFetch({});
  const cache = new AssetCache({ caches: new MemoryCaches(), fetch });
  await assert.rejects(cache.get(spec, { offlineOnly: true }), /not in the offline cache/);
  assert.equal(fetch.calls.length, 0);
  await assert.rejects(cache.get(spec), /HTTP 404/);
  const ac = new AbortController();
  ac.abort();
  await assert.rejects(cache.get(spec, { signal: ac.signal }), { name: 'AbortError' });
  await assert.rejects(cache.get({ url: spec.url, sha3_256: 'abc' }), TypeError);
  assert.equal(await cache.delete(modelDigest), false);
});

test('without the Cache API it still verifies, and keeps nothing', async () => {
  const fetch = fakeFetch({ [spec.url]: model });
  const cache = new AssetCache({ caches: null, fetch });
  assert.equal((await cache.get(spec)).source, 'network');
  assert.equal((await cache.get(spec)).source, 'network');
  assert.equal(fetch.calls.length, 2);
  assert.equal(await cache.has(modelDigest), false);
});

test('the real wasm module through the cache', async () => {
  const url = 'https://example.invalid/zxv.wasm';
  const cache = new AssetCache({ caches: new MemoryCaches(), fetch: fakeFetch({ [url]: wasm.bytes }) });
  const v = await cache.get({ url, sha3_256: wasm.sha3_256 });
  assert.equal(v.bytes.length, wasm.bytes.length);
  assert.ok(WebAssembly.validate(v.bytes));
});

test('VerifiedBytes are made only by checking', () => {
  const v = verifyBytes(model, modelDigest.toUpperCase(), 'model');
  assert.ok(v instanceof VerifiedBytes);
  assert.equal(v.source, 'memory');
  assert.equal(v.sha3_256, modelDigest);
  assert.ok(Object.isFrozen(v));
  assert.throws(() => verifyBytes(model, '0'.repeat(64)), DigestMismatchError);
  assert.throws(() => new VerifiedBytes(model, modelDigest, 'memory', Symbol('fake')), TypeError);
});
