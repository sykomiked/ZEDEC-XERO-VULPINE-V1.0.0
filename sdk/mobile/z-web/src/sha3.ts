// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// SHA3-256 (FIPS 202) in TypeScript, for checking the wasm module and model
// files BEFORE they are used. WebCrypto has no SHA-3, and the module cannot
// vouch for itself, so this runs without libzxv. 64-bit lanes are kept as
// (lo, hi) pairs of 32-bit words. The tests check it against
// vectors.json and against libzxv's zxv_sha3_256 on random inputs.

/** Rotation offsets r[x + 5y] of the rho step. */
const RHO = [0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14];

/** Round constants as (lo, hi) pairs, from the FIPS 202 LFSR (no hand-typed table). */
const RC: Uint32Array = (() => {
  const out = new Uint32Array(48);
  let r = 1;
  const bit = (): number => {
    const b = r & 1;
    r = (r << 1) & 0x1ff;
    if (r & 0x100) r ^= 0x171;
    return b;
  };
  for (let round = 0; round < 24; round++) {
    let lo = 0;
    let hi = 0;
    for (let j = 0; j < 7; j++) {
      if (bit()) {
        const pos = (1 << j) - 1;
        if (pos < 32) lo |= 1 << pos;
        else hi |= 1 << (pos - 32);
      }
    }
    out[2 * round] = lo >>> 0;
    out[2 * round + 1] = hi >>> 0;
  }
  return out;
})();

const B = new Uint32Array(50);
const C = new Uint32Array(10);

function keccakF(s: Uint32Array): void {
  for (let round = 0; round < 24; round++) {
    // theta
    for (let x = 0; x < 5; x++) {
      let lo = 0;
      let hi = 0;
      for (let y = 0; y < 25; y += 5) {
        lo ^= s[2 * (x + y)]!;
        hi ^= s[2 * (x + y) + 1]!;
      }
      C[2 * x] = lo;
      C[2 * x + 1] = hi;
    }
    for (let x = 0; x < 5; x++) {
      const p = (x + 4) % 5;
      const n = (x + 1) % 5;
      const nlo = C[2 * n]!;
      const nhi = C[2 * n + 1]!;
      const dlo = C[2 * p]! ^ ((nlo << 1) | (nhi >>> 31));
      const dhi = C[2 * p + 1]! ^ ((nhi << 1) | (nlo >>> 31));
      for (let y = 0; y < 25; y += 5) {
        s[2 * (x + y)]! ^= dlo;
        s[2 * (x + y) + 1]! ^= dhi;
      }
    }
    // rho and pi: B[y, 2x + 3y] = rot(A[x, y], r[x, y])
    for (let x = 0; x < 5; x++) {
      for (let y = 0; y < 5; y++) {
        const i = x + 5 * y;
        let lo = s[2 * i]!;
        let hi = s[2 * i + 1]!;
        let n = RHO[i]!;
        if (n >= 32) {
          const t = lo;
          lo = hi;
          hi = t;
          n -= 32;
        }
        if (n > 0) {
          const l = (lo << n) | (hi >>> (32 - n));
          const h = (hi << n) | (lo >>> (32 - n));
          lo = l;
          hi = h;
        }
        const j = y + 5 * ((2 * x + 3 * y) % 5);
        B[2 * j] = lo;
        B[2 * j + 1] = hi;
      }
    }
    // chi
    for (let y = 0; y < 25; y += 5) {
      for (let x = 0; x < 5; x++) {
        const a = 2 * (x + y);
        const b = 2 * (((x + 1) % 5) + y);
        const c = 2 * (((x + 2) % 5) + y);
        s[a] = B[a]! ^ (~B[b]! & B[c]!);
        s[a + 1] = B[a + 1]! ^ (~B[b + 1]! & B[c + 1]!);
      }
    }
    // iota
    s[0]! ^= RC[2 * round]!;
    s[1]! ^= RC[2 * round + 1]!;
  }
}

const RATE = 136; // bytes, SHA3-256

/** SHA3-256 of `data` (32 bytes). */
export function sha3_256(data: Uint8Array): Uint8Array {
  const s = new Uint32Array(50);
  const block = new Uint8Array(RATE);
  const absorb = (bytes: Uint8Array, off: number): void => {
    for (let i = 0; i < RATE / 4; i++) {
      const k = off + 4 * i;
      s[i]! ^= (bytes[k]! | (bytes[k + 1]! << 8) | (bytes[k + 2]! << 16) | (bytes[k + 3]! << 24)) >>> 0;
    }
    keccakF(s);
  };
  let off = 0;
  for (; off + RATE <= data.length; off += RATE) absorb(data, off);
  block.set(data.subarray(off));
  const tail = data.length - off;
  block[tail]! ^= 0x06;
  block[RATE - 1]! ^= 0x80;
  absorb(block, 0);
  const out = new Uint8Array(32);
  for (let i = 0; i < 8; i++) {
    const w = s[i]!;
    out[4 * i] = w & 0xff;
    out[4 * i + 1] = (w >>> 8) & 0xff;
    out[4 * i + 2] = (w >>> 16) & 0xff;
    out[4 * i + 3] = (w >>> 24) & 0xff;
  }
  return out;
}

/** Lower-case hex of `bytes`. */
export function toHex(bytes: Uint8Array): string {
  let h = '';
  for (const b of bytes) h += b.toString(16).padStart(2, '0');
  return h;
}

/** SHA3-256 of `data` as 64 lower-case hex digits. */
export function sha3_256Hex(data: Uint8Array): string {
  return toHex(sha3_256(data));
}

/** True for a well-formed SHA3-256 digest in hex (64 hex digits, either case). */
export function isDigestHex(text: unknown): text is string {
  return typeof text === 'string' && /^[0-9a-fA-F]{64}$/.test(text);
}
