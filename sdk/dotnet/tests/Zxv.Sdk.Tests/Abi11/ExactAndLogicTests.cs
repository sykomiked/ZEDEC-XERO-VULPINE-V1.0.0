// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text.Json;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>Status classes, exact rationals and four-valued logic against vectors.json.</summary>
public sealed class ExactAndLogicTests
{
    [Fact]
    public void Library_is_abi_1_1_or_later()
    {
        Assert.Equal(1, ZxvLibrary.AbiVersion.Major);
        Assert.True(ZxvLibrary.AbiVersion.Minor >= 1);
        int[] abi = Conformance.Ints(Conformance.Vectors.GetProperty("abi"));
        Assert.Equal(abi[0], ZxvLibrary.AbiVersion.Major);
        Assert.True(ZxvLibrary.Supports(ZxvFeatures.Rational | ZxvFeatures.FourValuedLogic | ZxvFeatures.Capabilities));
        Assert.False(ZxvLibrary.Supports(ZxvFeatures.Lora));
    }

    [Fact]
    public void Status_names_and_classes_match_vectors()
    {
        foreach (JsonElement s in Conformance.Vectors.GetProperty("status").EnumerateArray())
        {
            var status = (ZxvStatus)s.GetProperty("code").GetInt32();
            Assert.Equal(s.GetProperty("name").GetString(), ZxvLibrary.GetStatusName(status));
            Assert.Equal((ZxvStatusClass)s.GetProperty("class").GetInt32(), ZxvLibrary.ClassOf(status));
            Assert.True(Enum.IsDefined(status), $"ZxvStatus lacks {status}");
        }
    }

    [Fact]
    public void Exceptions_follow_the_status_class()
    {
        foreach (JsonElement s in Conformance.Vectors.GetProperty("status").EnumerateArray())
        {
            int code = s.GetProperty("code").GetInt32();
            if (code == 0)
            {
                continue;
            }

            Exception ex = NativeCall.Create(code, "Test");
            switch ((ZxvStatusClass)s.GetProperty("class").GetInt32())
            {
                case ZxvStatusClass.Usage:
                    Assert.IsAssignableFrom<ZxvUsageException>(ex);
                    break;
                case ZxvStatusClass.Refused:
                    Assert.IsAssignableFrom<ZxvRefusedException>(ex);
                    break;
                case ZxvStatusClass.Fatal:
                    Assert.IsType<ZxvFatalException>(ex);
                    Assert.False(ex is ZxvException, "ZxvFatalException must stay outside the ZxvException family");
                    break;
                case ZxvStatusClass.System:
                    Assert.IsType<ZxvSystemException>(ex);
                    break;
            }
        }

        Assert.IsType<CapabilityDeniedException>(NativeCall.Create(-17, "T"));
        Assert.IsType<BudgetExhaustedException>(NativeCall.Create(-19, "T"));
        Assert.IsType<InsufficientFundsException>(NativeCall.Create(-6, "T"));
        Assert.IsType<VerificationFailedException>(NativeCall.Create(-21, "T"));
        Assert.IsType<ZxvFeatureNotAvailableException>(NativeCall.Create(-3, "T"));
        Assert.IsType<FloatRejectedException>(NativeCall.Create(-24, "T"));
        Assert.IsType<ZxvSystemException>(NativeCall.Create(-77, "T")); // unknown negative: SYSTEM
        var refused = (ZxvRefusedException)NativeCall.Create(-19, "Budget.Consume");
        Assert.Equal("ZXV_E_BUDGET", refused.StatusName);
        Assert.Equal(ZxvStatusClass.Refused, refused.StatusClass);
    }

