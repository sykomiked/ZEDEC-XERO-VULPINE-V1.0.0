// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import {
  type DynamicModule, Inject, Injectable, type InjectionToken, Module, type ModuleMetadata, type OnApplicationShutdown,
  type OptionalFactoryDependency, type Provider,
} from '@nestjs/common';
import { TerminusModule } from '@nestjs/terminus';

import { ZxvCapabilities } from './capabilities.js';
import { ZxvExceptionFilter } from './exception-filter.js';
import { ZxvFatalState } from './fatal-state.js';
import { ZxvHealthIndicator } from './health.js';
import { ExactJsonInterceptor } from './interceptor.js';
import { checkDeclaredNames, type ResolvedZxvOptions, resolveOptions, type ZxvModuleOptions } from './options.js';
import { ZxvRuntime } from './runtime.js';
import { capabilityToken, ZXV_CONTEXT, ZXV_LIBRARY, ZXV_OPTIONS, ZXV_RUNTIME } from './tokens.js';

/** Options of {@link ZxvModule.forRootAsync}. */
export interface ZxvModuleAsyncOptions {
  /** Modules whose providers the factory injects (e.g. `ConfigModule`). */
  readonly imports?: ModuleMetadata['imports'];
  /** Tokens passed to the factory. */
  readonly inject?: (InjectionToken | OptionalFactoryDependency)[];
  /** Returns the options, possibly asynchronously. */
  readonly useFactory: (...args: never[]) => ZxvModuleOptions | Promise<ZxvModuleOptions>;
  /**
   * The capability names to register as injectable providers. Nest needs
   * provider tokens before the factory runs, so they are declared here; the
   * factory's `capabilities` must name exactly these (checked at start-up).
   */
  readonly capabilityNames?: readonly string[];
  /** Register the module globally (default true). */
  readonly isGlobal?: boolean;
}

/** Closes the runtime when the application shuts down (`app.close()`, or a signal with `enableShutdownHooks()`). */
@Injectable()
class ZxvShutdown implements OnApplicationShutdown {
  readonly #runtime: ZxvRuntime;

  constructor(@Inject(ZXV_RUNTIME) runtime: ZxvRuntime) {
    this.#runtime = runtime;
  }

  /** The last lifecycle hook, so other providers' `onModuleDestroy` can still use the context. */
  onApplicationShutdown(): void {
    this.#runtime.close();
  }
}

function common(optionsProvider: Provider, names: readonly string[], isGlobal: boolean, imports: ModuleMetadata['imports'] = []): DynamicModule {
  const named: Provider[] = names.map((name) => ({
    provide: capabilityToken(name),
    useFactory: (rt: ZxvRuntime) => rt.capability(name),
    inject: [ZXV_RUNTIME],
  }));
  return {
    module: ZxvModule,
    global: isGlobal,
    imports: [...imports, TerminusModule],
    providers: [
      optionsProvider,
      { provide: ZXV_RUNTIME, useFactory: (o: ResolvedZxvOptions) => ZxvRuntime.create(o), inject: [ZXV_OPTIONS] },
      { provide: ZXV_CONTEXT, useFactory: (rt: ZxvRuntime) => rt.context, inject: [ZXV_RUNTIME] },
      { provide: ZXV_LIBRARY, useFactory: (rt: ZxvRuntime) => rt.zxv, inject: [ZXV_RUNTIME] },
      ...named,
      ZxvShutdown,
      ZxvCapabilities,
      ZxvFatalState,
      ZxvHealthIndicator,
      ZxvExceptionFilter,
      ExactJsonInterceptor,
    ],
    exports: [
      ZXV_OPTIONS, ZXV_CONTEXT, ZXV_LIBRARY, ...names.map(capabilityToken),
      ZxvCapabilities, ZxvFatalState, ZxvHealthIndicator, ZxvExceptionFilter, ExactJsonInterceptor,
    ],
  };
}

/**
 * The ZXV module: one `ZxvContext` per application, created when the
 * application starts (a bad option or a missing libzxv fails start-up, not
 * the first request) and closed on shutdown in ABI.md R6 order: named
 * capabilities first, then the root, then the context.
 *
 * ```ts
 * @Module({ imports: [ZxvModule.forRoot({
 *   currencies: ['NGN'],
 *   rootRights: ['LEDGER_READ', 'LEDGER_POST'],
 *   capabilities: { teller: ['LEDGER_READ', 'LEDGER_POST'], auditor: ['LEDGER_READ'] },
 * })] })
 * export class AppModule {}
 *
 * constructor(@InjectCapability('teller') teller: Capability) {}
 * ```
 *
 * Global by default. Exports: the context (`@InjectZxvContext()`), the
 * library (`@InjectZxv()`), each named capability, {@link ZxvCapabilities},
 * {@link ZxvFatalState}, {@link ZxvHealthIndicator},
 * {@link ZxvExceptionFilter} and {@link ExactJsonInterceptor}. The root
 * capability has no token.
 */
@Module({})
export class ZxvModule {
  /** Configures the module with static options. */
  static forRoot(options: ZxvModuleOptions & { readonly isGlobal?: boolean }): DynamicModule {
    const resolved = resolveOptions(options);
    return common({ provide: ZXV_OPTIONS, useValue: resolved }, [...resolved.capabilities.keys()], options.isGlobal ?? true);
  }

  /** Configures the module from a factory (e.g. over `ConfigService`). */
  static forRootAsync(options: ZxvModuleAsyncOptions): DynamicModule {
    const names = [...(options.capabilityNames ?? [])];
    checkDeclaredNames(names);
    const factory = options.useFactory as (...args: unknown[]) => ZxvModuleOptions | Promise<ZxvModuleOptions>;
    return common(
      {
        provide: ZXV_OPTIONS,
        useFactory: async (...args: unknown[]) => resolveOptions(await factory(...args), names),
        inject: options.inject ?? [],
      },
      names,
      options.isGlobal ?? true,
      options.imports,
    );
  }
}
