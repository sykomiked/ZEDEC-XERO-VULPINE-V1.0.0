// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using Xunit;

namespace Zedec.Zxv.Tests
{
    internal static class Vectors
    {
        internal static JsonElement Load()
        {
            DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir != null)
            {
                string p = Path.Combine(dir.FullName, "sdk", "conformance", "vectors.json");
                if (File.Exists(p)) return JsonDocument.Parse(File.ReadAllText(p)).RootElement;
                p = Path.Combine(dir.FullName, "conformance", "vectors.json");
                if (File.Exists(p)) return JsonDocument.Parse(File.ReadAllText(p)).RootElement;
                dir = dir.Parent;
            }
            throw new FileNotFoundException("sdk/conformance/vectors.json");
        }

        internal static Rational R(JsonElement pair) => Rational.Of(pair[0].GetInt64(), pair[1].GetInt64());
    }

    public class RationalTests
    {
        [Fact]
        public void ExactDecimalArithmetic()
        {
            Rational sum = Rational.Parse("0.1") + Rational.Parse("0.2");
            Assert.Equal(Rational.Parse("0.3"), sum);
            Assert.Equal("0.3", sum.ToString());
            Assert.Equal("3/10", sum.ToFractionString());
            Rational third = Rational.Of(1, 3);
            Assert.Equal(Rational.One, third + third + third);
            Assert.Equal("1/3", third.ToString());
        }

        [Fact]
        public void DefaultIsZeroAndReduces()
        {
            Rational z = default;
            Assert.Equal(0, z.Numerator);
            Assert.Equal(1, z.Denominator);
            Assert.Equal(Rational.Zero, z);
            Assert.Equal(z, Rational.Of(0, 5));
            Rational r = Rational.Of(2, -6);
            Assert.Equal(-1, r.Numerator);
            Assert.Equal(3, r.Denominator);
            Assert.True(r.IsCanonical);
            Rational i = 42;
            Assert.Equal("42", i.ToString());
        }

        [Theory]
        [InlineData("1e3")]
        [InlineData("NaN")]
        [InlineData(" 1")]
        [InlineData("0.1f")]
        [InlineData("")]
        public void ParseRefusesNonExactText(string text)
        {
            ZxvException e = Assert.Throws<ZxvException>(() => Rational.Parse(text));
            Assert.Equal(ZxvStatus.Format, e.Status);
            Assert.False(Rational.TryParse(text, out _));
        }

        [Fact]
        public void OverflowAndZeroDivisionThrowNeverWrap()
        {
            Rational big = long.MaxValue;
            Assert.Equal(ZxvStatus.Range, Assert.Throws<ZxvException>(() => big + 1).Status);
            Assert.Equal(ZxvStatus.Range, Assert.Throws<ZxvException>(() => big * 2).Status);
            Assert.Equal(ZxvStatus.Arg, Assert.Throws<ZxvException>(() => Rational.One / Rational.Zero).Status);
            Assert.Equal(ZxvStatus.Arg, Assert.Throws<ZxvException>(() => Rational.Of(1, 0)).Status);
        }

        [Fact]
        public void ComparisonIsExact()
        {
            Assert.True(Rational.Of(1, 3) < Rational.Of(1, 2));
            Assert.True(Rational.Of(2, 4) == Rational.Of(1, 2));
            Assert.True(Rational.Of(-1, 2) <= Rational.Zero);
            Assert.Equal(0, Rational.Of(3, 9).CompareTo(Rational.Of(1, 3)));
        }

        [Fact]
        public void NoFloatEntersTheType()
        {
            // The struct stores two longs and nothing else: no float field can be serialized.
            FieldInfo[] fields = typeof(Rational).GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
            Assert.Equal(new[] { "den", "num" }, fields.Select(f => f.Name).OrderBy(n => n).ToArray());
            Assert.All(fields, f => Assert.Equal(typeof(long), f.FieldType));
            // No public member takes a float or a double (only ToFloatLossy returns one).
            foreach (MethodInfo m in typeof(Rational).GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance))
            {
                Assert.DoesNotContain(m.GetParameters(), p => p.ParameterType == typeof(float) || p.ParameterType == typeof(double));
                if (m.ReturnType == typeof(float) || m.ReturnType == typeof(double))
                    Assert.Equal(nameof(Rational.ToFloatLossy), m.Name);
            }
            foreach (ConstructorInfo c in typeof(Rational).GetConstructors(BindingFlags.Public | BindingFlags.Instance))
                Assert.DoesNotContain(c.GetParameters(), p => p.ParameterType == typeof(float) || p.ParameterType == typeof(double));
            // Lossy conversion exists only for drawing.
            float shown = Rational.Of(1, 3).ToFloatLossy();
            Assert.InRange(shown, 0.333f, 0.334f);
        }

        [Fact]
        public void HandEditedAssetIsCaught()
        {
            // A serialized 5/0 (hand-edited YAML) must not read as 5/1.
            Rational bad = (Rational)typeof(Rational).GetMethod("Of")!.Invoke(null, new object[] { 5L, 1L })!;
            object boxed = bad;
            typeof(Rational).GetField("den", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(boxed, 0L);
            bad = (Rational)boxed;
            Assert.False(bad.IsCanonical);
            Assert.Throws<ZxvException>(() => bad + Rational.One);
        }

        [Fact]
        public void ConformanceRationalOps()
        {
            JsonElement v = Vectors.Load();
            int n = 0;
            foreach (JsonElement e in v.GetProperty("rational_ops").EnumerateArray())
            {
                Rational a = Vectors.R(e.GetProperty("a")), b = Vectors.R(e.GetProperty("b"));
                Assert.Equal(Vectors.R(e.GetProperty("add")), a + b);
                Assert.Equal(Vectors.R(e.GetProperty("sub")), a - b);
                Assert.Equal(Vectors.R(e.GetProperty("mul")), a * b);
                Assert.Equal(Vectors.R(e.GetProperty("div")), a / b);
                Assert.Equal(e.GetProperty("cmp").GetInt32(), a.CompareTo(b));
                n++;
            }
            Assert.True(n > 0);
        }

        [Fact]
        public void ConformanceRationalParse()
        {
            JsonElement v = Vectors.Load();
            foreach (JsonElement e in v.GetProperty("rational_parse").EnumerateArray())
            {
                string text = e.GetProperty("text").GetString()!;
                int status = e.GetProperty("status").GetInt32();
                if (status == 0)
                {
                    Rational r = Rational.Parse(text);
                    Assert.Equal(Vectors.R(e.GetProperty("value")), r);
                    Assert.Equal(e.GetProperty("format").GetString(), r.ToString());
                }
                else
                {
                    Assert.Equal(status, Assert.Throws<ZxvException>(() => Rational.Parse(text)).Code);
                }
            }
        }
    }
}
