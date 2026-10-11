// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Globalization;
using System.Security.Cryptography;

namespace Zxv;

/// <summary>Generates ISO 20022 identifiers (at most 35 characters, <c>[A-Z0-9-]</c>).</summary>
internal sealed class IdGenerator
{
    private readonly string _prefix;
    private readonly TimeProvider _time;

    internal IdGenerator(string prefix, TimeProvider time)
    {
        _prefix = prefix;
        _time = time;
    }

    /// <summary>MsgId: prefix + UTC timestamp + 48 random bits, e.g. <c>ZXV20261009120000A1B2C3D4E5F6</c>.</summary>
    internal string NextMessageId() => Next(string.Empty);

    /// <summary>EndToEndId: like a MsgId, with an <c>E</c> marker.</summary>
    internal string NextEndToEndId() => Next("E");

    private string Next(string marker)
    {
        Span<byte> random = stackalloc byte[6];
        RandomNumberGenerator.Fill(random);
        string stamp = _time.GetUtcNow().UtcDateTime.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture);
        string id = _prefix + marker + stamp + Convert.ToHexString(random);
        return id.Length <= 35 ? id : id[..35];
    }
}
