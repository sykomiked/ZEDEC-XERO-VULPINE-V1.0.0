// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Cap, type FatalPolicy, type ZxvFatalError } from '@zedec/zxv';

/** A capability right by name (`ZXV_CAP_*`), or `ALL`. */
export type CapabilityRight = keyof typeof Cap;

/**
 * Rights as configuration: names (`['LEDGER_READ', 'LEDGER_POST']`), one
 * comma-separated string (`'LEDGER_READ, LEDGER_POST'`, case-insensitive,
 * convenient for environment variables), or `Cap` bits as a bigint
 * (`Cap.LEDGER_READ | Cap.LEDGER_POST`). Numbers are refused.
 */
export type RightsSpec = readonly CapabilityRight[] | string | bigint;

/** A platform-private unit registered at start-up (ledger-only, never an ISO 20022 currency). */
export interface PrivateUnitOption {
  /** 3 to 8 upper-case letters and digits, not an ISO 4217 code (e.g. `VFV`). */
  readonly code: string;
  /** Decimal places, 0 to 8. */
  readonly minorUnits: number;
}

/** Options of {@link ZxvModule.forRoot}. */
export interface ZxvModuleOptions {
  /**
   * The SDK backend. Default `'native'` (the Node-API addon over libzxv),
   * so a missing library fails start-up instead of silently running on
   * WebAssembly. `'auto'` falls back to wasm; `'wasm'` forces it.
   */
  readonly backend?: 'native' | 'auto' | 'wasm';
  /** Explicit path to libzxv; otherwise `ZXV_LIBRARY_PATH`, the bundled copy, the system loader. */
  readonly libraryPath?: string;
  /** What a FATAL status does: `'throw'` (default) or `'abort'` (ends the process). */
  readonly fatalPolicy?: FatalPolicy;
  /** ISO 4217 currencies enabled when the context is created (e.g. `['NGN']`). */
  readonly currencies?: readonly string[];
  /** Private units registered when the context is created. */
  readonly privateUnits?: readonly PrivateUnitOption[];
  /** Rights of the root capability (default every right). Every named capability must be a subset. */
  readonly rootRights?: RightsSpec;
  /**
   * Named capabilities, each attenuated from the root: inject them with
   * `@InjectCapability('<name>')`. The root itself is never injectable.
   */
  readonly capabilities?: Readonly<Record<string, RightsSpec>>;
  /**
   * Called once, the first time the exception filter or the health
   * indicator sees the context halted (e.g. to stop the process so an
   * orchestrator restarts it). Must not throw.
   */
  readonly onFatal?: (error: ZxvFatalError | undefined) => void;
}

/** Options validated and normalised to bits. */
export interface ResolvedZxvOptions {
  readonly backend: 'native' | 'auto' | 'wasm';
  readonly libraryPath: string | undefined;
  readonly fatalPolicy: FatalPolicy;
  readonly currencies: readonly string[];
  readonly privateUnits: readonly PrivateUnitOption[];
  readonly rootRights: bigint;
  /** Named capabilities in declaration order. */
  readonly capabilities: ReadonlyMap<string, bigint>;
  readonly onFatal: ((error: ZxvFatalError | undefined) => void) | undefined;
}

/** Thrown at start-up for options that cannot work. Lists every problem found. */
export class ZxvConfigurationError extends Error {
  /** One line per problem. */
  readonly problems: readonly string[];

  constructor(problems: readonly string[]) {
    super(`Invalid ZxvModule options:\n- ${problems.join('\n- ')}`);
    this.name = 'ZxvConfigurationError';
    this.problems = problems;
  }
}

const RIGHT_NAMES = Object.keys(Cap) as CapabilityRight[];
const NAME = /^[A-Za-z][A-Za-z0-9_-]{0,63}$/;
const CCY = /^[A-Z]{3}$/;
const PRIVATE = /^[A-Z0-9]{3,8}$/;

/** The names of the rights set in `bits` (never `ALL`). */
export function rightNames(bits: bigint): CapabilityRight[] {
  return RIGHT_NAMES.filter((n) => n !== 'ALL' && (bits & Cap[n]) !== 0n);
}

/**
 * Parses a {@link RightsSpec} to `Cap` bits. Throws {@link ZxvConfigurationError}
 * for an unknown name, a number, an empty set or bits outside `Cap.ALL`.
 */
export function parseRights(spec: unknown, what: string): bigint {
  const problems: string[] = [];
  const bits = parseRightsInto(spec, what, problems);
  if (problems.length > 0) throw new ZxvConfigurationError(problems);
  return bits;
}

