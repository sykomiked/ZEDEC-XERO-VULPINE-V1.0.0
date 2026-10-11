// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Zxv.Gateway: a sample operator REST gateway over the ZXV .NET SDK.
// Minimal API, OpenAPI document at /openapi/v1.json (Swagger UI at /swagger in
// Development), RFC 9457 problem details, health checks at /health/live and
// /health/ready, and OIDC or mTLS authentication (see docs/DOTNET_SDK.md).
// It ships no keys, no certificates and no network connectivity to PAPSS,
// SWIFT or CIPS.

using System;
using System.Text.Json.Serialization;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Diagnostics.HealthChecks;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Json;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.OpenApi.Models;
using Zxv;
using Zxv.Gateway.Auth;
using Zxv.Gateway.Endpoints;
using Zxv.Gateway.Infrastructure;
using Zxv.Payments;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

// ZXV platform: options from the "Zxv" section; transport from "Gateway:Transport".
builder.Services.AddZxv(builder.Configuration.GetSection(ZxvOptions.SectionName));
string? dropDirectory = builder.Configuration["Gateway:Transport:Directory"];
if (!string.IsNullOrWhiteSpace(dropDirectory))
{
    builder.Services.AddSingleton<IMxTransport>(new DirectoryMxTransport(dropDirectory));
}

builder.Services.AddSingleton<GatewayState>();

// JSON: enums as strings, camelCase (the default), no float amounts anywhere.
builder.Services.Configure<JsonOptions>(o => o.SerializerOptions.Converters.Add(new JsonStringEnumConverter()));

// Errors: RFC 9457 problem details for everything, SDK refusals mapped by status.
builder.Services.AddProblemDetails();
builder.Services.AddExceptionHandler<ZxvExceptionHandler>();

// Health: live = process up; ready = native core loaded and ledger invariants hold.
builder.Services.AddHealthChecks().AddCheck<LedgerHealthCheck>("ledger", tags: new[] { "ready" });

// Authentication placeholder: Oidc (default), Mtls, or Development (dev only).
builder.AddGatewayAuthentication();

// OpenAPI.
builder.Services.AddEndpointsApiExplorer();
builder.Services.AddSwaggerGen(o =>
{
    o.SwaggerDoc("v1", new OpenApiInfo
    {
        Title = "ZXV Operator API",
        Version = "1.0.0",
        Description = "Operator gateway over the ZXV platform core. Amounts are exact integer minor units with ISO 4217 alpha codes. "
            + "No PAPSS, SWIFT or CIPS connectivity is included.",
    });
    o.AddSecurityDefinition("oidc", new OpenApiSecurityScheme
    {
        Type = SecuritySchemeType.Http,
        Scheme = "bearer",
        BearerFormat = "JWT",
        Description = "OAuth 2.0 access token from the operator's identity provider, with scope zxv.operator.",
    });
    o.AddSecurityRequirement(new OpenApiSecurityRequirement
    {
        [new OpenApiSecurityScheme { Reference = new OpenApiReference { Type = ReferenceType.SecurityScheme, Id = "oidc" } }] = new[] { "zxv.operator" },
    });
});

WebApplication app = builder.Build();

app.UseExceptionHandler();
app.UseStatusCodePages();
app.UseAuthentication();
app.UseAuthorization();

app.UseSwagger(o => o.RouteTemplate = "openapi/{documentName}.json");
if (app.Environment.IsDevelopment())
{
    app.UseSwaggerUI(o => o.SwaggerEndpoint("/openapi/v1.json", "ZXV Operator API v1"));
}

app.MapHealthChecks("/health/live", new HealthCheckOptions { Predicate = _ => false }).AllowAnonymous();
app.MapHealthChecks("/health/ready", new HealthCheckOptions { Predicate = c => c.Tags.Contains("ready") }).AllowAnonymous();
app.MapOperatorApi();

// Resolve the platform at start so a bad configuration fails fast, not on the first request.
_ = app.Services.GetRequiredService<ZxvPlatform>();

app.Run();

/// <summary>Entry point; partial so integration tests can use WebApplicationFactory.</summary>
public partial class Program
{
    private Program()
    {
    }
}
