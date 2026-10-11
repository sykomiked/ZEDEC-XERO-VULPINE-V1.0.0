<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Zxv.AspNetCore

ASP.NET Core integration over the [ZXV .NET SDK](../../dotnet/) (`Zxv.Sdk`).
One call, `services.AddZxv(...)`, gives your application:

- one hosted `ZxvPlatform`, created when the host starts (so a bad
  configuration or a missing `libzxv` fails start-up, not the first request)
  and disposed in the destroy order of [ABI.md R6](../../docs/ABI.md#r6-threading);
- narrow `Capability` singletons declared in configuration and injected with
  `[FromKeyedServices("teller")]`; the root capability is never injectable;
- System.Text.Json support for `Rational` as an exact string (`"0.1"`,
  `"1/3"`) that refuses JSON floats, for minimal APIs and MVC;
- problem-details services.

Two more pieces you add explicitly:

- `services.AddHealthChecks().AddZxv()`: a health check that is Unhealthy
  when the context is halted (fail-closed);
- `app.UseZxvProblemDetails()`: middleware that maps ZXV exceptions to RFC
  9457 problem details by status class, without the native detail text.

Requirements: the .NET 8 SDK and `libzxv` built for your platform.

## Quickstart

```sh
make -C sdk/libzxv all                       # builds sdk/libzxv/build/libzxv.so
cd sdk/frameworks/aspnetcore-zxv
dotnet run --project samples/Zxv.AspNetCore.Sample
```

Reference `src/Zxv.AspNetCore/Zxv.AspNetCore.csproj` from your web project
(it references `Zxv.Sdk`, which copies `libzxv` next to your output). Then:

```json
"Zxv": {
  "Currencies": [ "NGN" ],
  "RootRights": "LedgerRead, LedgerPost",
  "Capabilities": {
    "teller": "LedgerRead, LedgerPost",
    "auditor": "LedgerRead"
  }
}
```

```csharp
using Zxv;
using Zxv.AspNetCore;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
builder.Services.AddZxv(o => o.ReadFrom(builder.Configuration.GetSection("Zxv")));
builder.Services.AddHealthChecks().AddZxv(tags: "ready");

WebApplication app = builder.Build();
app.UseZxvProblemDetails();

app.MapGet("/sum", (Rational a, Rational b) => new { sum = a + b });          // ?a=0.1&b=0.2 -> {"sum":"0.3"}
app.MapPost("/pay", ([FromKeyedServices("teller")] Capability teller, ZxvPlatform zxv, Payment p) =>
    zxv.Ledger.WithCapability(teller).Post(/* ... */));
app.MapHealthChecks("/health/ready", new HealthCheckOptions { Predicate = c => c.Tags.Contains("ready") });
app.Run();
```

Or configure in code:

```csharp
builder.Services.AddZxv(o =>
{
    o.Platform.Currencies.Add("NGN");
    o.RootRights = CapabilityRights.LedgerRead | CapabilityRights.LedgerPost;
    o.Capabilities["teller"] = CapabilityRights.LedgerRead | CapabilityRights.LedgerPost;
});
```

### The sample

[`samples/Zxv.AspNetCore.Sample`](samples/Zxv.AspNetCore.Sample/) is a
minimal API: exact quotes, a ledger posted through `teller` and read through
`auditor`, health probes and problem details. It listens on
`http://localhost:5080`.

```sh
curl -s localhost:5080/quote -H 'content-type: application/json' \
     -d '{"price":"12.75","quantity":"1/3"}'          # {"total":"4.25"}
curl -s localhost:5080/quote -H 'content-type: application/json' \
     -d '{"price":12.75,"quantity":"1"}'              # 400: a JSON float
curl -s 'localhost:5080/sum?a=0.1&b=0.2'              # {"total":"0.3"}
curl -s localhost:5080/payments -H 'content-type: application/json' \
     -d '{"amountMinor":15000,"reference":"E2E-1"}'   # posted with teller
curl -s localhost:5080/refunds -H 'content-type: application/json' \
     -d '{"amountMinor":100,"reference":"E2E-2"}'     # 403 problem: auditor cannot post
curl -s localhost:5080/health/ready                   # Healthy
```

The refusal looks like this (no account, reference or native detail text):

```json
{"type":"urn:zxv:problem:zxv_e_cap","title":"Operation refused","status":403,
 "code":"ZXV_E_CAP","class":"Refused","traceId":"0HN..."}
```

Run it with `--Sample:HaltEndpoint=true` to get `POST /admin/halt`, which
halts the context: the next ZXV call answers 503 and `/health/ready` turns
Unhealthy. Never enable it outside a local demo.

## Configuration

`ZxvAspNetCoreOptions`, read from the `Zxv` section by `ReadFrom` (no
reflection, so trimming and AOT are fine) or set in code:

| Key | Property | Default | Meaning |
|---|---|---|---|
| `Currencies`, `MinorUnits`, `PrivateUnits`, `IdentifierPrefix` | `Platform` (`Zxv.Sdk`'s `ZxvOptions`) | none, `ZXV` | Currencies enabled and units registered when the platform is created. |
| `RootRights` | `RootRights` | `All` | Rights of the root capability. Every named capability must be a subset. |
| `Capabilities:<name>` | `Capabilities[name]` | none | One keyed `Capability` singleton per name, e.g. `"LedgerRead, LedgerPost"`. |
| `StopApplicationOnFatal` | `StopApplicationOnFatal` | `false` | Stop the host when the context halts, so an orchestrator restarts it. |

Rights are names only (`LedgerRead`, `LedgerPost`, `CryptoSign`,
`CryptoKem`, `AiInference`, `Budget`, `Phase`, `Attest`, `Swarm`, `Evm`,
`All`), comma-separated and case-insensitive; numbers are refused.

`AddZxv` runs your configure delegate once, at registration, so it knows
the capability names to register; it then validates and freezes the
options. Misconfiguration throws there (`OptionsValidationException`, or
`FormatException` for a malformed right): a capability with no rights, one
asking for a right the root does not hold, a bad name, an invalid currency
code. A currency the native ISO 4217 table does not know fails at start-up.

This is not `Zxv.Sdk`'s `AddZxv(Action<ZxvOptions>)`, which registers only
the platform. Calling both throws `InvalidOperationException`. If you write
`services.AddZxv(o => o.Currencies.Add("NGN"))` you are calling the SDK's
method (the property here is `o.Platform.Currencies`).

## How it behaves

**Services.**

| Service | Lifetime | Injectable |
|---|---|---|
| `ZxvPlatform` | singleton | yes |
| root `Capability` | keyed singleton (private key) | **no** |
| `Capability` keyed `"<name>"` | keyed singleton | `[FromKeyedServices("<name>")]` |
| `ZxvCapabilities` | singleton | yes: `Names`, `Get(name)` (never the root) |
| `ZxvFatalState` | singleton | yes: `IsTripped` |
| `IOptions<ZxvAspNetCoreOptions>` | singleton | yes |

**Disposal.** The container disposes singletons in reverse order of
creation. The hosted service creates the platform, then the root (from the
platform), then each named capability (from the root), so on shutdown the
named capabilities go first, then the root, then the platform: ABI.md R6.
(`Zxv.Sdk` also keeps a parent's native handle alive while a child holds it,
so even a wrong order would not free memory under a call.)

**Fatal errors.** .NET cannot make an exception uncatchable, and
`ZxvFatalException` deliberately does not derive from `ZxvException`. The
middleware handles it separately: it logs at Critical, trips
`ZxvFatalState`, stops the host if `StopApplicationOnFatal` is set, and
answers 503 `urn:zxv:problem:zxv_e_poisoned`. The health check finds the
halted context on its own as well. For a process that must die at once, set
`ZxvLibrary.FatalPolicy = FatalPolicy.FailFast`.

**Problem details.** One table, `ZxvProblemMapping.HttpStatusFor`:

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

The body has `type` (`urn:zxv:problem:<status name>`), a generic `title`,
`status`, and the extensions `code`, `class` and `traceId`. It never has the
exception message, the native `Detail` (which can name accounts, references
or currencies), the SDK operation or a stack trace; those go to the server
log only (refusals at Information without a stack, SYSTEM at Error). The
exception is not passed to `IProblemDetailsService`, so a
`CustomizeProblemDetails` hook cannot copy it into the response either.
A `BadHttpRequestException` (a body or parameter that does not bind, such as
a JSON float) is answered with its own status, 400, and no message: in .NET 8
`UseExceptionHandler` would otherwise turn it into a 500 when minimal APIs
throw on bad requests (the Development default). Other exceptions pass
through untouched.

**Health.** `AddZxv()` on `IHealthChecksBuilder` adds a check named `zxv`.
Healthy only when libzxv answers that the context is live; Unhealthy when
it is halted, disposed, or does not answer. Data: `state`, `abi`,
`features`, and `capability:<name>` with rights, attestation and revocation.
No keys, ledger data or halt reason.

**JSON.** `RationalJsonConverter` writes the exact string (`"12.75"`,
`"1/3"`), never a number. It reads a string in the SDK's exact syntax
(`"12.75"`, `"-0.1"`, `"3/4"`, `"42"`) or a JSON integer that fits in 64
bits. A JSON number with a fraction or exponent (`0.1`, `1.0`, `1e3`) is
always a `JsonException` (400), even with
`JsonNumberHandling.AllowReadingFromString`. `Rational?` works.
(System.Text.Json already refuses `150.7` for a `long`, so integer amounts
need nothing extra.)
Outside ASP.NET Core: `new JsonSerializerOptions { Converters = { new RationalJsonConverter() } }`.

**Query and route values.** Minimal APIs bind `Rational` through its
`TryParse`: `?a=0.1` is exactly 1/10, `?a=1e-1` is a 400.

## Build and test

```sh
make -C sdk/libzxv all fixtures
cd sdk/frameworks/aspnetcore-zxv
dotnet build Zxv.AspNetCore.sln -c Release -p:ZxvSkipNativeBuild=true   # warnings are errors
dotnet test  Zxv.AspNetCore.sln -c Release --no-build                   # 56 tests
```

The build imports `sdk/dotnet/Directory.Build.props` and
`Directory.Packages.props` (net8.0, nullable, `latest-recommended`
analyzers, central package versions). The tests run against the real
`libzxv`:

- `HostingTests` (15): start-up with narrow keyed capabilities (a ledger
  post through `teller`, a refused post through `auditor`), the root not
  injectable, reading configuration, malformed rights, every validation
  failure, a second registration, an unknown currency failing start-up,
  disposal (and its order, observed through sentinels in the container's
  disposal list), health Healthy then Unhealthy after `Halt`, Unhealthy when
  disposed, and `StopApplicationOnFatal`.
- `JsonTests` (20): exact output, exact input, six float forms refused, twelve
  malformed or inexact inputs refused.
- `MiddlewareTests` (11): every status class over a test server, no secret
  text in any body, fatal answered 503 and tripping the state, a JSON float
  in a body answered 400 without a message, other exceptions untouched.
- `SampleTests` and `SampleFatalTests` (10): the sample through
  `WebApplicationFactory`, including the halt.

## Honest limits

- **Fatal is catchable.** Code that catches `Exception` before the
  middleware sees it can still swallow `ZxvFatalException`; the context stays
  halted in C regardless. Use `FatalPolicy.FailFast` when the process must
  stop at once.
- **Capabilities are least privilege, not a sandbox.** Any code that can
  resolve `ZxvPlatform` can create its own root capability.
- **One platform per container.** `AddZxv` registers exactly one.
- **Platforms.** Built and tested on Linux x64 with .NET 8; CI also runs macOS (arm64).
  Windows needs a `zxv.dll` build that does not exist yet.
- **Not published.** Reference the project; there is no NuGet release yet.
