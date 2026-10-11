// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Globalization;
using Microsoft.Extensions.Configuration;

namespace Zxv.AspNetCore;

/// <summary>
/// Configuration of the hosted ZXV platform. Set it in code, or read the
/// <c>Zxv</c> configuration section with <see cref="ReadFrom(IConfiguration)"/>.
/// </summary>
/// <example>
/// <code language="json">
/// "Zxv": {
///   "Currencies": [ "NGN" ],
///   "PrivateUnits": [ { "Code": "VFV", "MinorUnits": 2 } ],
///   "RootRights": "LedgerRead, LedgerPost",
///   "Capabilities": { "teller": "LedgerRead, LedgerPost", "auditor": "LedgerRead" }
/// }
/// </code>
/// </example>
public sealed class ZxvAspNetCoreOptions
{
    /// <summary>The default configuration section name.</summary>
    public const string SectionName = "Zxv";

    /// <summary>Gets the platform options (currencies, private units, identifier prefix) of <c>Zxv.Sdk</c>.</summary>
    public ZxvOptions Platform { get; } = new();

    /// <summary>
    /// Gets or sets the rights of the root capability. Every named capability
    /// is attenuated from it, so it must hold all of theirs. The root is never
    /// injectable. Default <see cref="CapabilityRights.All"/>.
    /// </summary>
    public CapabilityRights RootRights { get; set; } = CapabilityRights.All;

    /// <summary>
    /// Gets the named capabilities to register as keyed singletons: inject one
    /// with <c>[FromKeyedServices("teller")] Capability teller</c>.
    /// </summary>
    public IDictionary<string, CapabilityRights> Capabilities { get; } = new Dictionary<string, CapabilityRights>(StringComparer.Ordinal);

    /// <summary>
    /// Gets or sets a value indicating whether a halted (poisoned) context
    /// stops the application (<c>IHostApplicationLifetime.StopApplication</c>)
    /// so an orchestrator restarts it from trusted state. Default
    /// <see langword="false"/>: the health check reports Unhealthy and every
    /// ZXV call answers 503.
    /// </summary>
    public bool StopApplicationOnFatal { get; set; }

    /// <summary>
    /// Reads the options from a configuration section (usually
    /// <c>configuration.GetSection("Zxv")</c>). Values present in the section
    /// replace or extend what is already set; absent keys are left alone.
    /// No reflection: safe for trimmed and AOT applications.
    /// </summary>
    /// <param name="section">The section.</param>
    /// <returns>These options, for chaining.</returns>
    /// <exception cref="FormatException">A rights value or number is malformed.</exception>
    public ZxvAspNetCoreOptions ReadFrom(IConfiguration section)
    {
        ArgumentNullException.ThrowIfNull(section);

        foreach (IConfigurationSection c in section.GetSection("Currencies").GetChildren())
        {
            if (!string.IsNullOrEmpty(c.Value) && !Platform.Currencies.Contains(c.Value))
            {
                Platform.Currencies.Add(c.Value);
            }
        }

        foreach (IConfigurationSection m in section.GetSection("MinorUnits").GetChildren())
        {
            Platform.MinorUnits[m.Key] = ParseByte(m.Value, "Zxv:MinorUnits:" + m.Key);
        }

        foreach (IConfigurationSection p in section.GetSection("PrivateUnits").GetChildren())
        {
            Platform.PrivateUnits.Add(new PrivateUnitOptions
            {
                Code = p["Code"] ?? string.Empty,
                MinorUnits = ParseByte(p["MinorUnits"], "Zxv:PrivateUnits:" + p.Key + ":MinorUnits"),
            });
        }

        if (section["IdentifierPrefix"] is { } prefix)
        {
            Platform.IdentifierPrefix = prefix;
        }

        if (section["RootRights"] is { } root)
        {
            RootRights = ParseRights(root, "Zxv:RootRights");
        }

        foreach (IConfigurationSection cap in section.GetSection("Capabilities").GetChildren())
        {
            Capabilities[cap.Key] = ParseRights(cap.Value, "Zxv:Capabilities:" + cap.Key);
        }

        if (section["StopApplicationOnFatal"] is { } stop)
        {
            StopApplicationOnFatal = bool.Parse(stop);
        }

        return this;
    }

    /// <summary>Parses <c>"LedgerRead, LedgerPost"</c> (names only, case-insensitive; numbers are refused).</summary>
    /// <param name="text">The text.</param>
    /// <param name="key">The configuration key, for the message.</param>
    /// <returns>The rights.</returns>
    internal static CapabilityRights ParseRights(string? text, string key)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return CapabilityRights.None;
        }

        CapabilityRights rights = CapabilityRights.None;
        foreach (string part in text.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            if (part.Length == 0 || char.IsAsciiDigit(part[0]) || part[0] == '-'
                || !Enum.TryParse(part, ignoreCase: true, out CapabilityRights one) || !Enum.IsDefined(one))
            {
                throw new FormatException($"{key}: '{part}' is not a capability right ({string.Join(", ", Enum.GetNames<CapabilityRights>())}).");
            }

            rights |= one;
        }

        return rights;
    }

    private static byte ParseByte(string? text, string key) =>
        byte.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out byte b)
            ? b
            : throw new FormatException($"{key}: '{text}' is not a number from 0 to 255.");
}
