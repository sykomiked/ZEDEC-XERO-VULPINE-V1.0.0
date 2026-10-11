// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Extensions.Options;

namespace Zxv;

/// <summary>
/// Configuration of a <see cref="ZxvPlatform"/>. Bind it from the <c>Zxv</c>
/// configuration section with <c>services.AddZxv(configuration.GetSection("Zxv"))</c>.
/// </summary>
/// <example>
/// <code language="json">
/// "Zxv": {
///   "Currencies": [ "NGN", "GHS", "KES", "XOF", "ZAR" ],
///   "PrivateUnits": [ { "Code": "VFV", "MinorUnits": 2 } ],
///   "IdentifierPrefix": "ZXV"
/// }
/// </code>
/// </example>
public sealed class ZxvOptions
{
    /// <summary>The default configuration section name.</summary>
    public const string SectionName = "Zxv";

    /// <summary>Gets ISO 4217 alpha codes to enable at start-up. Minor units come from ISO 4217.</summary>
    public IList<string> Currencies { get; } = new List<string>();

    /// <summary>
    /// Gets expected minor units per currency. On native builds without the
    /// ISO 4217 table these are required; otherwise they are asserted against the table.
    /// </summary>
    public IDictionary<string, byte> MinorUnits { get; } = new Dictionary<string, byte>(StringComparer.Ordinal);

    /// <summary>Gets platform-private units (e.g. VFV) to register. They never appear on the wire.</summary>
    public IList<PrivateUnitOptions> PrivateUnits { get; } = new List<PrivateUnitOptions>();

    /// <summary>
    /// Gets or sets the prefix of generated MsgId/EndToEndId values and of
    /// external account identifiers (1 to 8 characters, <c>[A-Z0-9]</c>). Default <c>ZXV</c>.
    /// </summary>
    public string IdentifierPrefix { get; set; } = "ZXV";
}

/// <summary>A platform-private unit.</summary>
public sealed class PrivateUnitOptions
{
    /// <summary>Gets or sets the unit code (3 to 8 upper-case letters and digits).</summary>
    public string Code { get; set; } = string.Empty;

    /// <summary>Gets or sets the number of decimal places.</summary>
    public byte MinorUnits { get; set; }
}

/// <summary>Validates <see cref="ZxvOptions"/> at start-up.</summary>
public sealed class ZxvOptionsValidator : IValidateOptions<ZxvOptions>
{
    /// <inheritdoc/>
    public ValidateOptionsResult Validate(string? name, ZxvOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var failures = new List<string>();
        foreach (string c in options.Currencies)
        {
            if (c is null || c.Length != 3 || !c.All(char.IsAsciiLetterUpper))
            {
                failures.Add($"Currencies: '{c}' is not a 3-letter upper-case ISO 4217 alpha code.");
            }
        }

        if (options.Currencies.Distinct(StringComparer.Ordinal).Count() != options.Currencies.Count)
        {
            failures.Add("Currencies: duplicate entries.");
        }

        foreach (PrivateUnitOptions p in options.PrivateUnits)
        {
            if (string.IsNullOrEmpty(p.Code) || p.Code.Length is < 3 or > 8 || !p.Code.All(ch => char.IsAsciiLetterUpper(ch) || char.IsAsciiDigit(ch)))
            {
                failures.Add($"PrivateUnits: '{p.Code}' must be 3 to 8 upper-case letters and digits.");
            }

            if (p.MinorUnits > 8)
            {
                failures.Add($"PrivateUnits: '{p.Code}' minor units must be 0 to 8.");
            }
        }

        string prefix = options.IdentifierPrefix ?? string.Empty;
        if (prefix.Length is < 1 or > 8 || !prefix.All(ch => char.IsAsciiLetterUpper(ch) || char.IsAsciiDigit(ch)))
        {
            failures.Add("IdentifierPrefix must be 1 to 8 upper-case letters and digits.");
        }

        return failures.Count == 0 ? ValidateOptionsResult.Success : ValidateOptionsResult.Fail(failures);
    }
}
