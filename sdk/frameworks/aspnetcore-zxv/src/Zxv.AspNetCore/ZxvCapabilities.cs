// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Extensions.DependencyInjection;

namespace Zxv.AspNetCore;

/// <summary>
/// The named capabilities of the hosted platform (never the root). Prefer
/// injecting one directly with <c>[FromKeyedServices("name")] Capability</c>;
/// this registry is for code that needs to enumerate them, such as the
/// health check.
/// </summary>
public sealed class ZxvCapabilities
{
    private readonly IServiceProvider _services;

    internal ZxvCapabilities(IServiceProvider services, IEnumerable<string> names)
    {
        _services = services;
        Names = names.OrderBy(n => n, StringComparer.Ordinal).ToArray();
    }

    /// <summary>Gets the configured names, in ordinal order.</summary>
    public IReadOnlyList<string> Names { get; }

    /// <summary>Gets the capability registered under <paramref name="name"/>.</summary>
    /// <param name="name">A configured name.</param>
    /// <returns>The capability.</returns>
    /// <exception cref="KeyNotFoundException">No capability has that name.</exception>
    public Capability Get(string name)
    {
        if (!Names.Contains(name, StringComparer.Ordinal))
        {
            throw new KeyNotFoundException($"No ZXV capability is named '{name}'.");
        }

        return _services.GetRequiredKeyedService<Capability>(name);
    }
}
