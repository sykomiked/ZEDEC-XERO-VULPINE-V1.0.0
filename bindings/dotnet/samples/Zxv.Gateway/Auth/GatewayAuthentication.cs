// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Security.Cryptography.X509Certificates;
using System.Text.Encodings.Web;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Certificate;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Zxv.Gateway.Auth;

/// <summary>How operators authenticate to the gateway.</summary>
public enum GatewayAuthMode
{
    /// <summary>OAuth 2.0 / OIDC bearer tokens from the operator's identity provider.</summary>
    Oidc,

    /// <summary>Mutual TLS: a client certificate chained to a trusted CA, optionally pinned by thumbprint.</summary>
    Mtls,

    /// <summary>Every request is an operator. Refused outside the Development environment.</summary>
    Development,
}

/// <summary>The <c>Gateway:Authentication</c> configuration section.</summary>
public sealed class GatewayAuthOptions
{
    public GatewayAuthMode Mode { get; set; } = GatewayAuthMode.Oidc;

    /// <summary>Scope (OIDC <c>scope</c>/<c>scp</c> claim) or role every operator call requires.</summary>
    public string RequiredScope { get; set; } = "zxv.operator";

    public OidcSettings Oidc { get; set; } = new();

    public MtlsSettings Mtls { get; set; } = new();

    public sealed class OidcSettings
    {
        /// <summary>Issuer URL of the identity provider (metadata is fetched from it). No default: must be configured.</summary>
        public string Authority { get; set; } = string.Empty;

        public string Audience { get; set; } = "zxv-operator-api";
    }

    public sealed class MtlsSettings
    {
        /// <summary>Optional SHA-256 or SHA-1 thumbprints the client certificate must match (pinning).</summary>
        public IList<string> AllowedThumbprints { get; } = new List<string>();

        public bool RevocationCheck { get; set; } = true;
    }
}

/// <summary>Registers authentication and the <c>operator</c> authorization policy.</summary>
public static class GatewayAuthentication
{
    public const string OperatorPolicy = "operator";
    public const string DevelopmentScheme = "Development";

    public static WebApplicationBuilder AddGatewayAuthentication(this WebApplicationBuilder builder)
    {
        IConfigurationSection section = builder.Configuration.GetSection("Gateway:Authentication");
        var options = new GatewayAuthOptions();
        section.Bind(options);
        foreach (string t in section.GetSection("Mtls:AllowedThumbprints").Get<string[]>() ?? Array.Empty<string>())
        {
            options.Mtls.AllowedThumbprints.Add(t);
        }

        builder.Services.AddSingleton(Options.Create(options));
        string scope = options.RequiredScope;

        switch (options.Mode)
        {
            case GatewayAuthMode.Development:
                if (!builder.Environment.IsDevelopment())
                {
                    throw new InvalidOperationException(
                        "Gateway:Authentication:Mode=Development is only allowed when ASPNETCORE_ENVIRONMENT=Development. Configure Oidc or Mtls.");
                }

                builder.Services.AddAuthentication(DevelopmentScheme)
                    .AddScheme<AuthenticationSchemeOptions, DevelopmentOperatorHandler>(DevelopmentScheme, _ => { });
                break;

            case GatewayAuthMode.Oidc:
                if (string.IsNullOrWhiteSpace(options.Oidc.Authority))
                {
                    throw new InvalidOperationException("Gateway:Authentication:Oidc:Authority must be set to your identity provider's issuer URL.");
                }

                builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
                    .AddJwtBearer(o =>
                    {
                        o.Authority = options.Oidc.Authority;
                        o.Audience = options.Oidc.Audience;
                        o.RequireHttpsMetadata = true;
                        o.MapInboundClaims = false;
                        o.TokenValidationParameters.ValidateIssuer = true;
                        o.TokenValidationParameters.ValidateAudience = true;
                        o.TokenValidationParameters.ValidateLifetime = true;
                        o.TokenValidationParameters.ClockSkew = TimeSpan.FromMinutes(1);
                    });
                break;

            case GatewayAuthMode.Mtls:
                builder.Services.AddAuthentication(CertificateAuthenticationDefaults.AuthenticationScheme)
                    .AddCertificate(o =>
                    {
                        o.AllowedCertificateTypes = CertificateTypes.Chained;
                        o.RevocationMode = options.Mtls.RevocationCheck ? X509RevocationMode.Online : X509RevocationMode.NoCheck;
                        o.ValidateValidityPeriod = true;
                        o.Events = new CertificateAuthenticationEvents
                        {
                            OnCertificateValidated = ctx =>
                            {
                                IList<string> pins = options.Mtls.AllowedThumbprints;
                                X509Certificate2 cert = ctx.ClientCertificate;
                                bool pinned = pins.Count == 0 || pins.Any(p =>
                                    string.Equals(p, cert.Thumbprint, StringComparison.OrdinalIgnoreCase) ||
                                    string.Equals(p, cert.GetCertHashString(System.Security.Cryptography.HashAlgorithmName.SHA256), StringComparison.OrdinalIgnoreCase));
                                if (!pinned)
                                {
                                    ctx.Fail("Client certificate is not in Gateway:Authentication:Mtls:AllowedThumbprints.");
                                    return Task.CompletedTask;
                                }

                                var identity = new ClaimsIdentity(new[]
                                {
                                    new Claim(ClaimTypes.NameIdentifier, cert.Subject),
                                    new Claim("scope", scope),
                                }, ctx.Scheme.Name);
                                ctx.Principal = new ClaimsPrincipal(identity);
                                ctx.Success();
                                return Task.CompletedTask;
                            },
                        };
                    });
                break;

            default:
                throw new InvalidOperationException($"Unknown Gateway:Authentication:Mode '{options.Mode}'.");
        }

        builder.Services.AddAuthorizationBuilder()
            .AddPolicy(OperatorPolicy, p => p.RequireAuthenticatedUser().RequireAssertion(ctx => HasScope(ctx.User, scope)));
        return builder;
    }

    private static bool HasScope(ClaimsPrincipal user, string scope) =>
        user.FindAll("scope").Concat(user.FindAll("scp"))
            .SelectMany(c => c.Value.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            .Any(s => string.Equals(s, scope, StringComparison.Ordinal))
        || user.IsInRole(scope);
}

/// <summary>Development-only scheme: authenticates every request as a local operator.</summary>
internal sealed class DevelopmentOperatorHandler : AuthenticationHandler<AuthenticationSchemeOptions>
{
    private readonly GatewayAuthOptions _options;

    public DevelopmentOperatorHandler(
        IOptionsMonitor<AuthenticationSchemeOptions> options, ILoggerFactory logger, UrlEncoder encoder, IOptions<GatewayAuthOptions> gateway)
        : base(options, logger, encoder)
    {
        _options = gateway.Value;
    }

    protected override Task<AuthenticateResult> HandleAuthenticateAsync()
    {
        var identity = new ClaimsIdentity(new[]
        {
            new Claim(ClaimTypes.NameIdentifier, "development-operator"),
            new Claim("scope", _options.RequiredScope),
        }, Scheme.Name);
        return Task.FromResult(AuthenticateResult.Success(new AuthenticationTicket(new ClaimsPrincipal(identity), Scheme.Name)));
    }
}
