// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Options;
using Zxv;
using Zxv.Payments;

// Placed in the DI namespace by convention so `services.AddZxv()` needs no extra using.
namespace Microsoft.Extensions.DependencyInjection;

/// <summary>Registers the ZXV platform with dependency injection.</summary>
public static class ZxvServiceCollectionExtensions
{
    /// <summary>Adds <see cref="ZxvPlatform"/> as a singleton, configured in code.</summary>
    /// <param name="services">The service collection.</param>
    /// <param name="configure">Configures <see cref="ZxvOptions"/>.</param>
    /// <returns>The options builder, for chaining validation.</returns>
    public static OptionsBuilder<ZxvOptions> AddZxv(this IServiceCollection services, Action<ZxvOptions>? configure = null)
    {
        ArgumentNullException.ThrowIfNull(services);
        OptionsBuilder<ZxvOptions> builder = services.AddOptions<ZxvOptions>();
        if (configure is not null)
        {
            builder.Configure(configure);
        }

        builder.ValidateOnStart();
        services.TryAddEnumerable(ServiceDescriptor.Singleton<IValidateOptions<ZxvOptions>, ZxvOptionsValidator>());
        services.TryAddSingleton<IMxTransport>(NullMxTransport.Instance);
        services.TryAddSingleton(TimeProvider.System);
        services.TryAddSingleton(sp => new ZxvPlatform(
            sp.GetRequiredService<IOptions<ZxvOptions>>(),
            sp.GetService<Microsoft.Extensions.Logging.ILogger<ZxvPlatform>>(),
            sp.GetRequiredService<IMxTransport>(),
            sp.GetRequiredService<TimeProvider>()));
        return builder;
    }

    /// <summary>Adds <see cref="ZxvPlatform"/> as a singleton, bound to a configuration section.</summary>
    /// <param name="services">The service collection.</param>
    /// <param name="section">Usually <c>configuration.GetSection("Zxv")</c>.</param>
    /// <returns>The options builder.</returns>
    [RequiresUnreferencedCode("Binds ZxvOptions by reflection. For trimmed or AOT apps, configure options in code with AddZxv(Action<ZxvOptions>).")]
    [RequiresDynamicCode("Binds ZxvOptions by reflection. For trimmed or AOT apps, configure options in code with AddZxv(Action<ZxvOptions>).")]
    public static OptionsBuilder<ZxvOptions> AddZxv(this IServiceCollection services, IConfiguration section)
    {
        ArgumentNullException.ThrowIfNull(section);
        OptionsBuilder<ZxvOptions> builder = services.AddZxv();
        builder.Bind(section);
        return builder;
    }

    /// <summary>
    /// Registers a singleton <see cref="Capability"/> created from the
    /// platform's root and holding only <paramref name="rights"/>, so services
    /// can take a <see cref="Capability"/> constructor parameter. Grant the
    /// narrowest rights the application needs; parts of the program that need
    /// less should <see cref="Capability.Attenuate(CapabilityRights)"/> it further.
    /// Call <see cref="AddZxv(IServiceCollection, Action{ZxvOptions}?)"/> first.
    /// </summary>
    /// <param name="services">The service collection.</param>
    /// <param name="rights">The rights of the registered capability.</param>
    /// <param name="processId">The process id recorded in it.</param>
    /// <returns>The service collection.</returns>
    public static IServiceCollection AddZxvCapability(this IServiceCollection services, CapabilityRights rights, uint processId = 0)
    {
        ArgumentNullException.ThrowIfNull(services);
        services.TryAddSingleton(sp => sp.GetRequiredService<ZxvPlatform>().CreateRootCapability(rights, processId));
        return services;
    }
}
