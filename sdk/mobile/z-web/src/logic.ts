// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Four-valued truth and the hexagram tag on the page. These are the
// @zedec/zxv classes themselves (pure TypeScript, usable before the worker
// starts), plus their wire forms: a Truth travels as its name, a Hexagram
// as its tag byte 0..63.
//
// Remember: `if (truth)` is always taken. Use isCertainlyTrue(),
// toBoolean({ onBoth, onNeither }) or require(). BOTH is a held
// contradiction, not an error; NEITHER is missing evidence.

import { Hexagram, Truth } from '@zedec/zxv/wasm';

import type { WireTruth } from './protocol.js';
import { wireFormat } from './wire.js';

export {
  Truth, TruthNotCertainError, toBoolean, Hexagram, HexBase, HexPolarity, HexPhase, HexAction, HEX_ACTION_NAMES, Rational,
} from '@zedec/zxv/wasm';
export type { TruthName, TruthPolicy } from '@zedec/zxv/wasm';

/** A Truth as it travels: its name. */
export function truthToWire(t: Truth): WireTruth {
  if (!(t instanceof Truth)) throw new TypeError('expected a Truth');
  return t.name;
}

/** A wire truth name back to the Truth singleton (compare with ===). */
export function truthFromWire(name: unknown): Truth {
  if (name !== 'NEITHER' && name !== 'TRUE' && name !== 'FALSE' && name !== 'BOTH') throw wireFormat('truth', name);
  return Truth.fromName(name);
}

/** A Hexagram as it travels: its tag byte. */
export function hexagramToWire(h: Hexagram): number {
  if (!(h instanceof Hexagram)) throw new TypeError('expected a Hexagram');
  return h.tag;
}

/** A tag byte 0..63 back to its Hexagram. */
export function hexagramFromWire(tag: unknown): Hexagram {
  if (typeof tag !== 'number' || !Number.isInteger(tag) || tag < 0 || tag > 63) throw wireFormat('hexagram tag', tag);
  return Hexagram.fromTag(tag);
}
