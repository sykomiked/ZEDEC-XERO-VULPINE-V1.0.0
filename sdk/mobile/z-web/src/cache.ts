// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// An offline cache for zxv.wasm and model files, keyed by SHA3-256. A file
// is named by its digest, not its URL: the URL only says where to fetch it
// the first time. Every read, from the cache or the network, is hashed and
// compared before the bytes are handed out; a cached copy that does not
// match is deleted and fetched again, and a download that does not match
// is refused and never stored.
//
// Uses the Cache API (`caches`, in pages and workers of secure contexts).
// Without it (an insecure context, Node) it still fetches and verifies,
// it just keeps nothing. Both `caches` and `fetch` can be injected.

import { DigestMismatchError } from './errors.js';
import { isDigestHex, sha3_256Hex } from './sha3.js';

/** A file to load: where to get it, and the SHA3-256 it must have (64 hex digits). */
export interface AssetSpec {
  readonly url: string | URL;
  readonly sha3_256: string;
}

/** The parts of a Response the cache reads. */
export interface ResponseLike {
  readonly ok?: boolean;
  readonly status?: number;
  arrayBuffer(): Promise<ArrayBuffer>;
}

/** The parts of a Cache the helper uses. */
export interface CacheLike {
  match(key: string): Promise<ResponseLike | undefined>;
  put(key: string, response: Response): Promise<void>;
  delete(key: string): Promise<boolean>;
}

/** The parts of CacheStorage the helper uses. */
export interface CacheStorageLike {
  open(name: string): Promise<CacheLike>;
}

/** A fetch function. */
export type FetchLike = (url: string, init?: { signal?: AbortSignal }) => Promise<ResponseLike>;

const TOKEN = Symbol('VerifiedBytes');

/**
 * Bytes whose SHA3-256 was checked against an expected digest. Only
 * {@link verifyBytes} and {@link AssetCache.get} make them, so code that
 * receives one need not hash again.
 */
export class VerifiedBytes {
  readonly bytes: Uint8Array;
  /** The digest, lower-case hex. */
  readonly sha3_256: string;
  /** Where the bytes came from. */
  readonly source: 'cache' | 'network' | 'memory';

  /** @internal */
  constructor(bytes: Uint8Array, digest: string, source: 'cache' | 'network' | 'memory', token: symbol) {
    if (token !== TOKEN) throw new TypeError('use verifyBytes() or AssetCache.get()');
    this.bytes = bytes;
    this.sha3_256 = digest;
    this.source = source;
    Object.freeze(this);
  }
}

function normalise(digest: string, what: string): string {
  if (!isDigestHex(digest)) throw new TypeError(`${what}: sha3_256 must be 64 hex digits`);
  return digest.toLowerCase();
}

/** Hash `bytes` and return them as {@link VerifiedBytes}, or throw {@link DigestMismatchError}. */
export function verifyBytes(bytes: Uint8Array, sha3_256: string, what = 'bytes'): VerifiedBytes {
  return check(bytes, normalise(sha3_256, what), what, 'memory');
}

function check(bytes: Uint8Array, want: string, what: string, source: 'cache' | 'network' | 'memory'): VerifiedBytes {
  const got = sha3_256Hex(bytes);
  if (got !== want) throw new DigestMismatchError(what, want, got);
  return new VerifiedBytes(bytes, want, source, TOKEN);
}

/** Options of {@link AssetCache}. */
export interface AssetCacheOptions {
  /** Cache name (default "zxv-assets-v1"). */
  name?: string;
  /** CacheStorage to use (default `globalThis.caches`; `null` keeps nothing). */
  caches?: CacheStorageLike | null;
  /** fetch to use (default `globalThis.fetch`). */
  fetch?: FetchLike;
}

/** Options of {@link AssetCache.get}. */
export interface GetOptions {
  signal?: AbortSignal;
  /** Never touch the network: a miss throws. */
  offlineOnly?: boolean;
}

/** The SHA3-256-keyed offline cache. */
export class AssetCache {
  readonly name: string;
  readonly #caches: CacheStorageLike | null;
  readonly #fetch: FetchLike | null;

  constructor(options: AssetCacheOptions = {}) {
    this.name = options.name ?? 'zxv-assets-v1';
    const g = globalThis as { caches?: CacheStorageLike; fetch?: FetchLike };
    this.#caches = options.caches === undefined ? g.caches ?? null : options.caches;
    this.#fetch = options.fetch ?? (g.fetch ? g.fetch.bind(globalThis) : null);
  }

  /** The cache key of a digest (a URL under the reserved .invalid domain; never fetched). */
  static keyFor(sha3_256: string): string {
    return `https://zxv-cache.invalid/sha3-256/${normalise(sha3_256, 'key')}`;
  }

  /** True when a copy is stored (not verified; {@link get} verifies). */
  async has(sha3_256: string): Promise<boolean> {
    const c = await this.#open();
    return c ? (await c.match(AssetCache.keyFor(sha3_256))) !== undefined : false;
  }

  /** Remove a stored copy. */
  async delete(sha3_256: string): Promise<boolean> {
    const c = await this.#open();
    return c ? c.delete(AssetCache.keyFor(sha3_256)) : false;
  }

  /**
   * The file's bytes, checked. A verified cached copy is used offline; a
   * missing or corrupt one is fetched from `spec.url`, checked, and stored.
   */
  async get(spec: AssetSpec, options: GetOptions = {}): Promise<VerifiedBytes> {
    const want = normalise(spec.sha3_256, String(spec.url));
    const what = String(spec.url);
    options.signal?.throwIfAborted();
    const cache = await this.#open();
    const key = AssetCache.keyFor(want);
    if (cache) {
      const hit = await cache.match(key);
      if (hit) {
        const bytes = new Uint8Array(await hit.arrayBuffer());
        try {
          return check(bytes, want, `${what} (cached)`, 'cache');
        } catch (e) {
          if (!(e instanceof DigestMismatchError)) throw e;
          await cache.delete(key); // corrupt or tampered: drop it, fetch again
        }
      }
    }
    if (options.offlineOnly) throw new Error(`${what}: not in the offline cache (${want})`);
    if (!this.#fetch) throw new Error(`${what}: no fetch available`);
    options.signal?.throwIfAborted();
    const res = await this.#fetch(what, options.signal ? { signal: options.signal } : {});
    if (res.ok === false) throw new Error(`${what}: HTTP ${String(res.status)}`);
    const verified = check(new Uint8Array(await res.arrayBuffer()), want, what, 'network');
    if (cache) {
      const R = (globalThis as { Response?: typeof Response }).Response;
      if (R) await cache.put(key, new R(verified.bytes.slice(), { headers: { 'content-type': 'application/octet-stream' } }));
    }
    return verified;
  }

  async #open(): Promise<CacheLike | null> {
    return this.#caches ? this.#caches.open(this.name) : null;
  }
}
