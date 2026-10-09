# ZXV .NET SDK

A guide for .NET teams integrating with the ZXV platform core: what the SDK
is, how to build and configure it, how amounts and currencies work, how errors
and threading behave, and what is not included.

The SDK targets **.NET 8 (LTS)** and follows the Microsoft .NET design
guidelines: PascalCase public API, `IDisposable` for native resources,
`IOptions<T>` configuration, `ILogger<T>` logging, nullable reference types,
XML documentation on every public member, and `CancellationToken` on every
asynchronous or batch operation.

## Contents

1. [Layout](#layout)
2. [Install and build](#install-and-build)
3. [Quick start](#quick-start)
4. [Configuration](#configuration)
5. [Amounts and currencies](#amounts-and-currencies)
6. [The ledger](#the-ledger)
7. [Payments, messages and statements](#payments-messages-and-statements)
8. [Error model](#error-model)
9. [Threading, lifetime and async](#threading-lifetime-and-async)
10. [Features that are still being built](#features-that-are-still-being-built)
11. [The operator gateway sample](#the-operator-gateway-sample)
12. [The native C ABI](#the-native-c-abi)
13. [Honest limits](#honest-limits)
14. [Continuous integration](#continuous-integration)

## Layout

```
bindings/dotnet/
  Zxv.sln
  Directory.Build.props        shared settings (net8.0, nullable, analyzers, warnings as errors)
  Directory.Packages.props     every NuGet version, in one place
  native/
    zxv_api.h                  the stable C ABI (handles, status codes, UTF-8, caller-owned buffers)
    zxv_api.c                  adapter over the kernel modules; the only file that includes kernel headers
    test_zxv_api.c             C conformance test of the ABI
    Makefile                   builds native/build/libzxv.so (libzxv.dylib on macOS)
  src/Zxv.Native/              P/Invoke (LibraryImport source generator) and SafeHandle types
  src/Zxv.Sdk/                 the supported API: ZxvPlatform and its services
  tests/Zxv.Sdk.Tests/         xUnit tests against the real native library
  tests/Zxv.Gateway.Tests/     integration tests of the gateway (in-memory TestServer)
  samples/Zxv.Samples.Console/ a guided tour of the SDK
  samples/Zxv.Gateway/         operator REST gateway (ASP.NET Core minimal API)
api/openapi/zxv-operator.yaml  OpenAPI 3.1 contract of the gateway
```

The managed code never touches a kernel struct. Everything crosses the
boundary through `zxv_api.h`, so the kernel can change without breaking the
SDK.

## Install and build

Requirements:

- .NET 8 SDK (`dotnet --version` 8.0.100 or later). On Ubuntu 24.04:
  `sudo apt-get install -y dotnet-sdk-8.0`.
- A C11 compiler and `make` (gcc 11 or later, or clang) to build the native
  library. Linux and macOS are supported. Windows is not built yet: the
  Makefile has no MSVC path (see [Honest limits](#honest-limits)).
- Access to nuget.org (or a mirror) to restore packages.

Build and test:

```sh
cd bindings/dotnet
make -C native            # builds native/build/libzxv.so and checks the C ABI with `make -C native test`
dotnet build Zxv.sln -c Release
dotnet test  Zxv.sln -c Release
dotnet run --project samples/Zxv.Samples.Console
```

If you skip `make`, the first `dotnet build` runs it for you (Unix only). Set
`-p:ZxvSkipNativeBuild=true` to turn that off, for example when your CI
builds the native library in a separate job.

At run time the library is found next to the application (it is copied to
every output folder, and packed under `runtimes/<rid>/native/` in a NuGet
package). To use a library elsewhere, set `ZXV_NATIVE_LIBRARY` to its full
path.

NuGet packages used (versions in `Directory.Packages.props`):

| Project | Packages |
| --- | --- |
| Zxv.Sdk | Microsoft.Extensions.DependencyInjection.Abstractions, Logging.Abstractions, Options, Options.ConfigurationExtensions (8.0.x) |
| Zxv.Gateway | Microsoft.AspNetCore.OpenApi, Authentication.JwtBearer, Authentication.Certificate (8.0.31), Swashbuckle.AspNetCore 6.9.0 |
| tests | Microsoft.NET.Test.Sdk 17.12.0, xunit 2.9.3, xunit.runner.visualstudio 2.8.2, Microsoft.AspNetCore.Mvc.Testing 8.0.31, Microsoft.Extensions.TimeProvider.Testing 8.10.0, Microsoft.Extensions.Hosting 8.0.1 |

`Zxv.Native` has no package dependencies.

## Quick start

Without dependency injection:

```csharp
using Zxv;
using Zxv.Ledger;
using Zxv.Payments;

using ZxvPlatform zxv = ZxvPlatform.Create(o =>
{
    o.Currencies.Add("NGN");
    o.Currencies.Add("GHS");
});

var ngn = zxv.Currencies.GetEnabled("NGN");          // 566, 2 minor units, "Naira"

Account settlement = zxv.Ledger.OpenAccount("Lagos settlement NGN", "NGN", AccountKind.Issuer);
Account ada        = zxv.Ledger.OpenAccount("Adébáyọ̀ Okonkwo", "NGN");
Account chidi      = zxv.Ledger.OpenAccount("Chidi Eze", "NGN");

zxv.Ledger.Post(new Transfer(settlement.Id, ada.Id, Money.FromDecimal(150_000.75m, ngn), "FUND-0001"));

PaymentReceipt receipt = await zxv.Payments.SubmitAsync(
    new PaymentRequest(ada.Id, chidi.Id, Money.FromDecimal(25_000.50m, ngn)),
    cancellationToken);

Console.WriteLine(receipt.Message.Xml);              // pacs.008.001.09, Ccy="NGN" 25000.50
Console.WriteLine(zxv.Ledger.GetAvailable(ada.Id));  // NGN 125000.25
```

With the generic host or ASP.NET Core:

```csharp
builder.Services.AddZxv(builder.Configuration.GetSection(ZxvOptions.SectionName));
// optional: your scheme transport (see "Payments")
builder.Services.AddSingleton<IMxTransport, MyPapssTransport>();

// later, anywhere:
public sealed class Payouts(ZxvPlatform zxv) { /* ... */ }
```

`ZxvPlatform` is registered as a singleton. Options are validated at start
(`ValidateOnStart`), so a bad currency code fails the host, not the first
request.

## Configuration

```json
"Zxv": {
  "Currencies": [ "NGN", "GHS", "KES", "XOF", "XAF", "ZAR", "EGP" ],
  "MinorUnits": { },
  "PrivateUnits": [ { "Code": "VFV", "MinorUnits": 2 } ],
  "IdentifierPrefix": "ZXV"
}
```

| Option | Meaning |
| --- | --- |
| `Currencies` | ISO 4217 alpha codes to enable for ledger accounts. Minor units come from the ISO 4217 table compiled into the native library. Unknown or non-payable codes (`XXX`, `XAU`) fail start-up. |
| `MinorUnits` | Optional `code -> decimal places`. Asserted against ISO 4217 (a mismatch fails start-up). Required only on a native build without the ISO 4217 table. |
| `PrivateUnits` | Platform-private units such as VFV. They can be held on the ledger; they are refused in every ISO 20022 message. |
| `IdentifierPrefix` | 1 to 8 of `[A-Z0-9]`. Prefix of generated MsgId and EndToEndId values and of external account identifiers (`ZXV-000042`). |

`ZxvPlatform.Create(Action<ZxvOptions>)` and `AddZxv(Action<ZxvOptions>)`
configure the same options in code; prefer them in trimmed or Native AOT apps,
because binding from `IConfiguration` uses reflection.

## Amounts and currencies

**Exact minor units, always.** An amount is `Money(long MinorUnits, string
Currency, byte Scale)`: NGN 2,500.50 is `MinorUnits = 250050, Scale = 2`;
XOF 5,000 is `5000, 0`; BHD 1.005 is `1005, 3`. The platform stores and
computes only the integer.

- `Money.FromDecimal(2500.50m, ngn)` converts **exactly** and throws
  `ArgumentException` if the value has more decimal places than the currency
  allows. The SDK never rounds; rounding is a business decision you make
  explicitly before calling it.
- `Money.ToDecimal()` and `ToString()` are for display only.
- `decimal` is never used for arithmetic inside the platform, and `double`
  is never used anywhere.
- Every amount and balance is bounded to ±(2^53 - 1) minor units
  (`Money.MaxMinorUnits`, about 90 trillion naira). That keeps values exact in
  every kernel number representation and in every JSON parser, including
  JavaScript's.
- Arithmetic between currencies throws. There is no implicit FX anywhere:
  a posting between an NGN account and a GHS account is refused.

**ISO 4217 alpha codes on the wire.** Every ISO 20022 `Ccy` attribute carries
an ISO 4217 alpha code (`NGN`, `GHS`, `XOF`). The native serializer checks the
code against the compiled ISO 4217 table (SIX list one; see
`Currencies.Iso4217PublicationDate`) and checks that the amount's scale equals
the currency's ISO 4217 minor unit. `Currencies.GetIso`, `GetIsoByNumeric`,
`ListIso` and `IsAfricanUnionMember` expose the reference data.

**Vino rail numerics only in proprietary fields.** The ledger keeps every
account on three rails with platform-internal numeric codes:

| Rail | Numeric | Meaning |
| --- | --- | --- |
| DEBIT | 846 | what the account has received (asset / backing) |
| CREDIT | 810 | what the account has sent (claim / liability) |
| EQUITY | 888 | DEBIT minus CREDIT: the spendable position |

None of these is an active ISO 4217 currency: 846 and 888 are unassigned and
810 is the **withdrawn** code of the old Russian ruble (RUR); a legacy parser
may read it as RUR. They therefore never appear in a `Ccy` attribute; they
appear in `RailBalances` and in the gateway's `railNumerics` field.
`Currencies.GetCaveat(810)` returns the explanation. Private units such as
VFV follow the same rule.

## The ledger

- `Ledger.OpenAccount(name, currency, kind)` opens an account in an enabled
  currency. `AccountKind.Holder` (the default) can never go below zero: there
  is no debt for holders. `AccountKind.Issuer` (a settlement, issuance or
  nostro-mirror account) may carry a CREDIT balance: the value it has put
  into circulation on the platform.
- `Ledger.Post(new Transfer(from, to, amount, reference))` is atomic. The
  native core validates funds, currency, range and reference uniqueness and
  reserves every resource before it writes anything; a refused posting leaves
  no trace. Each posting is booked on the three axes of the kernel triple
  ledger (financial, provenance, externality).
- `reference` (at most 35 bytes) must be unique per platform; for payments it
  is the EndToEndId. A reused reference is `ZxvStatus.Duplicate`.
- `GetBalances`, `GetAvailable`, `GetEntries` and `ListAccounts` read the
  ledger; `Verify()` (or `TryVerify`) checks the invariants: per currency the
  DEBIT and CREDIT rails balance, no holder is negative, and the exact
  minor-unit balances equal the kernel triple ledger's financial axis.
- Names and references are UTF-8 and limits are in **bytes** (a name is at
  most 63 bytes: "Adébáyọ̀" uses more bytes than letters).

## Payments, messages and statements

`Payments.SubmitAsync(PaymentRequest, CancellationToken)`:

1. builds and validates the pacs.008.001.09 (nothing is booked if the message
   is invalid, for example a VFV amount);
2. posts the transfer on the ledger, atomically;
3. awaits `IMxTransport.SendAsync`.

If the transport fails after booking, `ZxvTransportException` carries the
`PaymentReceipt`; the booking stands and `RetransmitAsync(receipt)` sends the
same message again. For internal transfers that produce no wire message
(including private units), call `Ledger.Post` directly.

`IMxTransport` is the seam for network connectivity. The SDK ships
`NullMxTransport` (the default; accepts and discards) and
`DirectoryMxTransport` (writes `<MsgId>.xml` files into a drop folder, never
overwriting). A PAPSS, SWIFT or RTGS transport is provided through scheme
onboarding and registered in DI.

`Messages.BuildPacs008` and `BuildCamt053` serialize directly when you have
your own data. Output is deterministic and XML-escaped. It is **structurally
well-formed, not schema-validated**: validate against the published XSDs and
the scheme's usage guidelines before production use.

`Statements.BuildStatement(account, new StatementRequest { Start = 0 })`
returns one camt.053 page of up to 32 entries with `NextStart` for paging.
The opening balance is recomputed from the journal and the native builder
refuses any page where opening plus booked entries is not the closing
balance.

`TimeProvider` is injectable (`services.AddSingleton<TimeProvider>(...)`), so
tests can fix `CreDtTm` and generated identifiers.

## Error model

One rule: **a refused operation throws; a card decline is a result.**

| Exception | When |
| --- | --- |
| `ArgumentException` family | Detected in managed code before any native call: null, empty, embedded NUL, out-of-range page sizes, inexact `FromDecimal`. |
| `ZxvException` | The native core refused the operation. Branch on `Status` (`ZxvStatus`); `Code` is the stable symbolic name (`ZXV_E_FUNDS`); `Detail` is diagnostic text only. |
| `ZxvFeatureNotAvailableException` (a `ZxvException`) | The feature exists in the API but not in the loaded native library yet (`ZxvStatus.NotImplemented`). |
| `ZxvTransportException` (a `ZxvException`) | A payment was booked but the transport failed; `Receipt` is set. |
| `ObjectDisposedException` | The platform was disposed. |
| `OperationCanceledException` | The token was cancelled (before booking, or during the send). |

Common statuses and the gateway's HTTP mapping:

| `ZxvStatus` | Code | Meaning | HTTP |
| --- | --- | --- | --- |
| `InvalidArgument`, `AmountOutOfRange`, `InvalidUtf8`, `TooLong`, `InvalidField` | `ZXV_E_ARG` ... | bad input | 400 |
| `NotFound` | `ZXV_E_NOT_FOUND` | unknown account, currency, card | 404 |
| `Duplicate` | `ZXV_E_DUPLICATE` | reference / EndToEndId reused | 409 |
| `InsufficientFunds` | `ZXV_E_FUNDS` | a holder would go below zero | 422 |
| `InvalidCurrency` | `ZXV_E_CURRENCY` | not ISO 4217, not enabled, private unit on the wire, scale mismatch, cross-currency | 422 |
| `InvariantViolated` | `ZXV_E_INVARIANT` | e.g. a camt.053 page that does not reconcile | 422 |
| `NotImplemented` | `ZXV_E_NOTIMPL` | feature not built in yet | 501 |
| `CapacityExceeded`, `OutOfMemory` | | resource limits | 503 |

The gateway returns RFC 9457 problem details with `type`
`urn:zxv:problem:<code>`, `code` and `operation`.

## Threading, lifetime and async

- `ZxvPlatform` and all its services are **thread-safe**. The native context
  serializes calls with a mutex; every posting is atomic.
- Register `ZxvPlatform` as a **singleton** and dispose it at shutdown (the
  host does this). `Dispose` is idempotent; calls already in flight on other
  threads finish safely (the `SafeHandle` reference count keeps the native
  object alive until the last call returns); later calls throw
  `ObjectDisposedException`.
- A `NettingCycle` is `IDisposable` and **not** thread-safe; use one from one
  thread at a time, or lock it (the gateway does).
- **Why most calls are synchronous.** Ledger, currency and message calls run
  in-process and finish in microseconds; there is no I/O to await, and
  wrapping them in `Task.Run` would only add overhead (Microsoft guidance:
  do not expose async wrappers for synchronous work). Operations that do I/O
  are asynchronous and take a `CancellationToken`: `Payments.SubmitAsync`,
  `RetransmitAsync` and `IMxTransport.SendAsync`. Batch methods
  (`Ledger.PostMany`) take a token checked between items.
- The SDK logs through `ILogger<ZxvPlatform>` with source-generated
  `[LoggerMessage]` methods and stable event ids (1000 start-up, 1100 ledger,
  1200 messages, 1300 payments). Refused postings are Warning; nothing logs
  account names or full message bodies.

## Features that are still being built

The kernel modules for some features are in development. Their API is
present now, and the native library reports what is live:

```csharp
if (zxv.Supports(ZxvFeatures.Netting)) { /* ... */ }
```

| Feature | Status in this build | Kernel module |
| --- | --- | --- |
| Ledger, pacs.008, camt.053 | live | iso20022, finance/triple_ledger, vino_stores |
| ISO 4217 table, AU membership | live | cbank/cb_ccy |
| PAN check digits (Damm, Luhn, Verhoeff) | live | cardnet/cn_check |
| pacs.002, pacs.004, pacs.009, camt.056, camt.029 | pending, `ZxvFeatureNotAvailableException` | cbank/cb_mx |
| Netting cycles | pending | cbank/cb_net |
| Card issue, reissue, authorize | pending | cardnet |
| VSS conformance checks | pending | cbank/cb_vss |

When those modules land, only `native/zxv_api.c` changes; the C ABI, the SDK
API and the gateway contract stay the same.

## The operator gateway sample

```sh
cd bindings/dotnet
ASPNETCORE_ENVIRONMENT=Development dotnet run --project samples/Zxv.Gateway
# http://localhost:5080/openapi/v1.json   generated OpenAPI 3.0
# http://localhost:5080/swagger           Swagger UI (Development only)
# http://localhost:5080/health/ready      ledger invariants + native core
```

```sh
curl -s -X POST localhost:5080/v1/accounts -H 'Content-Type: application/json' \
     -d '{"name":"Lagos settlement","currency":"NGN","kind":"Issuer"}'
curl -s -X POST localhost:5080/v1/payments -H 'Content-Type: application/json' \
     -d '{"fromAccount":0,"toAccount":1,"amount":{"currency":"NGN","minorUnits":250050}}'
```

Endpoints (all under `/v1`, all requiring the `operator` policy): platform,
currencies, accounts, balances, entries, statements (camt.053), payments
(pacs.008, retransmit), netting cycles, cards and conformance. The contract is
`api/openapi/zxv-operator.yaml` (OpenAPI 3.1); `Zxv.Gateway.Tests` fails if the
served paths and the document disagree.

**Authentication** is a placeholder you configure; the sample ships no keys,
no certificates and no secrets.

- `Gateway:Authentication:Mode = Oidc` (the default). Set
  `Oidc:Authority` to your identity provider's issuer URL and
  `Oidc:Audience` (default `zxv-operator-api`). Tokens must carry scope
  `zxv.operator` (`scope` or `scp` claim). Metadata must be served over HTTPS.

  ```json
  "Gateway": { "Authentication": { "Mode": "Oidc",
    "Oidc": { "Authority": "https://idp.your-bank.example/realms/operators", "Audience": "zxv-operator-api" } } }
  ```

- `Mode = Mtls`. Client certificates must chain to a CA trusted by the host,
  be within validity, and pass online revocation checking
  (`Mtls:RevocationCheck`). Optionally pin with `Mtls:AllowedThumbprints`
  (SHA-1 or SHA-256). Kestrel must request the certificate:

  ```json
  "Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:8443",
    "ClientCertificateMode": "RequireCertificate",
    "Certificate": { "Path": "/run/secrets/gateway.pfx", "Password": "<from your secret store>" } } } },
  "Gateway": { "Authentication": { "Mode": "Mtls", "Mtls": { "AllowedThumbprints": [ "<sha256>" ] } } }
  ```

  Behind a TLS-terminating proxy, forward the client certificate with
  ASP.NET Core's certificate forwarding middleware instead.

- `Mode = Development` authenticates every request as a local operator. The
  gateway **refuses to start** with it unless `ASPNETCORE_ENVIRONMENT` is
  `Development`.

Other settings: `Gateway:Transport:Directory` switches the transport to a
file drop. Payments and netting cycles are kept in memory by the sample; a
production gateway persists receipts in its own store.

## The native C ABI

`bindings/dotnet/native/zxv_api.h` is usable from any language with a C FFI
(Java/Panama, Python ctypes, Go cgo, Rust). Its rules, in short:

- opaque handles only (`zxv_ctx`, `zxv_msg`, `zxv_netting`); no kernel struct
  crosses the ABI;
- fixed-width integers and NUL-terminated UTF-8; amounts are `int64_t` minor
  units; no floats;
- every function returns `ZXV_OK` (0) or a negative `ZXV_E_*`; out
  parameters are written only on success;
- text out uses caller-owned buffers with a two-call pattern: `*out_len`
  always reports the needed length, and a short buffer returns `ZXV_E_BUFFER`;
- a context is internally serialized; message and netting handles are
  single-threaded; a child handle is destroyed before its context;
- `zxv_last_error` gives thread-local diagnostic text; `zxv_features` reports
  the live feature groups; ABI major version 1, additive minor versions.

Build details: the shim compiles the kernel modules with `-DTEST_HOST` (the
hosted representation, where the kernel's `surplus_real_t` is an IEEE
double). Exactness does not depend on that double: the ABI bounds every value
to 2^53 - 1, the adapter keeps an exact `int64_t` shadow of every balance, and
`zxv_ledger_check` verifies the two agree. The kernel triple ledger is
fixed-size (512 accounts per book, 256 entries per account); the adapter
chains books and segments so platform journals are unbounded. Only `zxv_*`
symbols are exported. The C test (`make -C native test`) passes under
AddressSanitizer and UndefinedBehaviorSanitizer.

## Honest limits

- **No network connectivity.** No PAPSS, SWIFT, CIPS or RTGS connectivity is
  included. Connectivity, certification and message-level usage guidelines
  come through scheme onboarding; plug the resulting transport into
  `IMxTransport`.
- **Not certified.** Structural well-formedness is not schema validation and
  is not scheme certification. A VSS conformance level, when available, is a
  self-assessment.
- **In-memory state.** A `ZxvPlatform` holds its ledger in process memory.
  There is no persistence, replication or recovery yet; restart means an
  empty ledger. Do not use it for real money in this form.
- **Licensing and regulation.** Operating a ledger for other people's money
  requires the licences of your jurisdiction; nothing here substitutes for
  them.
- **Pending modules.** Netting, card lifecycle, VSS checks and the
  pacs.002/004/009 and camt.056/029 messages are pending (see the table
  above).
- **Platforms.** Linux x64 is built and tested. macOS should build with the
  same Makefile but is untested here. Windows needs an MSVC or clang-cl build
  of the shim, which does not exist yet.
- **ISO 4217 currency.** The table is as current as the SIX list it was
  generated from (`Iso4217PublicationDate`); regenerate on each amendment. AU
  membership is as of its compiled date and does not record suspensions.

## Continuous integration

A GitHub Actions job that builds the shim, runs the C ABI test, builds the
solution with warnings as errors, and runs every .NET test:

```yaml
  dotnet-sdk:
    runs-on: ubuntu-24.04
    defaults:
      run:
        working-directory: bindings/dotnet
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-dotnet@v4
        with:
          dotnet-version: '8.0.x'
      - name: Native shim (C ABI) and its conformance test
        run: make -C native test
      - name: Build (Release, warnings as errors)
        run: dotnet build Zxv.sln -c Release -p:ZxvSkipNativeBuild=true
      - name: Test
        run: dotnet test Zxv.sln -c Release --no-build --logger "trx;LogFileName=results.trx"
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: dotnet-test-results
          path: bindings/dotnet/tests/**/TestResults/*.trx
```
