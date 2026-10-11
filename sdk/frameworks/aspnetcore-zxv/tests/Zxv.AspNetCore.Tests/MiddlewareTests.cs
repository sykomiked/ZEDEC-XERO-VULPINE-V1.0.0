// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.DependencyInjection;
using Zxv.Interop;

namespace Zxv.AspNetCore.Tests;

/// <summary>Every ZXV status class through the middleware, and that nothing secret reaches the client.</summary>
public sealed class MiddlewareTests : IAsyncLifetime
{
    private const string Secret = "acct=ada ref=E2E-SECRET";
    private WebApplication _app = null!;
    private HttpClient _http = null!;

    public async Task InitializeAsync()
    {
        // Development: minimal APIs throw BadHttpRequestException for bad input.
        WebApplicationBuilder b = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Development" });
        b.WebHost.UseTestServer();
        b.Services.AddZxv(o => o.Capabilities["reader"] = CapabilityRights.LedgerRead);
        _app = b.Build();
        _app.UseZxvProblemDetails();
        _app.MapGet("/usage", () => { throw new ZxvUsageException(ZxvStatus.InvalidFormat, "Rational.Parse", Secret); });
        _app.MapGet("/float", () => Rational.FromDoubleExact(double.NaN).ToString());
        _app.MapGet("/funds", () => { throw new InsufficientFundsException("Ledger.Post", Secret); });
        _app.MapGet("/budget", () => { throw new BudgetExhaustedException("Budget.Consume", Secret); });
        _app.MapGet("/notfound", () => { throw new ZxvRefusedException(ZxvStatus.NotFound, "Ledger.GetAccount", Secret); });
        _app.MapGet("/notimpl", () => { throw new ZxvFeatureNotAvailableException("Netting.Run", Secret); });
        _app.MapGet("/system", () => { throw new ZxvSystemException(ZxvStatus.IOError, "Model.Open", Secret); });
        _app.MapGet("/cap", ([FromKeyedServices("reader")] Capability reader) =>
        {
            reader.Demand(CapabilityRights.CryptoSign);
            return "no";
        });
        _app.MapGet("/fatal", (ZxvPlatform zxv) =>
        {
            zxv.Halt(Secret);
            return zxv.CreateRootCapability().Rights.ToString();
        });
        _app.MapPost("/body", (Holder h) => h.Value.ToString());
        _app.MapGet("/other", () => { throw new InvalidOperationException(Secret); });
        await _app.StartAsync();
        _http = _app.GetTestClient();
    }

    public async Task DisposeAsync()
    {
        _http.Dispose();
        await _app.DisposeAsync();
    }

    [Theory]
    [InlineData("/usage", 400, "ZXV_E_FORMAT", "Usage")]
    [InlineData("/float", 400, "ZXV_E_FLOAT", "Usage")]
    [InlineData("/funds", 422, "ZXV_E_FUNDS", "Refused")]
    [InlineData("/budget", 429, "ZXV_E_BUDGET", "Refused")]
    [InlineData("/notfound", 404, "ZXV_E_NOT_FOUND", "Refused")]
    [InlineData("/notimpl", 501, "ZXV_E_NOTIMPL", "Refused")]
    [InlineData("/system", 500, "ZXV_E_IO", "System")]
    [InlineData("/cap", 403, "ZXV_E_CAP", "Refused")]
    public async Task Maps_status_classes_to_problem_details(string path, int status, string code, string cls)
    {
        HttpResponseMessage r = await _http.GetAsync(path);
        Assert.Equal(status, (int)r.StatusCode);
        Assert.Equal("application/problem+json", r.Content.Headers.ContentType?.MediaType);
        string body = await r.Content.ReadAsStringAsync();
        using JsonDocument doc = JsonDocument.Parse(body);
        Assert.Equal(code, doc.RootElement.GetProperty("code").GetString());
        Assert.Equal(cls, doc.RootElement.GetProperty("class").GetString());
        Assert.Equal(status, doc.RootElement.GetProperty("status").GetInt32());
        Assert.True(doc.RootElement.TryGetProperty("traceId", out _));
        Assert.DoesNotContain("E2E-SECRET", body, StringComparison.Ordinal);
        Assert.DoesNotContain("acct=", body, StringComparison.Ordinal);
        Assert.False(doc.RootElement.TryGetProperty("detail", out _));
    }

    [Fact]
    public async Task Fatal_is_503_and_trips_the_fatal_state()
    {
        HttpResponseMessage r = await _http.GetAsync("/fatal");
        Assert.Equal(HttpStatusCode.ServiceUnavailable, r.StatusCode);
        string body = await r.Content.ReadAsStringAsync();
        Assert.DoesNotContain("E2E-SECRET", body, StringComparison.Ordinal);
        Assert.Contains("ZXV_E_POISONED", body, StringComparison.Ordinal);
        Assert.True(_app.Services.GetRequiredService<ZxvFatalState>().IsTripped);
    }

    /// <summary>A request body.</summary>
    /// <param name="Value">An exact value.</param>
    public sealed record Holder(Rational Value);

    [Fact]
    public async Task Json_float_in_a_body_is_400_without_a_message()
    {
        using var content = new StringContent("{\"value\":0.1}", System.Text.Encoding.UTF8, "application/json");
        HttpResponseMessage r = await _http.PostAsync("/body", content);
        Assert.Equal(HttpStatusCode.BadRequest, r.StatusCode);
        string body = await r.Content.ReadAsStringAsync();
        Assert.Contains("urn:zxv:problem:bad-request", body, StringComparison.Ordinal);
        Assert.DoesNotContain("floating-point", body, StringComparison.Ordinal);

        using var exact = new StringContent("{\"value\":\"0.1\"}", System.Text.Encoding.UTF8, "application/json");
        Assert.Equal("0.1", await (await _http.PostAsync("/body", exact)).Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Other_exceptions_pass_through()
    {
        // Not mapped by the ZXV middleware: it reaches the host's own handler
        // (here the Development exception page) as a plain 500.
        HttpResponseMessage r = await _http.GetAsync("/other");
        Assert.Equal(HttpStatusCode.InternalServerError, r.StatusCode);
        Assert.DoesNotContain("urn:zxv:problem", await r.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }
}