    [Fact]
    public void Rational_ops_match_vectors()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("rational_ops").EnumerateArray())
        {
            Rational a = Conformance.Rat(v.GetProperty("a")), b = Conformance.Rat(v.GetProperty("b"));
            Assert.Equal(Conformance.Rat(v.GetProperty("add")), a + b);
            Assert.Equal(Conformance.Rat(v.GetProperty("sub")), a - b);
            Assert.Equal(Conformance.Rat(v.GetProperty("mul")), a * b);
            Assert.Equal(Conformance.Rat(v.GetProperty("div")), a / b);
            Assert.Equal(v.GetProperty("cmp").GetInt32(), a.CompareTo(b));
        }
    }

    [Fact]
    public void Rational_parse_and_format_match_vectors()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("rational_parse").EnumerateArray())
        {
            string text = v.GetProperty("text").GetString()!;
            int status = v.GetProperty("status").GetInt32();
            if (status == 0)
            {
                Rational r = Rational.Parse(text);
                Assert.Equal(Conformance.Rat(v.GetProperty("value")), r);
                Assert.Equal(v.GetProperty("format").GetString(), r.ToString());
                Assert.Equal(r, Rational.Parse(r.ToString()));
            }
            else
            {
                var ex = Assert.ThrowsAny<ZxvUsageException>(() => Rational.Parse(text));
                Assert.Equal((ZxvStatus)status, ex.Status);
                Assert.False(Rational.TryParse(text, out _));
            }
        }
    }

    [Fact]
    public void Ieee754_boundary_matches_vectors()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("ieee754").EnumerateArray())
        {
            ulong bits = Convert.ToUInt64(v.GetProperty("bits").GetString(), 16);
            double d = BitConverter.Int64BitsToDouble(unchecked((long)bits));
            int status = v.GetProperty("status").GetInt32();
            if (status == 0)
            {
                Assert.Equal(Conformance.Rat(v.GetProperty("value")), Rational.FromDoubleExact(d));
                Assert.Equal(Conformance.Rat(v.GetProperty("value")), Rational.FromIeee754Bits(bits));
            }
            else if (status == (int)ZxvStatus.FloatRejected)
            {
                Assert.Throws<FloatRejectedException>(() => Rational.FromDoubleExact(d));
            }
            else
            {
                Assert.Equal((ZxvStatus)status, Assert.ThrowsAny<ZxvUsageException>(() => Rational.FromDoubleExact(d)).Status);
            }
        }

        // The drift is visible: 0.1 is not 1/10.
        Assert.NotEqual(Rational.Of(1, 10), Rational.FromDoubleExact(0.1));
        Assert.Equal(Rational.Of(1, 10), Rational.FromDecimal(0.1m));
    }

    [Fact]
    public void Q16_matches_vectors()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("q16").EnumerateArray())
        {
            Rational r = Conformance.Rat(v.GetProperty("value"));
            if (v.GetProperty("status").GetInt32() == 0)
            {
                Assert.Equal(v.GetProperty("q16").GetInt32(), r.ToQ16());
            }
            else
            {
                Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.ThrowsAny<ZxvUsageException>(() => r.ToQ16()).Status);
            }
        }
    }

    [Fact]
    public void Rational_is_exact_and_never_wraps()
    {
        Assert.Equal(Rational.Zero, default);
        Assert.Equal(1, default(Rational).Denominator);
        Assert.Equal(Rational.Of(3, 10), Rational.Of(1, 10) + Rational.Of(2, 10));
        Assert.Equal(Rational.Of(-3, 2), Rational.Of(6, -4));
        Assert.Equal(Rational.Of(51, 4), Rational.FromDecimal(12.750m));
        Assert.Equal(Rational.Of(-1, 1_000_000), Rational.FromDecimal(-0.000001m));
        Assert.Equal("1/3", Rational.Of(1, 3).ToString());
        Assert.True(Rational.Of(1, 3) > Rational.Parse("0.333333"));
        Rational three = 3L;
        Assert.Equal(Rational.Of(3), three);
        Assert.Throws<DivideByZeroException>(() => Rational.Of(1, 0));
        Assert.Throws<DivideByZeroException>(() => Rational.One / Rational.Zero);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.ThrowsAny<ZxvUsageException>(() => Rational.Of(long.MaxValue) * Rational.Of(2)).Status);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.ThrowsAny<ZxvUsageException>(() => Rational.Of(long.MinValue)).Status);
        Assert.ThrowsAny<ZxvUsageException>(() => Rational.FromDecimal(0.0000000000000000000000000001m));
        Rational[] sorted = new[] { Rational.Of(1, 2), Rational.Of(-1, 3), Rational.Of(1, 3) }.OrderBy(r => r).ToArray();
        Assert.Equal(new[] { Rational.Of(-1, 3), Rational.Of(1, 3), Rational.Of(1, 2) }, sorted);
    }

    [Fact]
    public void Truth_tables_match_vectors()
    {
        JsonElement lp = Conformance.Vectors.GetProperty("lpres");
        string[] names = lp.GetProperty("names").EnumerateArray().Select(e => e.GetString()!).ToArray();
        int[] not = Conformance.Ints(lp.GetProperty("not"));
        for (int a = 0; a < 4; a++)
        {
            Assert.Equal(names[a], ((Truth)a).Name());
            Assert.Equal((Truth)not[a], ((Truth)a).Not());
            for (int b = 0; b < 4; b++)
            {
                Assert.Equal((Truth)lp.GetProperty("and")[a][b].GetInt32(), ((Truth)a).And((Truth)b));
                Assert.Equal((Truth)lp.GetProperty("or")[a][b].GetInt32(), ((Truth)a).Or((Truth)b));
            }
        }

        Assert.Equal(Truth.Both, TruthExtensions.FromEvidence(2, 1));
        Assert.Equal(Truth.True, TruthExtensions.FromEvidence(1, 0));
        Assert.Equal(Truth.False, TruthExtensions.FromEvidence(0, 3));
        Assert.Equal(Truth.Neither, TruthExtensions.FromEvidence(0, 0));
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.ThrowsAny<ZxvUsageException>(() => ((Truth)7).Not()).Status);
    }

    [Fact]
    public void Truth_needs_an_explicit_boolean_policy()
    {
        Assert.True(Truth.True.IsCertainlyTrue());
        Assert.False(Truth.Both.IsCertainlyTrue());
        Assert.True(Truth.Both.IsContradiction());
        Assert.True(Truth.Both.ToBoolean(onBoth: true, onNeither: false));
        Assert.False(Truth.Neither.ToBoolean(onBoth: true, onNeither: false));
        Assert.False(Truth.Neither.IsCertain());
        Truth.True.RequireTrue();
        Assert.Contains("BOTH", Assert.Throws<InvalidOperationException>(() => Truth.Both.RequireTrue("gate")).Message, StringComparison.Ordinal);
    }
}
