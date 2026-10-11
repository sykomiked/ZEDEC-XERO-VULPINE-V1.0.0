<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Spring Boot

A Spring Boot 3 starter over the [ZXV SDK for the JVM](../../java/)
(`org.zedec:zxv`, Java 22 FFM). Add one dependency and your application
gets:

- one managed `ZxvContext`, closed on shutdown after everything made from it
  (the destroy order of [ABI.md R6](../../docs/ABI.md#r6-threading));
- narrow `Capability` beans declared in `application.yml`, injected with
  `@Qualifier("teller")`; the all-powerful root capability is never
  injectable;
- an Actuator health indicator, `zxv`, that is DOWN when the context is
  poisoned (fail-closed);
- `ZxvFatalError` kept out of Spring MVC's exception handling: liveness
  goes BROKEN, readiness REFUSING_TRAFFIC, and the request ends in a 500 even
  if the application has an `@ExceptionHandler(Exception.class)`;
- Jackson support for `Rational` as an exact string (`"0.1"`, `"1/3"`) that
  refuses JSON floats, and exact `Rational` request parameters.

No Micrometer dependency, no web or Actuator dependency forced on you: each
integration switches on only when the application has that library.

Requirements: Java 22 or later (CI runs 22 and 25), Spring Boot 3.5, and
`libzxv` built for your platform.

## Quickstart

1. Build the C library and install the Java SDK and the starter into your
   local Maven repository:

   ```sh
   make -C sdk/libzxv all
   mvn -f sdk/java/pom.xml install -DskipTests
   mvn -f sdk/frameworks/spring-zxv/pom.xml install -DskipTests
   ```

2. Add the starter (plus web and Actuator if you want them):

   ```xml
   <dependency>
     <groupId>org.zedec</groupId>
     <artifactId>zxv-spring-boot-starter</artifactId>
     <version>1.1.0</version>
   </dependency>
   ```

3. Configure the context and the capabilities your code needs:

   ```yaml
   zxv:
     currencies: [NGN]
     root-permissions: [LEDGER_READ, LEDGER_POST]
     capabilities:
       teller:
         permissions: [LEDGER_READ, LEDGER_POST]
       auditor:
         permissions: [LEDGER_READ]
   ```

4. Inject a capability by name:

   ```java
   @Service
   class Treasury {
       private final Ledger ledger;

       Treasury(@Qualifier("teller") Capability teller) {
           this.ledger = Ledger.of(teller);
       }
   }
   ```

5. Run with native access granted to the class path:

   ```sh
   java --enable-native-access=ALL-UNNAMED -Dzxv.library=/path/to/libzxv.so -jar app.jar
   ```

   (Without `-Dzxv.library`, the SDK uses `ZXV_LIBRARY_PATH`, the copy
   bundled in the SDK jar, then the system loader path. You can also set
   `zxv.library` in `application.yml`.)

### The sample

[`zxv-spring-boot-sample`](zxv-spring-boot-sample/) is a small REST service:
exact quotes, a ledger posted through `teller` and read through `auditor`,
problem details for refusals, and health probes.

```sh
cd sdk/frameworks/spring-zxv
mvn install -DskipTests
mvn -pl zxv-spring-boot-sample spring-boot:run
```

```sh
curl -s localhost:8080/quote -H 'content-type: application/json' \
     -d '{"price":"12.75","quantity":"1/3"}'          # {"total":"4.25"}
curl -s localhost:8080/quote -H 'content-type: application/json' \
     -d '{"price":12.75,"quantity":"1"}'              # 400: a JSON float
curl -s 'localhost:8080/sum?a=0.1&b=0.2'              # {"total":"0.3"}
curl -s localhost:8080/payments -H 'content-type: application/json' \
     -d '{"amountMinor":15000,"reference":"E2E-1"}'   # posted with teller
curl -s localhost:8080/refunds -H 'content-type: application/json' \
     -d '{"amountMinor":100,"reference":"E2E-2"}'     # 403 ZXV_E_CAP: auditor cannot post
curl -s localhost:8080/actuator/health                # zxv UP, with capability details
```

Start it with `--sample.halt-endpoint.enabled=true` to get
`POST /admin/halt`, which halts the context so you can watch the fatal path:
the next ZXV call answers 500, and `/actuator/health`, `/health/liveness` and
`/health/readiness` answer 503. Never enable it outside a local demo.

## Configuration

| Property | Default | Meaning |
|---|---|---|
| `zxv.enabled` | `true` | Create the context and its beans. |
| `zxv.library` | unset | Path of `libzxv`. A second application context in the same JVM may name the same file again; a different file fails. |
| `zxv.fatal-policy` | `throw` | `throw`: `ZxvFatalError`. `abort`: halt the JVM at once (`Runtime.halt(70)`). |
| `zxv.root-permissions` | all | Permissions of the root capability. Every named capability must be a subset. |
| `zxv.currencies` | none | ISO 4217 codes enabled at start-up (needs `LEDGER_POST` on the root). |
| `zxv.private-currencies.<CODE>` | none | Private units and their minor units, e.g. `VFV: 2`. |
| `zxv.capabilities.<name>.permissions` | none | One `Capability` bean, `@Qualifier("<name>")`, bean name `zxv.capability.<name>`. |
| `zxv.jackson.strict-integers` | `true` | Turn off Jackson's `ACCEPT_FLOAT_AS_INT`, so `150.7` for a `long` amount is a 400 instead of a silent 150. |
| `management.health.zxv.enabled` | `true` | The `zxv` health contributor. |

Misconfiguration fails start-up with a message naming the property: an
unknown permission, a capability with no permissions, a capability asking
for a permission the root does not hold, a currency that is not ISO 4217,
or a `zxv.library` that is not a file. Nothing is silently narrowed.

## How it behaves

**Beans.**

| Bean | Type | Injectable by type | Closed |
|---|---|---|---|
| `zxvContext` | `ZxvContext` | yes (replace it with your own bean if you need to) | last |
| `zxvRootCapability` | `Capability` | **no** (`autowireCandidate = false`) | after the named ones |
| `zxv.capability.<name>` | `Capability` | with `@Qualifier("<name>")`, or by type if it is the only one | first |
| `zxvFatalHandler` | `ZxvFatalHandler` | yes | |
| `zxvHealthIndicator` | `ZxvHealthIndicator` | when Actuator is present | |
| `zxvJacksonModule` | `ZxvJacksonModule` | when Jackson is present | |

**Shutdown order.** Each named capability is a dependent of the root
capability (bean reference and `depends-on`), and the root is a dependent
of the context, so Spring destroys them children first, as ABI.md R6
requires. A poisoned context still shuts down cleanly.

**Fatal errors.** `ZxvFatalError` is an `Error`, so ordinary
`catch (Exception e)` blocks let it through. Spring MVC, however, wraps an
`Error` from a handler in a `ServletException`, which an application's
`@ExceptionHandler(Exception.class)` or `(Throwable.class)` would match and
answer as if nothing had happened. The starter's `ZxvFatalExceptionResolver`
runs before every `@ExceptionHandler`, finds the `ZxvFatalError` in the
cause chain, trips `ZxvFatalHandler` and rethrows it. The servlet container
then answers 500 through Boot's error page (which shows no exception text by
default). `ZxvFatalHandler` logs once at ERROR and publishes
`LivenessState.BROKEN` and `ReadinessState.REFUSING_TRAFFIC`, so with
`management.endpoint.health.probes.enabled=true` an orchestrator restarts the
process from trusted state. The health indicator trips it too when it finds
the context poisoned, even if no request touched it.

Outside Spring MVC, the error propagates as usual. Two places in Spring
log and suppress errors instead: `@Scheduled` tasks and `@Async` methods
returning `void`. In those, call `zxvFatalHandler.inspect(t)` from your
`ErrorHandler` / `AsyncUncaughtExceptionHandler` and rethrow, or create the
context with `zxv.fatal-policy=abort`.

**Health.** `zxv` is UP only when libzxv answers that the context is live,
DOWN when it is poisoned, closed or does not answer. Details (when
`show-details` allows): `state`, `abi`, `features`, and for each named
capability its permissions, attestation and revocation. No keys, ledger
data or halt reason. A revoked capability or one attested other than `TRUE`
is reported in the details but does not make the context DOWN. To include it
in the readiness probe:

```yaml
management:
  endpoint:
    health:
      probes:
        enabled: true
      group:
        readiness:
          include: readinessState,zxv
```

**JSON.** `Rational` is written as its exact string: `"12.75"` for a
terminating decimal, `"1/3"` otherwise, never a JSON number. It is read
from a string in the SDK's exact syntax (`"12.75"`, `"-0.1"`, `"3/4"`,
`"42"`) or a JSON integer that fits in 64 bits. A JSON floating-point number
(`0.1`, `1.0`, `1e3`) is always refused with `InvalidFormatException`, which
Spring MVC answers with 400, even with
`spring.jackson.deserialization.use-big-decimal-for-floats=true`. Outside
Spring, `mapper.registerModule(new ZxvJacksonModule())`.

