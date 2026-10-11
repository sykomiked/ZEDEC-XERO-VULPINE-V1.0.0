// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Text.Json;

namespace Zxv.AspNetCore.Tests;

/// <summary>Rational in System.Text.Json: exact strings out, exact strings or integers in, floats refused.</summary>
public sealed class JsonTests
{
    private sealed record Order(Rational Price, Rational? Discount);

    private static readonly JsonSerializerOptions Options = new()
    {
        Converters = { new RationalJsonConverter() },
        // Even with the switch that reads numbers from strings, floats stay refused.
        NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowReadingFromString,
    };

    [Fact]
    public void Writes_the_exact_string()
    {
        Assert.Equal("\"0.3\"", JsonSerializer.Serialize(Rational.Parse("0.1") + Rational.Parse("0.2"), Options));
        Assert.Equal("\"1/3\"", JsonSerializer.Serialize(Rational.Of(1, 3), Options));
        Assert.Equal("\"-12.75\"", JsonSerializer.Serialize(Rational.Of(-51, 4), Options));
        Assert.Equal("{\"Price\":\"42\",\"Discount\":null}", JsonSerializer.Serialize(new Order(42, null), Options));
    }

    [Fact]
    public void Reads_exact_strings_and_integers()
    {
        Assert.Equal(Rational.Of(1, 10), JsonSerializer.Deserialize<Rational>("\"0.1\"", Options));
        Assert.Equal(Rational.Of(3, 4), JsonSerializer.Deserialize<Rational>("\"3/4\"", Options));
        Assert.Equal(Rational.Of(42), JsonSerializer.Deserialize<Rational>("42", Options));
        Assert.Equal(Rational.Of(-7), JsonSerializer.Deserialize<Rational>("-7", Options));
        Order o = JsonSerializer.Deserialize<Order>("{\"Price\":\"12.75\",\"Discount\":\"1/4\"}", Options)!;
        Assert.Equal(Rational.Of(25, 2), o.Price - o.Discount!.Value);
        Assert.Equal(o, JsonSerializer.Deserialize<Order>(JsonSerializer.Serialize(o, Options), Options));
        Assert.Null(JsonSerializer.Deserialize<Order>("{\"Price\":\"1\",\"Discount\":null}", Options)!.Discount);
    }

    public static IEnumerable<object[]> Floats => new[]
    {
        new object[] { "0.1" }, new object[] { "1.0" }, new object[] { "1e3" }, new object[] { "-0.0" },
        new object[] { "12.75" }, new object[] { "1E-2" },
    };

    [Theory]
    [MemberData(nameof(Floats))]
    public void Refuses_json_floats(string json)
    {
        JsonException e = Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Rational>(json, Options));
        Assert.Contains("floating-point numbers are refused", e.Message, System.StringComparison.Ordinal);
        Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Order>("{\"Price\":\"1\",\"Discount\":" + json + "}", Options));
    }

    [Theory]
    [InlineData("\"1e3\"")]
    [InlineData("\"NaN\"")]
    [InlineData("\"Infinity\"")]
    [InlineData("\" 1\"")]
    [InlineData("\".5\"")]
    [InlineData("\"1/0\"")]
    [InlineData("\"abc\"")]
    [InlineData("\"\"")]
    [InlineData("123456789012345678901234567890")]
    [InlineData("true")]
    [InlineData("[1]")]
    [InlineData("{}")]
    public void Refuses_inexact_or_malformed_input(string json)
    {
        Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Rational>(json, Options));
    }
}
