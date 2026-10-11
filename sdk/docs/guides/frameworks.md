<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Web services with Spring Boot and ASP.NET Core

**Who this is for:** enterprise teams putting ZXV behind an HTTP API.
**What you will be able to do:** declare narrow capabilities in
configuration, inject them into a service, map ZXV errors to HTTP answers,
and make a halted context take the service out of rotation.

Both kits follow the same pattern, and so do the
[NestJS](../../frameworks/nest-zxv/) and
[Django](../../frameworks/django-zxv/) kits:

1. The framework owns **one context**, created at start-up and closed at
   shutdown in the right order.
2. You declare **named capabilities** in configuration. Your code asks for
   them by name. The root capability is **never** injectable.
3. A **health check** reports the service as down when the context is
   halted.
4. `Rational` travels in JSON as an exact **string** (`"0.1"`, `"1/3"`). A
   JSON float such as `0.1` is refused (with a 400 in the Spring and ASP.NET
   Core kits).
5. A halted context is **never** answered as a normal response.

The examples below come from each kit's sample application.

## Spring Boot 3

Add the starter (see the [Spring kit README](../../frameworks/spring-zxv/)
for installing it locally):

```xml
<dependency>
  <groupId>org.zedec</groupId>
  <artifactId>zxv-spring-boot-starter</artifactId>
  <version>1.1.0</version>
</dependency>
```

Declare the capabilities in `application.yml`:

```yaml
zxv:
  currencies: [NGN]
  # The root capability only needs what the named ones use.
  root-permissions: [LEDGER_READ, LEDGER_POST]
  capabilities:
    teller:
      permissions: [LEDGER_READ, LEDGER_POST]
    auditor:
      permissions: [LEDGER_READ]
```

Inject them with `@Qualifier`:

```java
@Service
public class Treasury {
    private final Ledger teller;
    private final Ledger auditor;
    private final Account bank;
    private final Account customer;

    public Treasury(@Qualifier("teller") Capability teller, @Qualifier("auditor") Capability auditor) {
        this.teller = Ledger.of(teller);
        this.auditor = Ledger.of(auditor);
        this.bank = this.teller.openAccount("bank", "NGN", AccountKind.ISSUER);
        this.customer = this.teller.openAccount("ada", "NGN", AccountKind.HOLDER);
    }

    public long payCustomer(long amountMinor, String reference) {
        return teller.post(bank, customer, amountMinor, "NGN", reference);
    }

    public Balance customerBalance() {
        return auditor.balance(customer);
    }
}
```

Map refusals to HTTP in a `@RestControllerAdvice`, by type:
`CapabilityDeniedException` → 403, `InsufficientFundsException` → 422, any
other `ZxvRefusedException` → 409, `ZxvUsageException` → 400. Send only the
status name (`e.statusName()`) to the client, never `e.detail()`, which can
name accounts and references. The sample's `ApiErrors` class does exactly
this.

**When the context halts:** the starter's `ZxvFatalExceptionResolver` runs
before every `@ExceptionHandler`, so even a catch-all
`@ExceptionHandler(Exception.class)` never sees a `ZxvFatalError`. The
request ends in a 500, the Actuator health indicator `zxv` goes DOWN,
liveness goes BROKEN and readiness REFUSING_TRAFFIC. For a process that must
stop at once, set `zxv.fatal-policy: abort`.

## ASP.NET Core 8

Configure in `appsettings.json`:

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

Register the services and middleware in `Program.cs`:

```csharp
builder.Services.AddZxv(o => o.ReadFrom(builder.Configuration.GetSection(ZxvAspNetCoreOptions.SectionName)));
builder.Services.AddHealthChecks().AddZxv(tags: "ready");
builder.Services.AddSingleton<Treasury>();

WebApplication app = builder.Build();
app.UseExceptionHandler();
app.UseZxvProblemDetails();
app.MapPost("/payments", (Payment p, Treasury t) => new Posted(t.PayCustomer(p.AmountMinor, p.Reference)));
app.MapGet("/balance", (Treasury t) => t.CustomerBalance());
app.MapHealthChecks("/health/ready", new HealthCheckOptions { Predicate = c => c.Tags.Contains("ready") });
```

Inject capabilities with `[FromKeyedServices]`:

```csharp
public Treasury(ZxvPlatform zxv, [FromKeyedServices("teller")] Capability teller, [FromKeyedServices("auditor")] Capability auditor)
{
    _teller = zxv.Ledger.WithCapability(teller);
    _auditor = zxv.Ledger.WithCapability(auditor);
    _ngn = zxv.Currencies.GetEnabled("NGN");
    _bank = _teller.OpenAccount("bank", "NGN", AccountKind.Issuer);
    _customer = _teller.OpenAccount("ada", "NGN", AccountKind.Holder);
}
```

`UseZxvProblemDetails` turns ZXV exceptions into RFC 9457 **problem
details** (a standard JSON error body). For example, `ZXV_E_CAP` → 403,
`ZXV_E_FUNDS` → 422, `ZXV_E_BUDGET` → 429. The body carries the status name,
never the native detail text.

**When the context halts:** the next ZXV call answers 503, and
`/health/ready` turns Unhealthy, so a load balancer stops sending traffic.
Set `StopApplicationOnFatal` to stop the host so an orchestrator restarts
it, or `ZxvLibrary.FatalPolicy = FatalPolicy.FailFast` to end the process at
once.

## Try the fatal path

Both samples have a demo-only `POST /admin/halt` endpoint, off by default
(`--sample.halt-endpoint.enabled=true` for Spring,
`--Sample:HaltEndpoint=true` for ASP.NET Core). It halts the context so you
can watch the service fail closed. It is unauthenticated: never enable it
outside a local demo.

## Honest limits

- Capabilities here are least privilege **inside one process**. They keep
  the teller and the auditor apart in your code; they are not a sandbox.
  See [Capabilities](capabilities.md#what-capabilities-do-not-do).
- Code that catches `Throwable` (Java) or every exception (C#) outside the
  framework's pipeline can still swallow the fatal error. The context stays
  halted in C regardless, and the health check still reports it. Use the
  abort policy if the process must die.
- The kits do not add authentication or authorisation for your HTTP
  endpoints. That is still your framework's job.

## Next

- Kit READMEs: [Spring Boot](../../frameworks/spring-zxv/),
  [ASP.NET Core](../../frameworks/aspnetcore-zxv/),
  [all framework kits](../../frameworks/).
- [Errors and fail-closed](errors-and-fail-closed.md)
