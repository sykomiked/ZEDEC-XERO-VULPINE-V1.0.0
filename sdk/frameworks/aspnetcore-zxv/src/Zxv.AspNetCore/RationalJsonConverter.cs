// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Zxv.AspNetCore;

/// <summary>
/// System.Text.Json converter for <see cref="Rational"/>, exact both ways.
/// </summary>
/// <remarks>
/// <para>Writes the exact string: <c>"12.75"</c>, or <c>"1/3"</c> when there is no terminating decimal. Never a JSON number.</para>
/// <para>
/// Reads a string in the SDK's exact syntax (<c>"12.75"</c>, <c>"-0.1"</c>,
/// <c>"3/4"</c>, <c>"42"</c>), or a JSON integer that fits in 64 bits. A JSON
/// number with a fraction or exponent (<c>0.1</c>, <c>1.0</c>, <c>1e3</c>) is
/// always refused, even when the serializer allows reading numbers from
/// strings: the client may already have rounded it through a binary double.
/// Failures are <see cref="JsonException"/>s, which ASP.NET Core answers with 400.
/// </para>
/// <para><c>Rational?</c> works too: System.Text.Json wraps this converter for nullable values.</para>
/// </remarks>
public sealed class RationalJsonConverter : JsonConverter<Rational>
{
    /// <inheritdoc/>
    public override Rational Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        switch (reader.TokenType)
        {
            case JsonTokenType.String:
                string? text = reader.GetString();
                if (Rational.TryParse(text, out Rational value))
                {
                    return value;
                }

                throw new JsonException("Not an exact rational; use \"12.75\", \"-0.1\", \"3/4\" or \"42\".");
            case JsonTokenType.Number:
                ReadOnlySpan<byte> raw = reader.HasValueSequence ? reader.ValueSequence.ToArray() : reader.ValueSpan;
                if (raw.IndexOfAny("."u8) >= 0 || raw.IndexOfAny("eE"u8) >= 0)
                {
                    throw new JsonException("JSON floating-point numbers are refused for exact values; send a string such as \"0.1\" or \"1/3\".");
                }

                if (!reader.TryGetInt64(out long whole))
                {
                    throw new JsonException("The integer does not fit in 64 bits.");
                }

                return Rational.Of(whole);
            default:
                throw new JsonException($"Expected an exact rational as a JSON string, not {reader.TokenType}.");
        }
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, Rational value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        writer.WriteStringValue(value.ToString());
    }
}
