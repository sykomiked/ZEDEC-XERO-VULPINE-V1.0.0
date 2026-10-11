// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { usage } from './errors.js';
import { bytes as checkBytes, toHex, u32, u8 } from './internal/check.js';

/** Octets in one UBH-168 frame (168 bits). */
export const UBH168_FRAME_BYTES = 21;

/**
 * What the W5 correlation resolver reports for a frame
 * (`zxv_wire_resolve`, ZXV_TRUTH_*). This six-state wire truth is not
 * the four-valued {@link Truth}: it adds NEUTRAL and the GLUT/PARADOX
 * forms of a held contradiction.
 */
export const WireTruth = Object.freeze({
  UNKNOWN: 0,
  TRUE: 1,
  FALSE: 2,
  NEUTRAL: 3,
  /** Contradiction held. */
  GLUT: 4,
  /** Persistent GLUT. */
  PARADOX: 5,
} as const);

/** A WireTruth value. */
export type WireTruth = (typeof WireTruth)[keyof typeof WireTruth];

/** The 20 payload octets of every idle pilot frame ("1VXZZXV1" repeating; never all zeros). */
const IDLE = new Uint8Array([0x31, 0x56, 0x58, 0x5a, 0x5a, 0x58, 0x56, 0x31, 0x31, 0x56, 0x58, 0x5a, 0x5a, 0x58, 0x56, 0x31, 0x31, 0x56, 0x58, 0x5a]);

/** Three S+ words (W0, W2, W4) as unsigned 32-bit integers. */
export type SPlus = readonly [number, number, number];
/** Two S- words (W1, W3) as unsigned 32-bit integers. */
export type SMinus = readonly [number, number];

/**
 * One UBH-168 harmonic wire frame, implemented in pure TypeScript so a
 * service can frame and parse without the native library (SDK_DESIGN §11).
 *
 * 21 octets: the tag, then five 32-bit words in the fixed L-B-L-B-L byte
 * order: W0 little-endian (S+), W1 big-endian (S-), W2 LE (S+), W3 BE (S-),
 * W4 LE (S+). The octets are identical on every host; the test suite checks
 * them byte for byte against vectors.json and against the C library.
 *
 * Immutable. Words are plain integers (never fractional); they are read by
 * the resolver as signed Q16.16, which {@link Zxv.wire} exposes.
 */
export class Ubh168Frame {
  /** The tag octet (100*sync + 10*shell + trunk for tags made by {@link Ubh168Frame.tag}). */
  readonly tag: number;
  /** S+ rails: W0, W2, W4. */
  readonly sPlus: SPlus;
  /** S- rails: W1, W3. */
  readonly sMinus: SMinus;
  readonly #octets: Uint8Array;

  private constructor(tag: number, sPlus: SPlus, sMinus: SMinus, octets: Uint8Array) {
    this.tag = tag;
    this.sPlus = Object.freeze([...sPlus]) as unknown as SPlus;
    this.sMinus = Object.freeze([...sMinus]) as unknown as SMinus;
    this.#octets = octets;
    Object.freeze(this);
  }

  /** The tag for a trunk (0..9), shell (0..9) and sync bit (0..1): 100*sync + 10*shell + trunk. */
  static tag(trunk: number, shell: number, sync: number): number {
    const t = u32('trunk', trunk);
    const s = u32('shell', shell);
    const y = u32('sync', sync);
    if (t > 9 || s > 9 || y > 1) throw usage('trunk and shell must be 0..9, sync 0..1');
    return 100 * y + 10 * s + t;
  }

  /** Pack a tag and the five rail words into a frame. */
  static pack(tag: number, sPlus: SPlus, sMinus: SMinus): Ubh168Frame {
    const t = u8('tag', tag);
    if (!Array.isArray(sPlus) || sPlus.length !== 3) throw usage('sPlus must be three uint32 words');
    if (!Array.isArray(sMinus) || sMinus.length !== 2) throw usage('sMinus must be two uint32 words');
    const p = sPlus.map((w, i) => u32(`sPlus[${i}]`, w)) as unknown as SPlus;
    const m = sMinus.map((w, i) => u32(`sMinus[${i}]`, w)) as unknown as SMinus;
    const out = new Uint8Array(UBH168_FRAME_BYTES);
    const dv = new DataView(out.buffer);
    dv.setUint8(0, t);
    dv.setUint32(1, p[0], true); // W0 LE  S+
    dv.setUint32(5, m[0], false); // W1 BE  S-
    dv.setUint32(9, p[1], true); // W2 LE  S+
    dv.setUint32(13, m[1], false); // W3 BE  S-
    dv.setUint32(17, p[2], true); // W4 LE  S+
    return new Ubh168Frame(t, p, m, out);
  }

  /** Parse 21 octets. */
  static unpack(octets: Uint8Array): Ubh168Frame {
    const b = checkBytes('frame', octets, UBH168_FRAME_BYTES);
    const dv = new DataView(b.buffer, b.byteOffset, UBH168_FRAME_BYTES);
    return new Ubh168Frame(
      dv.getUint8(0),
      [dv.getUint32(1, true), dv.getUint32(9, true), dv.getUint32(17, true)],
      [dv.getUint32(5, false), dv.getUint32(13, false)],
      b.slice(),
    );
  }

  /** The idle pilot frame of a trunk (0..9): tag 100 + trunk, a fixed non-zero payload. */
  static idle(trunk: number): Ubh168Frame {
    const t = u32('trunk', trunk);
    if (t > 9) throw usage('trunk must be 0..9');
    const out = new Uint8Array(UBH168_FRAME_BYTES);
    out[0] = 100 + t;
    out.set(IDLE, 1);
    return Ubh168Frame.unpack(out);
  }

  /** Trunk digit of the tag (tag % 10). */
  get trunk(): number {
    return this.tag % 10;
  }

  /** Shell digit of the tag ((tag / 10) % 10). */
  get shell(): number {
    return Math.trunc(this.tag / 10) % 10;
  }

  /** Sync digit of the tag (tag >= 100 ? 1 : 0 for tags up to 199). */
  get sync(): number {
    return Math.trunc(this.tag / 100);
  }

  /** A fresh copy of the 21 octets. */
  toBytes(): Uint8Array {
    return this.#octets.slice();
  }

  /** Lower-case hex of the octets. */
  toHex(): string {
    return toHex(this.#octets);
  }

  /** Byte-for-byte equality. */
  equals(other: Ubh168Frame): boolean {
    return this.toHex() === other.toHex();
  }
}
