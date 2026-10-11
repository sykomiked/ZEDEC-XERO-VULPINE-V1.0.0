// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability } from './context.js';
import { usage } from './errors.js';
import { type Ptr, utf8 } from './internal/backend.js';
import { bytes, u32 } from './internal/check.js';
import type { Core } from './internal/core.js';

/** Anything that carries the loaded library (a {@link Zxv} or a {@link ZxvContext}). */
export interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Bytes, or text taken as UTF-8. */
export type BytesLike = Uint8Array | string;

function data(name: string, v: BytesLike): Uint8Array {
  return typeof v === 'string' ? utf8(v) : bytes(name, v);
}

interface Held {
  core: Core;
  ptr: Ptr;
  length: number;
}

// Backstop: a SecretKey that was never closed is wiped when collected.
const reaper = new FinalizationRegistry<Held>((h) => h.core.b.free(h.ptr, h.length));

/**
 * Secret key material (ML-KEM decapsulation keys, ML-DSA and Ed25519
 * secret keys, shared secrets) held OUTSIDE the JavaScript heap: in
 * addon-owned native memory, or in the WebAssembly module's linear memory.
 * It is wiped with `zxv_secure_zero` on {@link SecretKey.close} (or at the
 * end of a `using` block), and by a FinalizationRegistry as a backstop.
 *
 * The bytes never reach the JS heap unless you call the explicitly named
 * {@link SecretKey.exportBytesUnsafe}, which defeats the wiping for that
 * copy. See the README's "Honest limits" for what cannot be guaranteed.
 */
export class SecretKey implements Disposable {
  /** Length in bytes. */
  readonly length: number;
  readonly #core: Core;
  #ptr: Ptr | null;
  readonly #held: Held;

