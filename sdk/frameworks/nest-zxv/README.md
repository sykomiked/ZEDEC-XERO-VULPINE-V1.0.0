<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# @zedec/zxv-nestjs

NestJS integration over the [ZXV TypeScript SDK](../../typescript/)
(`@zedec/zxv`, Node-API backend). `ZxvModule.forRoot(...)` gives your
application:

- one `ZxvContext`, created when the application starts (so a bad option
  or a missing `libzxv` fails start-up, not the first request) and closed
  on shutdown in the destroy order of [ABI.md R6](../../docs/ABI.md#r6-threading):
  named capabilities, then the root, then the context;
- narrow `Capability` providers declared in options and injected with
  `@InjectCapability('teller')`; the root capability has no token and is
  never injectable;
- `ZxvHealthIndicator` for `@nestjs/terminus`: DOWN when the context is
  halted or closed (fail-closed);
- `ZxvExceptionFilter`: ZXV errors answered as RFC 9457 problem details by
  status class, without the native detail text;
- exact numbers at the HTTP boundary: `ParseRationalPipe`, `ExactBodyPipe`
  and the `exactJson()` body parser, which refuse JSON floats, and
  `ExactJsonInterceptor`, which writes bigints as exact strings.

Requirements: Node 22+, NestJS 12 (ESM), `libzxv` and the TypeScript SDK
built from this repository.

## Quickstart

```sh
make -C sdk/libzxv all                         # sdk/libzxv/build/libzxv.so
(cd sdk/typescript && npm ci && npm run build:ts)
cd sdk/frameworks/nest-zxv
npm ci
npm run sample                                 # http://localhost:3080
```

In your application (ESM, `"type": "module"`, with `experimentalDecorators`
and `emitDecoratorMetadata`):

```ts
import 'reflect-metadata';
import { Body, Controller, Get, Injectable, Module, Post, Query } from '@nestjs/common';
import { APP_FILTER, APP_INTERCEPTOR, NestFactory } from '@nestjs/core';
import { HealthCheck, HealthCheckService, TerminusModule } from '@nestjs/terminus';
import { type Capability, Ledger, Rational } from '@zedec/zxv';
import {
  ExactBodyPipe, type ExactBody, ExactJsonInterceptor, InjectCapability, ParseRationalPipe,
  ZxvExceptionFilter, ZxvHealthIndicator, ZxvModule, exactJson,
} from '@zedec/zxv-nestjs';

const Quote = new ExactBodyPipe({ price: 'rational', quantity: 'rational' });

@Controller()
class AppController {
  readonly #ledger: Ledger;
  constructor(@InjectCapability('auditor') auditor: Capability,
              private readonly health: HealthCheckService, private readonly zxv: ZxvHealthIndicator) {
    this.#ledger = Ledger.of(auditor);
  }

  @Post('quote') quote(@Body(Quote) q: ExactBody<typeof Quote.schema>) { return { total: q.price.mul(q.quantity) }; }
  @Get('sum') sum(@Query('a', ParseRationalPipe) a: Rational, @Query('b', ParseRationalPipe) b: Rational) { return { sum: a.add(b) }; }
  @Get('health') @HealthCheck() ready() { return this.health.check([() => this.zxv.isHealthy()]); }
}

@Module({
  imports: [
    ZxvModule.forRoot({
      currencies: ['NGN'],
      rootRights: ['LEDGER_READ', 'LEDGER_POST'],
      capabilities: { teller: ['LEDGER_READ', 'LEDGER_POST'], auditor: ['LEDGER_READ'] },
    }),
    TerminusModule,
  ],
  controllers: [AppController],
  providers: [
    { provide: APP_FILTER, useExisting: ZxvExceptionFilter },
    { provide: APP_INTERCEPTOR, useExisting: ExactJsonInterceptor },
  ],
})
class AppModule {}

const app = await NestFactory.create(AppModule, { bodyParser: false });
app.use(exactJson());            // refuses 0.1, 1.0, 1e3 and integers beyond 2^53 in any JSON body
app.enableShutdownHooks();       // SIGTERM closes the context in ABI order
await app.listen(3080);
```

With configuration from a factory (Nest needs the capability names before
the factory runs, so declare them):

```ts
ZxvModule.forRootAsync({
  imports: [ConfigModule],
  inject: [ConfigService],
  capabilityNames: ['teller', 'auditor'],
  useFactory: (c: ConfigService) => ({
    currencies: c.get('ZXV_CURRENCIES', 'NGN').split(','),
    capabilities: { teller: c.get('ZXV_CAP_TELLER'), auditor: c.get('ZXV_CAP_AUDITOR') }, // "LEDGER_READ, LEDGER_POST"
  }),
});
```

### The sample

[`sample/`](sample/) is a small treasury: exact quotes, a ledger posted
through `teller` and read through `auditor`, health probes and problem
details. `npm run sample` listens on `http://localhost:3080` (`PORT` to
change it).

```sh
curl -s localhost:3080/quote -H 'content-type: application/json' \
     -d '{"price":"12.75","quantity":"1/3"}'          # {"total":"4.25"}
curl -s localhost:3080/quote -H 'content-type: application/json' \
     -d '{"price":12.75,"quantity":"1"}'              # 400 urn:zxv:problem:json_float
curl -s 'localhost:3080/sum?a=0.1&b=0.2'              # {"sum":"0.3"}
curl -s localhost:3080/payments -H 'content-type: application/json' \
     -d '{"amountMinor":15000,"reference":"E2E-1"}'   # {"entryId":"1"}, posted with teller
curl -s localhost:3080/refunds -H 'content-type: application/json' \
     -d '{"amountMinor":100,"reference":"E2E-2"}'     # 403 problem: auditor cannot post
curl -s localhost:3080/balance                        # {"debit":"15000","credit":"0","equity":"15000"}
curl -s localhost:3080/health/ready                   # {"status":"ok",...}
```

The refusal (no account, reference or native detail text):

```json
{"type":"urn:zxv:problem:zxv_e_cap","title":"Operation refused","status":403,"code":"ZXV_E_CAP","class":"refused"}
```

Start it with `ZXV_SAMPLE_HALT=1` to get `POST /admin/halt`, which halts the
context: the next ZXV call answers 503 and `/health/ready` turns DOWN.
Never enable it outside a local demo.

## Options

`ZxvModule.forRoot(options)`; everything is validated when the module is
built (`forRoot`) or when the factory resolves (`forRootAsync`), and every
problem is listed in one `ZxvConfigurationError`.

| Option | Default | Meaning |
|---|---|---|
| `backend` | `'native'` | `'native'` (Node-API addon; a missing libzxv fails start-up), `'auto'` (falls back to wasm), `'wasm'`. |
| `libraryPath` | none | Explicit libzxv; otherwise `ZXV_LIBRARY_PATH`, the bundled copy, the system loader. |
| `fatalPolicy` | `'throw'` | `'abort'` ends the process on a FATAL status (the only uncatchable form). |
| `currencies` | `[]` | ISO 4217 codes enabled at start-up. A code libzxv refuses fails start-up. |
| `privateUnits` | `[]` | `{ code, minorUnits }` ledger-only units (e.g. `VFV`). |
| `rootRights` | every right | Rights of the root. Every named capability must be a subset. |
| `capabilities` | `{}` | `name -> rights`. One injectable `Capability` per name. |
| `onFatal` | none | Called once when the filter or the health indicator first sees the context halted (e.g. to exit so an orchestrator restarts the process). |
| `isGlobal` | `true` | Register the module globally. |

Rights are names (`LEDGER_READ`, `LEDGER_POST`, `CRYPTO_SIGN`, `CRYPTO_KEM`,
`AI_INFERENCE`, `BUDGET`, `PHASE`, `ATTEST`, `SWARM`, `EVM`, `ALL`) as an
array or one comma-separated, case-insensitive string, or `Cap` bits as a
`bigint`. Numbers are refused. A name is a letter followed by letters,
digits, `-` or `_`; `root` is reserved. Currencies and private units are
set up through a short-lived capability with `LEDGER_POST` only, closed
before anything else exists, so `rootRights` need not include it.

## How it behaves

**Providers** (exported from the module):

| Token | Value | Inject with |
|---|---|---|
| `ZXV_CONTEXT` | the `ZxvContext` | `@InjectZxvContext()` |
| `ZXV_LIBRARY` | the loaded `Zxv` (version, features, pure helpers) | `@InjectZxv()` |
| `capabilityToken(name)` | the named `Capability` | `@InjectCapability(name)` |
| `ZxvCapabilities` | `names`, `get(name)` (never the root) | constructor type |
| `ZxvFatalState` | `isTripped`, `status` | constructor type |
| `ZxvHealthIndicator`, `ZxvExceptionFilter`, `ExactJsonInterceptor` | | constructor type, `APP_FILTER`, `APP_INTERCEPTOR` |
| root capability | none | **not injectable** |

**Shutdown.** The context is closed in `onApplicationShutdown`, the last
lifecycle hook, so your providers can still use it in `onModuleDestroy`.
Named capabilities close first (reverse declaration order), then the root,
then the context. Call `app.close()`, or `app.enableShutdownHooks()` for
signals.

**Errors.** `ZxvExceptionFilter` catches `ZxvError` and `ZxvFatalError`
only; everything else is left to Nest. One table (`httpStatusFor`):

| ZXV status | HTTP |
|---|---|
| `ZXV_E_NOT_FOUND` | 404 |
| `ZXV_E_DUPLICATE` | 409 |
| `ZXV_E_CAP` | 403 |
| `ZXV_E_FUNDS`, `ZXV_E_CURRENCY`, `ZXV_E_DECLINED`, `ZXV_E_VERIFY` | 422 |
| `ZXV_E_BUDGET` | 429 |
| `ZXV_E_NOTIMPL` | 501 |
| `ZXV_E_CAPACITY`, `ZXV_E_NOMEM` | 503 |
| any other USAGE / REFUSED / SYSTEM / FATAL | 400 / 409 / 500 / 503 |

The body is `application/problem+json` with exactly `type`
(`urn:zxv:problem:<status name>`), a generic `title`, `status`, `code` and
`class`. It never has the exception message or the native `detail` (which
can name accounts, references or currencies); those go to the server log
(`Logger('Zxv')`: refusals at log level, SYSTEM at error, FATAL at fatal).

**Fatal.** JavaScript cannot make an error uncatchable, and the SDK's
`ZxvFatalError` deliberately does not extend `ZxvError`. After a FATAL
status every SDK call on the context throws `ZxvFatalError` without
reaching C; the filter answers each with 503, trips `ZxvFatalState` and
calls `onFatal` once. The health indicator finds the halted context on its
own as well. Use `fatalPolicy: 'abort'` when the process must stop at once.

**Health.** `isHealthy(key = 'zxv')` is UP only when libzxv answers that
the context is live; DOWN with `state: 'halted' | 'closed' | 'unknown'`
otherwise (Terminus then answers 503). UP data: `state`, `abi`, `backend`,
and per named capability its rights, attestation and revocation. No keys,
ledger data or halt reason.

**Exact JSON.** A JSON parser turns `0.1` into a binary double, and
`1.00000000000000001` into `1`, before any pipe runs, so a pipe alone
cannot tell. `exactJson()` scans the raw body first and answers 400
`urn:zxv:problem:json_float` for any number token with a fraction or
exponent, or an integer beyond 2^53, anywhere outside strings; malformed
JSON is 400 `json_syntax`, a body over the limit (default 100 KiB) is 413.
Then:

- `ParseRationalPipe` (one value) and `toRational(value, field)` accept a
  string in the SDK's exact syntax (`"12.75"`, `"-0.1"`, `"3/4"`, `"42"`),
  a safe integer, a `bigint` or a `Rational`; anything else is a 400 whose
  message names the field and never echoes native text.
- `ExactBodyPipe(schema)` validates a whole body: `rational`, `minorUnits`
  (a `bigint` from digits or a safe integer) and `string` fields, all
  required, unknown fields refused. `ExactBody<typeof pipe.schema>` is the
  typed result.
- `ExactJsonInterceptor` writes `bigint` (entry ids, balances) as exact
  decimal strings; `Rational` already serializes as its exact text.

## Build and test

```sh
make -C sdk/libzxv all fixtures
(cd sdk/typescript && npm ci && npm run build:ts)
cd sdk/frameworks/nest-zxv
npm ci
npm run typecheck     # strict TypeScript (exactOptionalPropertyTypes, noUncheckedIndexedAccess)
npm test              # 35 tests against the real libzxv
```

`node:test` with `@nestjs/testing` and `supertest`:

- `module.test` (9): providers and narrow capabilities, the root not
  injectable (no provider holds any capability but the named ones), eleven
  invalid option shapes, a currency libzxv refuses, `forRootAsync` (and a
  name mismatch), shutdown order observed on the handles, the context still
  usable in `onModuleDestroy`.
- `health.test` (4): UP with data, DOWN once halted (tripping the state and
  `onFatal` exactly once), DOWN once closed, a revoked capability reported.
- `exact.test` (10): rationals, minor units, `ExactBodyPipe`, the float
  scanner, bigint serialization.
- `filter.test` (4): every status class over HTTP with no secret text in
  any body, real native refusals, fatal answered 503, other exceptions
  untouched.
- `sample.test` (8): the sample end to end, including the halt.

## Honest limits

- **Fatal is catchable.** Code that catches every error before the filter
  sees it can swallow `ZxvFatalError`; the context stays halted in C
  regardless. Use `fatalPolicy: 'abort'` when the process must stop.
- **Capabilities are least privilege, not a sandbox.** Any code that can
  inject the context can create its own root capability.
- **One context per application.** `ZxvModule` registers exactly one, and
  the native addon loads one libzxv per process.
- **Start-up failures.** If another provider fails during `app.init()`
  after the context was created, Nest runs no shutdown hook; the process
  is expected to exit.
- **Platforms.** Built and tested on Linux x64 with Node 22; CI also runs
  macOS (arm64). Windows needs a `zxv.dll` build that does not exist yet.
- **Not published.** Use it from this repository; there is no npm release.
