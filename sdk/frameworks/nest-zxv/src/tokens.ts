// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Inject } from '@nestjs/common';

/** Injection token of the `ZxvContext` (`@InjectZxvContext()`). */
export const ZXV_CONTEXT = Symbol.for('zxv:context');

/** Injection token of the loaded `Zxv` library (version, features, pure helpers). */
export const ZXV_LIBRARY = Symbol.for('zxv:library');

/** Injection token of the validated options ({@link ResolvedZxvOptions}). */
export const ZXV_OPTIONS = Symbol.for('zxv:options');

/** @internal Injection token of the runtime that owns the context; not exported from the package. */
export const ZXV_RUNTIME = Symbol.for('zxv:runtime');

/** The injection token of the named capability `name`. */
export function capabilityToken(name: string): string {
  return `zxv:capability:${name}`;
}

/** Injects the named capability declared in `capabilities` (`@InjectCapability('teller')`). */
export function InjectCapability(name: string): PropertyDecorator & ParameterDecorator {
  return Inject(capabilityToken(name));
}

/** Injects the `ZxvContext`. */
export function InjectZxvContext(): PropertyDecorator & ParameterDecorator {
  return Inject(ZXV_CONTEXT);
}

/** Injects the loaded `Zxv` library. */
export function InjectZxv(): PropertyDecorator & ParameterDecorator {
  return Inject(ZXV_LIBRARY);
}