function parseRightsInto(spec: unknown, what: string, problems: string[]): bigint {
  if (typeof spec === 'bigint') {
    if (spec <= 0n || (spec & ~Cap.ALL) !== 0n) {
      problems.push(`${what}: ${spec} is not a non-empty set of Cap bits`);
      return 0n;
    }
    return spec;
  }

  let names: readonly unknown[];
  if (typeof spec === 'string') {
    names = spec.split(/[,|]/).map((s) => s.trim()).filter((s) => s.length > 0);
  } else if (Array.isArray(spec)) {
    names = spec;
  } else {
    problems.push(`${what}: rights must be names (e.g. ['LEDGER_READ']), a comma-separated string or Cap bits as a bigint; got ${typeof spec}`);
    return 0n;
  }

  let bits = 0n;
  for (const n of names) {
    const key = typeof n === 'string' ? n.trim().toUpperCase() : undefined;
    if (key === undefined || !(RIGHT_NAMES as string[]).includes(key)) {
      problems.push(`${what}: unknown right ${JSON.stringify(n)} (known: ${RIGHT_NAMES.join(', ')})`);
      continue;
    }
    bits |= Cap[key as CapabilityRight];
  }

  if (bits === 0n && problems.length === 0) problems.push(`${what}: holds no right`);
  return bits;
}

/** Validates options and normalises rights to bits. Throws {@link ZxvConfigurationError}. */
export function resolveOptions(o: ZxvModuleOptions | null | undefined, declaredNames?: readonly string[]): ResolvedZxvOptions {
  const problems: string[] = [];
  if (o === null || typeof o !== 'object') {
    throw new ZxvConfigurationError(['options must be an object']);
  }

  const backend = o.backend ?? 'native';
  if (backend !== 'native' && backend !== 'auto' && backend !== 'wasm') problems.push(`backend: '${String(backend)}' is not 'native', 'auto' or 'wasm'`);
  const fatalPolicy = o.fatalPolicy ?? 'throw';
  if (fatalPolicy !== 'throw' && fatalPolicy !== 'abort') problems.push(`fatalPolicy: '${String(fatalPolicy)}' is not 'throw' or 'abort'`);
  if (o.libraryPath !== undefined && (typeof o.libraryPath !== 'string' || o.libraryPath.length === 0)) problems.push('libraryPath: must be a non-empty string');

  const currencies = [...(o.currencies ?? [])];
  for (const c of currencies) {
    if (typeof c !== 'string' || !CCY.test(c)) problems.push(`currencies: ${JSON.stringify(c)} is not an ISO 4217 alpha-3 code`);
  }

  const privateUnits = [...(o.privateUnits ?? [])];
  for (const p of privateUnits) {
    if (typeof p?.code !== 'string' || !PRIVATE.test(p.code)) problems.push(`privateUnits: ${JSON.stringify(p?.code)} is not 3 to 8 upper-case letters and digits`);
    if (!Number.isInteger(p?.minorUnits) || p.minorUnits < 0 || p.minorUnits > 8) problems.push(`privateUnits: minorUnits of ${JSON.stringify(p?.code)} must be an integer 0..8`);
  }

  const rootRights = o.rootRights === undefined ? Cap.ALL : parseRightsInto(o.rootRights, 'rootRights', problems);
  const capabilities = new Map<string, bigint>();
  for (const [name, spec] of Object.entries(o.capabilities ?? {})) {
    if (!NAME.test(name)) {
      problems.push(`capabilities: '${name}' is not a valid name (a letter, then letters, digits, '-' or '_', at most 64)`);
      continue;
    }
    if (name.toLowerCase() === 'root') {
      problems.push("capabilities: 'root' is reserved; the root capability is never injectable");
      continue;
    }
    const bits = parseRightsInto(spec, `capabilities.${name}`, problems);
    if (bits !== 0n && (bits & ~rootRights) !== 0n) {
      problems.push(`capabilities.${name}: asks for ${rightNames(bits & ~rootRights).join(', ')}, which the root does not hold`);
    }
    capabilities.set(name, bits);
  }

  if (declaredNames !== undefined) {
    const declared = new Set(declaredNames);
    for (const n of capabilities.keys()) {
      if (!declared.has(n)) problems.push(`capabilities.${n}: not listed in forRootAsync({ capabilityNames })`);
    }
    for (const n of declared) {
      if (!capabilities.has(n)) problems.push(`capabilityNames: '${n}' has no rights in the options the factory returned`);
    }
  }

  if (o.onFatal !== undefined && typeof o.onFatal !== 'function') problems.push('onFatal: must be a function');
  if (problems.length > 0) throw new ZxvConfigurationError(problems);

  return Object.freeze({
    backend,
    libraryPath: o.libraryPath,
    fatalPolicy,
    currencies,
    privateUnits,
    rootRights,
    capabilities,
    onFatal: o.onFatal,
  });
}

/** Checks capability names declared for {@link ZxvModule.forRootAsync} before any provider exists. */
export function checkDeclaredNames(names: readonly string[]): void {
  const problems: string[] = [];
  const seen = new Set<string>();
  for (const n of names) {
    if (typeof n !== 'string' || !NAME.test(n)) problems.push(`capabilityNames: ${JSON.stringify(n)} is not a valid name`);
    else if (n.toLowerCase() === 'root') problems.push("capabilityNames: 'root' is reserved; the root capability is never injectable");
    else if (seen.has(n)) problems.push(`capabilityNames: '${n}' is listed twice`);
    seen.add(n);
  }
  if (problems.length > 0) throw new ZxvConfigurationError(problems);
}