  /** @internal */
  constructor(core: Core, length: number) {
    this.#core = core;
    this.length = length;
    this.#ptr = core.b.alloc(length);
    this.#held = { core, ptr: this.#ptr, length };
    reaper.register(this, this.#held, this);
  }

  /**
   * Copy key bytes (e.g. a known-answer seed) into off-heap memory. The
   * source array stays on the JS heap: overwrite it yourself if it matters.
   */
  static fromBytes(owner: LibraryOwner, key: Uint8Array): SecretKey {
    const k = bytes('key', key);
    const s = new SecretKey(owner._core, k.length);
    owner._core.b.write(s._ptr(), k);
    return s;
  }

  /** @internal The backend address (throws if closed). */
  _ptr(): Ptr {
    if (this.#ptr === null) throw usage('SecretKey is closed');
    return this.#ptr;
  }

  /** @internal The library this key lives in. */
  get _core(): Core {
    return this.#core;
  }

  /** True once wiped. */
  get closed(): boolean {
    return this.#ptr === null;
  }

  /**
   * A JS-heap copy of the key. UNSAFE: that copy is not wiped by close()
   * and may be copied by the garbage collector. For interop only.
   */
  exportBytesUnsafe(): Uint8Array {
    return this.#core.b.read(this._ptr(), this.length);
  }

  /** Constant-structure comparison with another secret (both stay off-heap except transiently). */
  equals(other: SecretKey): boolean {
    if (other.length !== this.length) return false;
    const a = this.exportBytesUnsafe();
    const b = other.exportBytesUnsafe();
    let d = 0;
    for (let i = 0; i < a.length; i++) d |= (a[i] ?? 0) ^ (b[i] ?? 0);
    a.fill(0);
    b.fill(0);
    return d === 0;
  }

  /** Wipe (zxv_secure_zero) and free. Idempotent. */
  close(): void {
    if (this.#ptr === null) return;
    reaper.unregister(this);
    this.#core.b.free(this.#ptr, this.length);
    this.#ptr = null;
  }

  /** `using` support. */
  [Symbol.dispose](): void {
    this.close();
  }
}

function needSecret(name: string, k: unknown, len: number, core: Core): SecretKey {
  if (!(k instanceof SecretKey)) throw usage(`${name} must be a SecretKey (use SecretKey.fromBytes for known bytes)`);
  if (k.length !== len) throw usage(`${name} must be ${len} bytes, got ${k.length}`);
  if (k._core !== core) throw usage(`${name} belongs to a different loaded library`);
  return k;
}

/** SHA3-256, SHAKE256 and Ethereum's Keccak-256. Reached as `zxv.hashes`. */
export class Hashes {
  readonly #core: Core;

  /** @internal */
  constructor(core: Core) {
    this.#core = core;
  }

  /** SHA3-256 (FIPS 202). */
  sha3_256(input: BytesLike): Uint8Array {
    const out = new Uint8Array(32);
    const d = data('input', input);
    this.#core.call(this.#core, 'zxv_sha3_256', d, d.length, out);
    return out;
  }

  /** SHAKE256 with `outLen` output bytes. */
  shake256(input: BytesLike, outLen: number): Uint8Array {
    const out = new Uint8Array(u32('outLen', outLen));
    const d = data('input', input);
    this.#core.call(this.#core, 'zxv_shake256', d, d.length, out, out.length);
    return out;
  }

  /** Keccak-256 with the original (pre-FIPS) padding, as Ethereum uses. */
  keccak256(input: BytesLike): Uint8Array {
    const out = new Uint8Array(32);
    const d = data('input', input);
    this.#core.call(this.#core, 'zxv_keccak256', d, d.length, out);
    return out;
  }
}

/** A key pair: public bytes and an off-heap secret. */
export interface KeyPair {
  readonly publicKey: Uint8Array;
  readonly secretKey: SecretKey;
}

function cc(cap: Capability): { core: Core; scope: Capability['context']['_state'] } {
  return { core: cap.context._core, scope: cap.context._state };
}

/** RFC 8032 Ed25519. Signing needs {@link Cap.CRYPTO_SIGN}. Secret key = the 32-byte seed. */
export const Ed25519 = Object.freeze({
  PUBLIC_KEY_BYTES: 32,
  SECRET_KEY_BYTES: 32,
  SIGNATURE_BYTES: 64,

  /** A fresh key pair from the OS CSPRNG. */
  generate(cap: Capability): KeyPair {
    const { core, scope } = cc(cap);
    const pk = new Uint8Array(32);
    const sk = new SecretKey(core, 32);
    try {
      core.call(scope, 'zxv_ed25519_keygen', cap._ptr(), pk, sk._ptr());
    } catch (e) {
      sk.close();
      throw e;
    }
    return Object.freeze({ publicKey: pk, secretKey: sk });
  },

  /** The public key of a secret key. */
  publicKey(cap: Capability, secretKey: SecretKey): Uint8Array {
    const { core, scope } = cc(cap);
    const pk = new Uint8Array(32);
    core.call(scope, 'zxv_ed25519_public', cap._ptr(), needSecret('secretKey', secretKey, 32, core)._ptr(), pk);
    return pk;
  },

  /** Sign a message (deterministic, per RFC 8032). */
  sign(cap: Capability, secretKey: SecretKey, message: BytesLike): Uint8Array {
    const { core, scope } = cc(cap);
    const m = data('message', message);
    const sig = new Uint8Array(64);
    core.call(scope, 'zxv_ed25519_sign', cap._ptr(), needSecret('secretKey', secretKey, 32, core)._ptr(), m, m.length, sig);
    return sig;
  },

  /** True if the signature verifies (a bad signature is not an error). */
  verify(cap: Capability, publicKey: Uint8Array, message: BytesLike, signature: Uint8Array): boolean {
    const { core, scope } = cc(cap);
    const m = data('message', message);
    return core.callAllowing(scope, [-21], 'zxv_ed25519_verify', cap._ptr(), bytes('publicKey', publicKey, 32), m, m.length,
      bytes('signature', signature, 64)) === 0;
  },
});

/** Options for ML-DSA signing and verification. */
export interface MlDsaOptions {
  /** FIPS 204 context string, at most 255 bytes (default empty). */
  context?: BytesLike;
}

function dsaCtx(o: MlDsaOptions): Uint8Array | null {
  if (o.context === undefined) return null;
  const c = data('context', o.context);
  if (c.length > 255) throw usage('the ML-DSA context is at most 255 bytes');
  return c;
}

/** FIPS 204 ML-DSA-87 (post-quantum signatures). Not FIPS 140-3 validated. */
export const MlDsa87 = Object.freeze({
  PUBLIC_KEY_BYTES: 2592,
  SECRET_KEY_BYTES: 4896,
  SIGNATURE_BYTES: 4627,

  /** A fresh key pair. */
  generate(cap: Capability): KeyPair {
    return keygen(cap, 'zxv_mldsa87_keygen', 2592, 4896, []);
  },

  /** The key pair of a 32-byte seed (deterministic; for known-answer tests). */
  fromSeed(cap: Capability, seed: Uint8Array): KeyPair {
    return keygen(cap, 'zxv_mldsa87_keygen_seed', 2592, 4896, [bytes('seed', seed, 32)]);
  },

  /** Hedged signing (fresh randomness each time). */
  sign(cap: Capability, secretKey: SecretKey, message: BytesLike, options: MlDsaOptions = {}): Uint8Array {
    return dsaSign(cap, 'zxv_mldsa87_sign', secretKey, message, options);
  },

  /** Deterministic signing (same input, same signature). */
  signDeterministic(cap: Capability, secretKey: SecretKey, message: BytesLike, options: MlDsaOptions = {}): Uint8Array {
    return dsaSign(cap, 'zxv_mldsa87_sign_deterministic', secretKey, message, options);
  },

  /** True if the signature verifies for this message and context. */
  verify(cap: Capability, publicKey: Uint8Array, message: BytesLike, signature: Uint8Array, options: MlDsaOptions = {}): boolean {
    const { core, scope } = cc(cap);
    const m = data('message', message);
    const c = dsaCtx(options);
    return core.callAllowing(scope, [-21], 'zxv_mldsa87_verify', cap._ptr(), bytes('publicKey', publicKey, 2592), m, m.length, c,
      c ? c.length : 0, bytes('signature', signature, 4627)) === 0;
  },
});

function keygen(cap: Capability, fn: string, pkLen: number, skLen: number, pre: Uint8Array[]): KeyPair {
  const { core, scope } = cc(cap);
  const pk = new Uint8Array(pkLen);
  const sk = new SecretKey(core, skLen);
  try {
    core.call(scope, fn, cap._ptr(), ...pre, pk, sk._ptr());
  } catch (e) {
    sk.close();
    throw e;
  }
  return Object.freeze({ publicKey: pk, secretKey: sk });
}

function dsaSign(cap: Capability, fn: string, secretKey: SecretKey, message: BytesLike, options: MlDsaOptions): Uint8Array {
  const { core, scope } = cc(cap);
  const m = data('message', message);
  const c = dsaCtx(options);
  const sig = new Uint8Array(4627);
  core.call(scope, fn, cap._ptr(), needSecret('secretKey', secretKey, 4896, core)._ptr(), m, m.length, c, c ? c.length : 0, sig);
  return sig;
}

/** The result of an ML-KEM encapsulation. */
export interface Encapsulation {
  /** Send this to the key owner. */
  readonly ciphertext: Uint8Array;
  /** The 32-byte shared secret, off-heap. */
  readonly sharedSecret: SecretKey;
}

/** FIPS 203 ML-KEM-1024 (post-quantum key encapsulation). KEM calls need {@link Cap.CRYPTO_KEM}. */
export const MlKem1024 = Object.freeze({
  ENCAPSULATION_KEY_BYTES: 1568,
  DECAPSULATION_KEY_BYTES: 3168,
  CIPHERTEXT_BYTES: 1568,
  SHARED_SECRET_BYTES: 32,

  /** A fresh key pair: `publicKey` is the encapsulation key, `secretKey` the decapsulation key. */
  generate(cap: Capability): KeyPair {
    return keygen(cap, 'zxv_mlkem1024_keygen', 1568, 3168, []);
  },

  /** Deterministic key pair from seeds d and z (FIPS 203 KeyGen_internal; for known-answer tests). */
  fromSeeds(cap: Capability, d: Uint8Array, z: Uint8Array): KeyPair {
    return keygen(cap, 'zxv_mlkem1024_keygen_derand', 1568, 3168, [bytes('d', d, 32), bytes('z', z, 32)]);
  },

  /** Encapsulate to an encapsulation key (a malformed key is ZXV_E_FORMAT). */
  encapsulate(cap: Capability, encapsulationKey: Uint8Array): Encapsulation {
    return encaps(cap, 'zxv_mlkem1024_encaps', encapsulationKey, []);
  },

  /** Deterministic encapsulation with message m (for known-answer tests). */
  encapsulateDerand(cap: Capability, encapsulationKey: Uint8Array, m: Uint8Array): Encapsulation {
    return encaps(cap, 'zxv_mlkem1024_encaps_derand', encapsulationKey, [bytes('m', m, 32)]);
  },

  /** Recover the shared secret. */
  decapsulate(cap: Capability, decapsulationKey: SecretKey, ciphertext: Uint8Array): SecretKey {
    const { core, scope } = cc(cap);
    const ss = new SecretKey(core, 32);
    try {
      core.call(scope, 'zxv_mlkem1024_decaps', cap._ptr(), needSecret('decapsulationKey', decapsulationKey, 3168, core)._ptr(),
        bytes('ciphertext', ciphertext, 1568), ss._ptr());
    } catch (e) {
      ss.close();
      throw e;
    }
    return ss;
  },
});

function encaps(cap: Capability, fn: string, ek: Uint8Array, pre: Uint8Array[]): Encapsulation {
  const { core, scope } = cc(cap);
  const ct = new Uint8Array(1568);
  const ss = new SecretKey(core, 32);
  try {
    core.call(scope, fn, cap._ptr(), bytes('encapsulationKey', ek, 1568), ...pre, ct, ss._ptr());
  } catch (e) {
    ss.close();
    throw e;
  }
  return Object.freeze({ ciphertext: ct, sharedSecret: ss });
}
