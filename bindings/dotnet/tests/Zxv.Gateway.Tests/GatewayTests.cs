// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace Zxv.Gateway.Tests;

public sealed class DevelopmentGatewayFactory : WebApplicationFactory<Program>
{
    protected override void ConfigureWebHost(IWebHostBuilder builder) => builder.UseEnvironment("Development");
}

public sealed class GatewayTests : IClassFixture<DevelopmentGatewayFactory>
{
    private readonly HttpClient _http;

    public GatewayTests(DevelopmentGatewayFactory factory) => _http = factory.CreateClient();

    private async Task<uint> OpenAsync(string name, string currency, string kind = "Holder")
    {
        HttpResponseMessage r = await _http.PostAsJsonAsync("/v1/accounts", new { name, currency, kind });
        Assert.Equal(HttpStatusCode.Created, r.StatusCode);
        return (await r.Content.ReadFromJsonAsync<JsonElement>()).GetProperty("id").GetUInt32();
    }

    private Task<HttpResponseMessage> PayAsync(uint from, uint to, string currency, long minor, string? e2e = null) =>
        _http.PostAsJsonAsync("/v1/payments", new { fromAccount = from, toAccount = to, amount = new { currency, minorUnits = minor }, endToEndId = e2e });

    private static async Task<JsonElement> ProblemAsync(HttpResponseMessage r, HttpStatusCode status, string code)
    {
        Assert.Equal(status, r.StatusCode);
        Assert.Equal("application/problem+json", r.Content.Headers.ContentType?.MediaType);
        JsonElement p = await r.Content.ReadFromJsonAsync<JsonElement>();
        Assert.Equal(code, p.GetProperty("code").GetString());
        Assert.Equal((int)status, p.GetProperty("status").GetInt32());
        return p;
    }

    [Fact]
    public async Task Health_endpoints()
    {
        Assert.Equal("Healthy", await _http.GetStringAsync("/health/live"));
        Assert.Equal("Healthy", await _http.GetStringAsync("/health/ready"));
    }

    [Fact]
    public async Task Platform_reports_features()
    {
        JsonElement p = await _http.GetFromJsonAsync<JsonElement>("/v1/platform");
        Assert.Equal("1.0.0", p.GetProperty("abiVersion").GetString());
        Assert.Contains("Pacs008", p.GetProperty("features").EnumerateArray().Select(f => f.GetString()));
    }

    [Fact]
    public async Task Currencies()
    {
        JsonElement ngn = await _http.GetFromJsonAsync<JsonElement>("/v1/currencies/NGN");
        Assert.Equal(566, ngn.GetProperty("numeric").GetInt32());
        Assert.True(ngn.GetProperty("wireCurrency").GetBoolean());
        JsonElement vfv = await _http.GetFromJsonAsync<JsonElement>("/v1/currencies/VFV");
        Assert.False(vfv.GetProperty("wireCurrency").GetBoolean());
        JsonElement enabled = await _http.GetFromJsonAsync<JsonElement>("/v1/currencies?scope=enabled");
        Assert.Contains("XOF", enabled.EnumerateArray().Select(c => c.GetProperty("code").GetString()));
        await ProblemAsync(await _http.GetAsync("/v1/currencies/QQQ"), HttpStatusCode.NotFound, "ZXV_E_NOT_FOUND");
    }

