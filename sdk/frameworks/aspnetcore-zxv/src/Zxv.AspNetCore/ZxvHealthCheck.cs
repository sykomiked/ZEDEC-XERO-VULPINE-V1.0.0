// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.HealthChecks;

namespace Zxv.AspNetCore;

/// <summary>
/// Health of the hosted ZXV platform, fail-closed: Unhealthy when the context
/// is halted (poisoned), disposed, or cannot be asked; Healthy only when
/// libzxv answers that it is live. Finding it halted trips
/// <see cref="ZxvFatalState"/>.
/// </summary>
/// <remarks>
/// Data: the ABI version, the live features and, for each named capability,
/// its rights, attestation and revocation. No key material, ledger data or
/// halt reason.
/// </remarks>
public sealed class ZxvHealthCheck : IHealthCheck
{
    private readonly IServiceProvider _services;

    /// <summary>Initializes a new instance.</summary>
    /// <param name="services">The application services.</param>
    public ZxvHealthCheck(IServiceProvider services) => _services = services;

    /// <inheritdoc/>
    public Task<HealthCheckResult> CheckHealthAsync(HealthCheckContext context, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(context);
        ZxvFatalState? fatal = _services.GetService<ZxvFatalState>();
        HealthStatus down = context.Registration?.FailureStatus ?? HealthStatus.Unhealthy;
        ZxvPlatform platform = _services.GetRequiredService<ZxvPlatform>();
        bool halted;
        try
        {
            halted = platform.IsHalted;
        }
        catch (ZxvFatalException)
        {
            halted = true;
        }
        catch (ObjectDisposedException)
        {
            return Task.FromResult(new HealthCheckResult(down, "ZXV platform is disposed.", data: State("disposed")));
        }
        catch (ZxvException e)
        {
            return Task.FromResult(new HealthCheckResult(down, "ZXV platform did not answer: " + e.StatusName, data: State("unknown")));
        }

        if (halted)
        {
            fatal?.Trip("ZXV_E_POISONED");
            return Task.FromResult(new HealthCheckResult(down, "ZXV context halted.", data: State("halted")));
        }

        var data = new Dictionary<string, object>
        {
            ["state"] = "live",
            ["abi"] = platform.AbiVersion.ToString(),
            ["features"] = platform.Features.ToString(),
        };
        if (_services.GetService<ZxvCapabilities>() is { } caps)
        {
            foreach (string name in caps.Names)
            {
                data["capability:" + name] = Describe(caps.Get(name));
            }
        }

        return Task.FromResult(HealthCheckResult.Healthy("ZXV context is live.", data));
    }

    private static Dictionary<string, object> State(string state) => new() { ["state"] = state };

    private static string Describe(Capability cap)
    {
        try
        {
            CapabilityInfo info = cap.GetInfo();
            return $"rights={info.Rights}; attestation={info.Attestation}; revoked={info.IsRevoked}";
        }
        catch (ObjectDisposedException)
        {
            return "disposed";
        }
    }
}

/// <summary>Adds the ZXV health check.</summary>
public static class ZxvHealthChecksBuilderExtensions
{
    /// <summary>
    /// Adds <see cref="ZxvHealthCheck"/>. Tag it <c>"ready"</c> or <c>"live"</c>
    /// to put it in your probes' predicates.
    /// </summary>
    /// <param name="builder">The health checks builder.</param>
    /// <param name="name">The check name. Default <c>zxv</c>.</param>
    /// <param name="tags">Tags, e.g. <c>"ready"</c>.</param>
    /// <returns>The builder.</returns>
    public static IHealthChecksBuilder AddZxv(this IHealthChecksBuilder builder, string name = "zxv", params string[] tags)
    {
        ArgumentNullException.ThrowIfNull(builder);
        return builder.Add(new HealthCheckRegistration(name, sp => new ZxvHealthCheck(sp), HealthStatus.Unhealthy, tags));
    }
}
