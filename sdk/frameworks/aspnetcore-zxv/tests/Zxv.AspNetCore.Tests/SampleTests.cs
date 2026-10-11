// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace Zxv.AspNetCore.Tests;

/// <summary>The minimal-API sample end to end over WebApplicationFactory, against the real libzxv.</summary>
public sealed class SampleTests : IClassFixture<WebApplicationFactory<Program>>
{
    private readonly WebApplicationFactory<Program> _factory;

    public SampleTests(WebApplicationFactory<Program> factory) => _factory = factory;

    private static StringContent Json(string json) => new(json, Encoding.UTF8, "application/json");

    [Fact]
    public async Task Quotes_exactly()
    {
        using HttpClient http = _factory.CreateClient();
        HttpResponseMessage r = await http.PostAsync("/quote", Json("{\"price\":\"12.75\",\"quantity\":\"1/3\"}"));
        Assert.Equal(HttpStatusCode.OK, r.StatusCode);
        Assert.Equal("{\"total\":\"4.25\"}", await r.Content.ReadAsStringAsync());
        Assert.Equal("{\"total\":\"0.3\"}", await http.GetStringAsync("/sum?a=0.1&b=0.2"));
        Assert.Equal("{\"total\":\"1/3\"}", await http.GetStringAsync("/sum?a=1/6&b=1/6"));
    }

    [Theory]
    [InlineData("{\"price\":12.75,\"quantity\":\"1\"}")]
    [InlineData("{\"price\":\"12.75\",\"quantity\":1e0}")]
    [InlineData("{\"price\":\"1.2e1\",\"quantity\":\"1\"}")]
    public async Task Refuses_floats_with_400(string body)
    {
        using HttpClient http = _factory.CreateClient();
        HttpResponseMessage r = await http.PostAsync("/quote", Json(body));
        Assert.Equal(HttpStatusCode.BadRequest, r.StatusCode);
    }

    [Fact]
    public async Task Refuses_a_float_for_an_integer_amount()
    {
        using HttpClient http = _factory.CreateClient();
        HttpResponseMessage r = await http.PostAsync("/payments", Json("{\"amountMinor\":150.7,\"reference\":\"E2E-F\"}"));
        Assert.Equal(HttpStatusCode.BadRequest, r.StatusCode);
    }

    [Fact]
    public async Task Refuses_float_query_parameters_with_400()
    {
        using HttpClient http = _factory.CreateClient();
        Assert.Equal(HttpStatusCode.BadRequest, (await http.GetAsync("/sum?a=0.1&b=1e3")).StatusCode);
    }

    [Fact]
    public async Task Teller_posts_and_auditor_is_refused_with_problem_details()
    {
        using HttpClient http = _factory.CreateClient();
        HttpResponseMessage paid = await http.PostAsync("/payments", Json("{\"amountMinor\":15000,\"reference\":\"E2E-SAMPLE-1\"}"));
        Assert.Equal(HttpStatusCode.OK, paid.StatusCode);
        Assert.Contains("\"equity\":", await http.GetStringAsync("/balance"), StringComparison.Ordinal);

        HttpResponseMessage refund = await http.PostAsync("/refunds", Json("{\"amountMinor\":100,\"reference\":\"E2E-SECRET-REF\"}"));
        Assert.Equal(HttpStatusCode.Forbidden, refund.StatusCode);
        Assert.Equal("application/problem+json", refund.Content.Headers.ContentType?.MediaType);
        string body = await refund.Content.ReadAsStringAsync();
        using JsonDocument doc = JsonDocument.Parse(body);
        Assert.Equal("ZXV_E_CAP", doc.RootElement.GetProperty("code").GetString());
        Assert.Equal("Refused", doc.RootElement.GetProperty("class").GetString());
        Assert.Equal("urn:zxv:problem:zxv_e_cap", doc.RootElement.GetProperty("type").GetString());
        Assert.DoesNotContain("E2E-SECRET-REF", body, StringComparison.Ordinal);
        Assert.DoesNotContain("Ledger.Post", body, StringComparison.Ordinal);
        Assert.False(doc.RootElement.TryGetProperty("detail", out _));
    }

    [Fact]
    public async Task Health_endpoints_are_healthy()
    {
        using HttpClient http = _factory.CreateClient();
        Assert.Equal("Healthy", await http.GetStringAsync("/health/live"));
        Assert.Equal("Healthy", await http.GetStringAsync("/health/ready"));
    }

    [Fact]
    public async Task Halt_endpoint_is_off_by_default()
    {
        using HttpClient http = _factory.CreateClient();
        Assert.Equal(HttpStatusCode.NotFound, (await http.PostAsync("/admin/halt", null)).StatusCode);
    }
}

/// <summary>The fatal path in the sample: its own factory, because halting poisons the platform.</summary>
public sealed class SampleFatalTests : IDisposable
{
    private readonly WebApplicationFactory<Program> _factory =
        new WebApplicationFactory<Program>().WithWebHostBuilder(b => b.UseSetting("Sample:HaltEndpoint", "true"));

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task Halt_answers_503_and_turns_readiness_unhealthy()
    {
        using HttpClient http = _factory.CreateClient();
        Assert.Equal("Healthy", await http.GetStringAsync("/health/ready"));
        Assert.Equal(HttpStatusCode.Accepted, (await http.PostAsync("/admin/halt", null)).StatusCode);

        HttpResponseMessage r = await http.GetAsync("/balance");
        Assert.Equal(HttpStatusCode.ServiceUnavailable, r.StatusCode);
        string body = await r.Content.ReadAsStringAsync();
        using JsonDocument doc = JsonDocument.Parse(body);
        Assert.Equal("Fatal", doc.RootElement.GetProperty("class").GetString());
        Assert.Equal("Service halted", doc.RootElement.GetProperty("title").GetString());
        Assert.DoesNotContain("operator halt", body, StringComparison.Ordinal);

        HttpResponseMessage ready = await http.GetAsync("/health/ready");
        Assert.Equal(HttpStatusCode.ServiceUnavailable, ready.StatusCode);
        Assert.Equal("Unhealthy", await ready.Content.ReadAsStringAsync());

        // Exact arithmetic needs no context and keeps working.
        Assert.Equal("{\"total\":\"3\"}", await http.GetStringAsync("/sum?a=1&b=2"));
    }
}