    [Fact]
    public async Task Payment_lifecycle()
    {
        uint issuer = await OpenAsync("Accra settlement", "GHS", "Issuer");
        uint ama = await OpenAsync("Ama Mensah", "GHS");
        uint kofi = await OpenAsync("Kofi Boateng", "GHS");
        Assert.Equal(HttpStatusCode.Created, (await PayAsync(issuer, ama, "GHS", 500_000, "GW-FUND-1")).StatusCode);

        HttpResponseMessage r = await PayAsync(ama, kofi, "GHS", 123_45, "GW-PAY-1");
        Assert.Equal(HttpStatusCode.Created, r.StatusCode);
        Assert.Equal("/v1/payments/GW-PAY-1", r.Headers.Location?.OriginalString);
        JsonElement pay = await r.Content.ReadFromJsonAsync<JsonElement>();
        Assert.Equal(12345, pay.GetProperty("amount").GetProperty("minorUnits").GetInt64());
        Assert.Equal("123.45", pay.GetProperty("amount").GetProperty("display").GetString());

        HttpResponseMessage xml = await _http.GetAsync("/v1/payments/GW-PAY-1/pacs008");
        Assert.Equal("application/xml", xml.Content.Headers.ContentType?.MediaType);
        Assert.Contains("<IntrBkSttlmAmt Ccy=\"GHS\">123.45</IntrBkSttlmAmt>", await xml.Content.ReadAsStringAsync(), StringComparison.Ordinal);

        JsonElement bal = await _http.GetFromJsonAsync<JsonElement>($"/v1/accounts/{kofi}/balances");
        Assert.Equal(12345, bal.GetProperty("equity").GetProperty("minorUnits").GetInt64());
        Assert.Equal(777, bal.GetProperty("railNumerics").GetProperty("credit").GetInt32());

        JsonElement stmt = await _http.GetFromJsonAsync<JsonElement>($"/v1/accounts/{ama}/statement");
        Assert.Equal(2, stmt.GetProperty("entries").GetArrayLength());
        Assert.Equal(500_000 - 12_345, stmt.GetProperty("closing").GetProperty("minorUnits").GetInt64());
        Assert.Contains("camt.053.001.08", stmt.GetProperty("camt053").GetString(), StringComparison.Ordinal);

        Assert.Equal(HttpStatusCode.NoContent, (await _http.PostAsync("/v1/payments/GW-PAY-1/retransmit", null)).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await _http.GetAsync("/v1/payments/NOPE")).StatusCode);
    }

    [Fact]
    public async Task Refusals_are_problem_details()
    {
        uint a = await OpenAsync("A", "NGN");
        uint b = await OpenAsync("B", "NGN");
        await ProblemAsync(await PayAsync(a, b, "NGN", 5), HttpStatusCode.UnprocessableEntity, "ZXV_E_FUNDS");
        await ProblemAsync(await PayAsync(a, b, "NGN", 0), HttpStatusCode.BadRequest, "ZXV_E_RANGE");
        await ProblemAsync(await _http.GetAsync("/v1/accounts/99999"), HttpStatusCode.NotFound, "ZXV_E_NOT_FOUND");

        uint issuer = await OpenAsync("Settlement", "NGN", "Issuer");
        Assert.Equal(HttpStatusCode.Created, (await PayAsync(issuer, a, "NGN", 100, "GW-DUP-1")).StatusCode);
        await ProblemAsync(await PayAsync(issuer, a, "NGN", 100, "GW-DUP-1"), HttpStatusCode.Conflict, "ZXV_E_DUPLICATE");

        uint v = await OpenAsync("V", "VFV");
        await ProblemAsync(await PayAsync(v, v, "VFV", 1), HttpStatusCode.UnprocessableEntity, "ZXV_E_CURRENCY");
    }

    [Fact]
    public async Task Pending_features_answer_501()
    {
        await ProblemAsync(await _http.PostAsJsonAsync("/v1/netting-cycles", new { cycleId = "C1", currency = "NGN", participants = new[] { "A", "B" } }), HttpStatusCode.NotImplemented, "ZXV_E_NOTIMPL");
        uint a = await OpenAsync("Card", "NGN");
        await ProblemAsync(await _http.PostAsJsonAsync("/v1/cards", new { network = "Dragon", account = a, limitMinorUnits = 1000 }), HttpStatusCode.NotImplemented, "ZXV_E_NOTIMPL");
        JsonElement cd = await (await _http.PostAsJsonAsync("/v1/cards/check-digit", new { network = "Dragon", pan = "79927398713" })).Content.ReadFromJsonAsync<JsonElement>();
        Assert.True(cd.GetProperty("valid").GetBoolean());
        JsonElement conf = await _http.GetFromJsonAsync<JsonElement>("/v1/conformance");
        Assert.True(conf.GetProperty("ledgerInvariantsHold").GetBoolean());
    }

    [Fact]
    public async Task Hand_written_openapi_matches_served_paths()
    {
        JsonElement served = await _http.GetFromJsonAsync<JsonElement>("/openapi/v1.json");
        var servedOps = served.GetProperty("paths").EnumerateObject()
            .SelectMany(p => p.Value.EnumerateObject().Select(m => $"{m.Name.ToUpperInvariant()} {p.Name} {m.Value.GetProperty("operationId").GetString()}"))
            .ToHashSet(StringComparer.Ordinal);

        string yaml = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "zxv-operator.yaml"));
        var written = new HashSet<string>(StringComparer.Ordinal);
        string? path = null;
        string? method = null;
        foreach (string line in yaml.Split('\n'))
        {
            Match m;
            if ((m = Regex.Match(line, @"^  (/[^:]+):\s*$")).Success)
            {
                path = m.Groups[1].Value;
            }
            else if (path is not null && (m = Regex.Match(line, @"^    (get|post|put|patch|delete):\s*$")).Success)
            {
                method = m.Groups[1].Value.ToUpperInvariant();
            }
            else if (path is not null && method is not null && (m = Regex.Match(line, @"^      operationId:\s*(\S+)")).Success)
            {
                if (path.StartsWith("/v1/", StringComparison.Ordinal))
                {
                    written.Add($"{method} {path} {m.Groups[1].Value}");
                }

                method = null;
            }
            else if (line.StartsWith("components:", StringComparison.Ordinal))
            {
                break;
            }
        }

        Assert.Empty(servedOps.Except(written));
        Assert.Empty(written.Except(servedOps));
    }

    [Fact]
    public async Task Unauthenticated_scheme_is_development_only()
    {
        using var prod = new WebApplicationFactory<Program>().WithWebHostBuilder(b =>
        {
            b.UseEnvironment("Production");
            b.UseSetting("Gateway:Authentication:Mode", "Development");
        });
        var ex = Assert.ThrowsAny<Exception>(() => prod.CreateClient());
        Assert.Contains("only allowed when ASPNETCORE_ENVIRONMENT=Development", ex.ToString(), StringComparison.Ordinal);
        await Task.CompletedTask;
    }

    [Fact]
    public async Task Oidc_mode_requires_a_token()
    {
        using var oidc = new WebApplicationFactory<Program>().WithWebHostBuilder(b =>
        {
            b.UseEnvironment("Production");
            b.UseSetting("Gateway:Authentication:Mode", "Oidc");
            b.UseSetting("Gateway:Authentication:Oidc:Authority", "https://idp.example.invalid/realms/zxv");
        });
        HttpClient client = oidc.CreateClient();
        Assert.Equal(HttpStatusCode.Unauthorized, (await client.GetAsync("/v1/platform")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/health/live")).StatusCode);
    }
}
