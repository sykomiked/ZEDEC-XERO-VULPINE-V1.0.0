// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Sdk.Tests.Abi11;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>Helpers for the ABI 1.5 conformance vectors (64-bit numbers there are decimal strings).</summary>
internal static class V
{
    internal static JsonElement Group(string name) => Zxv.Sdk.Tests.Abi11.Conformance.Vectors.GetProperty(name);

    internal static ulong U64(JsonElement e) => e.ValueKind == JsonValueKind.String ? ulong.Parse(e.GetString()!, CultureInfo.InvariantCulture) : e.GetUInt64();

    internal static long I64(JsonElement e) => e.ValueKind == JsonValueKind.String ? long.Parse(e.GetString()!, CultureInfo.InvariantCulture) : e.GetInt64();

    internal static byte[] Hex(JsonElement e) => Convert.FromHexString(e.GetString()!);

    internal static string ToHex(ReadOnlySpan<byte> b) => Zxv.Sdk.Tests.Abi11.Conformance.ToHex(b);

    internal static byte[] Utf8(JsonElement e) => Encoding.UTF8.GetBytes(e.GetString()!);

    internal static string Str(JsonElement e, string name) => e.GetProperty(name).GetString()!;

    internal static bool Has(JsonElement e, string name) => e.TryGetProperty(name, out JsonElement v) && v.ValueKind != JsonValueKind.Null;

    internal static byte[] Ramp(int length)
    {
        var b = new byte[length];
        for (int i = 0; i < length; i++)
        {
            b[i] = (byte)(i & 255);
        }

        return b;
    }

    /// <summary>The C name of a status, e.g. ZXV_E_CID.</summary>
    internal static string Name(ZxvStatus s) => NativeText.StatusName((int)s);

    internal static ZxvStatus Status(string name) => Enum.GetValues<ZxvStatus>().First(s => Name(s) == name);

    /// <summary>Runs <paramref name="action"/> and returns the C name of the status it ended with.</summary>
    internal static string StatusOf(Action action)
    {
        try
        {
            action();
            return "ZXV_OK";
        }
        catch (ZxvException e)
        {
            return Name(e.Status);
        }
        catch (ZxvFatalException e)
        {
            return Name(e.Status);
        }
    }

    /// <summary>Asserts that <paramref name="action"/> ends with the vector's status; returns whether it succeeded.</summary>
    internal static bool Expect(JsonElement vector, Action action) => Expect(Str(vector, "status"), action);

    internal static bool Expect(string status, Action action)
    {
        string got = StatusOf(action);
        Assert.Equal(status, got);
        return status == "ZXV_OK";
    }

    /// <summary>A sparse holding [[tier,"count"],...] as 64 counts.</summary>
    internal static ulong[] Holding(JsonElement sparse)
    {
        var c = new ulong[64];
        foreach (JsonElement pair in sparse.EnumerateArray())
        {
            c[pair[0].GetInt32()] = U64(pair[1]);
        }

        return c;
    }
}