Jackson's default `ACCEPT_FLOAT_AS_INT` would also let a float into an
integer field (`{"amountMinor": 150.7}` becomes 150). The starter turns it
off on Boot's `ObjectMapper`; set `zxv.jackson.strict-integers=false` to keep
Jackson's default.

**Request parameters.** `@RequestParam Rational amount` and
`@PathVariable Rational` use `Rational.parse`: `?amount=0.1` is exactly
1/10, `?amount=1e-1` is a 400.

## Build and test

```sh
export JAVA_HOME=/path/to/jdk-22-or-later
make -C sdk/libzxv all fixtures
mvn -f sdk/java/pom.xml -B install -DskipTests
cd sdk/frameworks/spring-zxv
mvn -B verify          # javac -Xlint:all with failOnWarning; 36 tests
```

The tests run against the real `libzxv` (Surefire passes
`--enable-native-access=ALL-UNNAMED -Dzxv.library=...`):

- `ZxvAutoConfigurationTest` (14): start-up, currencies and private units,
  the root not injectable, named capabilities holding exactly their
  permissions (a ledger post through `teller`, a refused post through
  `auditor`), qualifier injection, every start-up failure above, disabling,
  the fatal policy, `zxv.library`, the destroy order recorded by a
  `DestructionAwareBeanPostProcessor`, and shutdown of a poisoned context.
