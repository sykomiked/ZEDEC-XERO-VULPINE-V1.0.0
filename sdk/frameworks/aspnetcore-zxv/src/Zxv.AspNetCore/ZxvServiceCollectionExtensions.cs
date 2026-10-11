// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using Microsoft.AspNetCore.Http.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Zxv.Payments;

namespace Zxv.AspNetCore;

/// <summary>Registers the hosted ZXV platform with an ASP.NET Core application.</summary>
public static class ZxvServiceCollectionExtensions
{
    /// <summary>
    /// Adds one hosted <see cref="ZxvPlatform"/>, its root capability (never
    /// injectable), one keyed <see cref="Capability"/> singleton per entry in
    /// <see cref="ZxvAspNetCoreOptions.Capabilities"/>, a hosted service that
    /// creates them all at start-up, the <see cref="Rational"/> JSON converter
    /// for minimal APIs and MVC, and problem-details services.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <paramref name="configure"/> runs once, here, so the capability names
    /// are known when the keyed services are registered; the options are then
    /// validated and frozen.
    /// </para>
    /// <para>
    /// Disposal follows the C ABI's destroy order (ABI.md R6). The container
    /// disposes singletons in reverse order of creation, and each named
    /// capability is created from the root, which is created from the
    /// platform: named capabilities are disposed first, then the root, then
    /// the platform.
    /// </para>
    /// <para>
    /// This is not <c>Zxv.Sdk</c>'s <c>AddZxv(Action&lt;ZxvOptions&gt;)</c>,
    /// which registers only the platform. Use one or the other.
    /// </para>
    /// </remarks>
    /// <param name="services">The service collection.</param>
    /// <param name="configure">Configures the options, e.g. <c>o =&gt; o.ReadFrom(builder.Configuration.GetSection("Zxv"))</c>.</param>
    /// <returns>The service collection.</returns>
    /// <exception cref="OptionsValidationException">The options are invalid.</exception>
    /// <exception cref="InvalidOperationException">A <see cref="ZxvPlatform"/> is already registered.</exception>
    public static IServiceCollection AddZxv(this IServiceCollection services, Action<ZxvAspNetCoreOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);
        if (services.Any(d => d.ServiceType == typeof(ZxvPlatform)))
        {
            throw new InvalidOperationException("A ZxvPlatform is already registered. Call AddZxv once, and not together with Zxv.Sdk's AddZxv.");
        }

        var options = new ZxvAspNetCoreOptions();
        configure(options);
        ValidateOptionsResult result = new ZxvAspNetCoreOptionsValidator().Validate(Options.DefaultName, options);
        if (result.Failed)
        {
            throw new OptionsValidationException(Options.DefaultName, typeof(ZxvAspNetCoreOptions), result.Failures);
        }

        services.AddSingleton(Options.Create(options));
        services.TryAddSingleton<IMxTransport>(NullMxTransport.Instance);
        services.TryAddSingleton(TimeProvider.System);
        services.TryAddSingleton<ZxvFatalState>();

        services.AddSingleton(sp => new ZxvPlatform(
            Options.Create(options.Platform),
            sp.GetService<ILogger<ZxvPlatform>>(),
            sp.GetRequiredService<IMxTransport>(),
            sp.GetRequiredService<TimeProvider>()));

        services.AddKeyedSingleton(ZxvServiceKeys.Root, (sp, _) =>
            sp.GetRequiredService<ZxvPlatform>().CreateRootCapability(options.RootRights));

        foreach ((string name, CapabilityRights rights) in options.Capabilities)
        {
            services.AddKeyedSingleton(name, (sp, _) =>
            {
                Capability root = sp.GetRequiredKeyedService<Capability>(ZxvServiceKeys.Root);
                return root.Attenuate(rights);
            });
        }

        services.AddSingleton(sp => new ZxvCapabilities(sp, options.Capabilities.Keys));
        services.AddHostedService<ZxvHostedService>();

        services.Configure<JsonOptions>(o => AddConverters(o.SerializerOptions.Converters));
        services.Configure<Microsoft.AspNetCore.Mvc.JsonOptions>(o => AddConverters(o.JsonSerializerOptions.Converters));
        services.AddProblemDetails();
        return services;
    }

    private static void AddConverters(System.Collections.Generic.IList<System.Text.Json.Serialization.JsonConverter> converters)
    {
        if (!converters.OfType<RationalJsonConverter>().Any())
        {
            converters.Add(new RationalJsonConverter());
        }
    }
}

/// <summary>Service keys private to this package.</summary>
internal static class ZxvServiceKeys
{
    /// <summary>The root capability's key: an object no application code holds, so the root cannot be injected.</summary>
    internal static readonly object Root = new();
}
