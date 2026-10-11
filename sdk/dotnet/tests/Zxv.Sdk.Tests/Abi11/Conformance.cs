// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>The cross-SDK answer key, <c>sdk/conformance/vectors.json</c>, and the engine fixtures.</summary>
internal static class Conformance
{
    private static readonly Lazy<string> Root = new(FindRoot);
    private static readonly Lazy<JsonDocument> Doc = new(() => JsonDocument.Parse(File.ReadAllBytes(Path.Combine(Root.Value, "vectors.json"))));

    internal static JsonElement Vectors => Doc.Value.RootElement;

    internal static string Fixture(string name) => Path.Combine(Root.Value, "fixtures", name);

    internal static byte[] Hex(string hex) => Convert.FromHexString(hex);

    internal static string ToHex(ReadOnlySpan<byte> bytes) => Convert.ToHexString(bytes).ToLowerInvariant();

    internal static int[] Ints(JsonElement array) => array.EnumerateArray().Select(e => e.GetInt32()).ToArray();

    internal static Rational Rat(JsonElement pair) => Rational.Of(pair[0].GetInt64(), pair[1].GetInt64());

    private static string FindRoot()
    {
        for (DirectoryInfo? d = new(AppContext.BaseDirectory); d is not null; d = d.Parent)
        {
            string candidate = Path.Combine(d.FullName, "sdk", "conformance");
            if (File.Exists(Path.Combine(candidate, "vectors.json")))
            {
                return candidate;
            }

            candidate = Path.Combine(d.FullName, "conformance");
            if (File.Exists(Path.Combine(candidate, "vectors.json")))
            {
                return candidate;
            }
        }

        throw new FileNotFoundException("sdk/conformance/vectors.json not found above " + AppContext.BaseDirectory);
    }
}