- `ZxvHealthIndicatorTest` (5): UP with details, attestation changes, DOWN
  on poison with liveness BROKEN and readiness REFUSING_TRAFFIC, DOWN when
  closed, disabled by property.
- `RationalJacksonTest` (6): exact output, exact input, floats refused (also
  with lenient mapper settings), floats refused for integer fields (and
  accepted again with `zxv.jackson.strict-integers=false`), malformed and
  oversized input refused.
- `ZxvWebIntegrationTest` (6): a real Tomcat on a random port; JSON and query
  exactness, a JSON float answered 400, a capability denial over HTTP, and
  the fatal path answered 500 although the test application has an
  `@ExceptionHandler(Throwable.class)` that answers 200, with health,
  liveness and readiness all 503.
- `SampleApplicationTest` (5): the sample end to end, including a JSON float
  for an integer amount answered 400.

## Honest limits

- **Spring MVC only.** The fatal resolver is for servlet applications.
  WebFlux, `@Scheduled` and `@Async void` need the handler wired in by hand
  (see above).
- **Fatal is as strong as the JVM allows.** Code that catches `Throwable`
  outside Spring MVC can still swallow `ZxvFatalError`; the context stays
  poisoned in C regardless. Use `zxv.fatal-policy=abort` when the process
  must stop at once.
- **Capabilities are least privilege, not a sandbox.** Any code in the JVM
  that can reach the `ZxvContext` bean can mint its own root capability.
- **One library per JVM.** `libzxv` is loaded once; every application
  context in the JVM shares it.
- **Not published.** The starter and the SDK are installed from source into
  your local repository; there is no Maven Central release yet.
