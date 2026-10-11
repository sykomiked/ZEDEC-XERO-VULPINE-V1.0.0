// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Extensions.Options;

namespace Zxv.AspNetCore;

/// <summary>
/// Validates <see cref="ZxvAspNetCoreOptions"/>. Misconfiguration fails at
/// registration, never by silently granting less (or more) than asked.
/// </summary>
public sealed class ZxvAspNetCoreOptionsValidator : IValidateOptions<ZxvAspNetCoreOptions>
{
    /// <inheritdoc/>
    public ValidateOptionsResult Validate(string? name, ZxvAspNetCoreOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var failures = new List<string>();

        ValidateOptionsResult platform = new ZxvOptionsValidator().Validate(name, options.Platform);
        if (platform.Failed)
        {
            failures.AddRange(platform.Failures);
        }

        if ((options.RootRights & ~CapabilityRights.All) != 0)
        {
            failures.Add("RootRights: unknown right bits.");
        }

        foreach (KeyValuePair<string, CapabilityRights> kv in options.Capabilities)
        {
            if (string.IsNullOrWhiteSpace(kv.Key) || !kv.Key.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.'))
            {
                failures.Add($"Capabilities: '{kv.Key}' must be letters, digits, '-', '_' or '.'.");
            }

            if (kv.Value == CapabilityRights.None)
            {
                failures.Add($"Capabilities:{kv.Key} holds no rights.");
            }

            CapabilityRights missing = kv.Value & ~options.RootRights;
            if (missing != CapabilityRights.None)
            {
                failures.Add($"Capabilities:{kv.Key} asks for {missing}, which RootRights does not hold.");
            }
        }

        return failures.Count == 0 ? ValidateOptionsResult.Success : ValidateOptionsResult.Fail(failures);
    }
}
