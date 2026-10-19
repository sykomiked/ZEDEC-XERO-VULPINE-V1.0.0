<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV framework kits

Enterprise framework integrations over the language SDKs. Each one wires the
same ideas into its framework's own idioms:

| Idea | [Spring Boot](spring-zxv/) | [ASP.NET Core](aspnetcore-zxv/) | [NestJS](nest-zxv/) | [Django](django-zxv/) |
|---|---|---|---|---|
| One managed context, closed children first (ABI.md R6) | `ZxvContext` bean, named capabilities `depends-on` the root | hosted `ZxvPlatform`, container disposes in reverse creation order | `ZxvModule.forRoot`/`forRootAsync`, closed in `onApplicationShutdown` | `ZxvConfig.ready()` owns it; `django_zxv.shutdown()` / `atexit` |
| Narrow capabilities from configuration; the root never injectable | `zxv.capabilities.<name>`, `@Qualifier("<name>")` | `Zxv:Capabilities:<name>`, `[FromKeyedServices("<name>")]` | `capabilities: {<name>: ...}`, `@InjectCapability('<name>')` | `ZXV["CAPABILITIES"]`, `django_zxv.get_capability("<name>")` |
| Fail-closed health | Actuator `zxv`: DOWN when poisoned; liveness BROKEN | `AddHealthChecks().AddZxv()`: Unhealthy when halted | terminus `ZxvHealthIndicator`: down when halted or closed | `django_zxv.views.health`: 503 unless live |
| A halted context is never answered as a normal response | `ZxvFatalError` rethrown past every `@ExceptionHandler` (500) | middleware answers 503 and trips `ZxvFatalState` | `ZxvExceptionFilter` answers 503 and records the halt | `ZxvProblemMiddleware` catches `ZxvFatal`, answers 503 |
| Exact JSON | Jackson: `Rational` as `"0.1"` / `"1/3"`, JSON floats refused | System.Text.Json: same rules | `exactJson()` parser, `ParseRationalPipe`, `ExactBodyPipe` refuse floats | `read_exact_json` refuses floats; `RationalField` stores exact `n/d` |
| Errors without secrets | (your `@ControllerAdvice`; the sample shows one) | problem details by status class, no native detail text | problem details by status class, no native detail text | problem details by status class, no native detail text |

| Kit | Package | Over | Tests |
|---|---|---|---|
| [`spring-zxv`](spring-zxv/) | `org.zedec:zxv-spring-boot-starter` | [`sdk/java`](../java/) (`org.zedec.zxv`) | 36 |
| [`aspnetcore-zxv`](aspnetcore-zxv/) | `Zxv.AspNetCore` | [`sdk/dotnet`](../dotnet/) (`Zxv.Sdk`) | 56 |
| [`nest-zxv`](nest-zxv/) | `@zedec/zxv-nestjs` | [`sdk/typescript`](../typescript/) (`@zedec/zxv`) | 35 |
| [`django-zxv`](django-zxv/) | `django-zxv` | [`sdk/python`](../python/) (`zxv`) | 56 |

Each tests against the real `libzxv`. The Spring and ASP.NET Core kits build
with warnings as errors; the NestJS kit typechecks under strict TypeScript and
the Django kit under `mypy --strict`. CI: [`.github/workflows/sdk-frameworks.yml`](../../.github/workflows/sdk-frameworks.yml).
