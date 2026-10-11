// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A minimal API on Zxv.AspNetCore: exact quotes, a ledger posted through a
// "teller" capability and read through an "auditor" capability, health
// probes, and ZXV errors as problem details.
//
//   dotnet run --project samples/Zxv.AspNetCore.Sample
//   curl -s localhost:5080/quote -H 'content-type: application/json' -d '{"price":"12.75","quantity":"1/3"}'

using System.Linq;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Diagnostics.HealthChecks;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zxv;
using Zxv.AspNetCore;
using Zxv.AspNetCore.Sample;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

// Platform, root and named capabilities from the "Zxv" section of appsettings.json.
builder.Services.AddZxv(o => o.ReadFrom(builder.Configuration.GetSection(ZxvAspNetCoreOptions.SectionName)));
builder.Services.AddHealthChecks().AddZxv(tags: "ready");
builder.Services.AddSingleton<Treasury>();

WebApplication app = builder.Build();

app.UseExceptionHandler();
app.UseZxvProblemDetails();

app.MapPost("/quote", (QuoteRequest q) => new QuoteResponse(q.Price * q.Quantity));
app.MapGet("/sum", (Rational a, Rational b) => new QuoteResponse(a + b));
app.MapPost("/payments", (Payment p, Treasury t) => new Posted(t.PayCustomer(p.AmountMinor, p.Reference)));
app.MapPost("/refunds", (Payment p, Treasury t) => new Posted(t.RefundAsAuditor(p.AmountMinor, p.Reference)));
app.MapGet("/balance", (Treasury t) => t.CustomerBalance());

app.MapHealthChecks("/health/live", new HealthCheckOptions { Predicate = _ => false });
app.MapHealthChecks("/health/ready", new HealthCheckOptions { Predicate = c => c.Tags.Contains("ready") });

// The operator kill switch, for demonstrating the fatal path. Unauthenticated:
// enable it (Sample:HaltEndpoint=true) only in a local demo.
if (app.Configuration.GetValue<bool>("Sample:HaltEndpoint"))
{
    app.MapPost("/admin/halt", (ZxvPlatform zxv) =>
    {
        zxv.Halt("operator halt from the sample");
        return Results.Accepted();
    });
}

app.Run();

/// <summary>Entry point; partial so integration tests can use WebApplicationFactory.</summary>
public partial class Program
{
    private Program()
    {
    }
}
