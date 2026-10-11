// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;

namespace Zedec.Zxv
{
    /// <summary>
    /// An exact rational: a 64-bit numerator over a positive 64-bit
    /// denominator, always reduced. It is Unity-serializable and stores
    /// exactly two integer fields, <c>num</c> and <c>den</c>; there is no
    /// float field, so a scene or prefab holds the exact value and every peer
    /// loads the same bits.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Build one from integers (<see cref="Of"/>, or an implicit
    /// <see cref="long"/>) or from exact text (<see cref="Parse"/>: "12.75",
    /// "3/4", "-2"). There is no constructor from <see cref="float"/> or
    /// <see cref="double"/>: a float is not exact, and two machines must start
    /// from the same value.
    /// </para>
    /// <para>
    /// Arithmetic runs in libzxv and never rounds or wraps: an overflow
    /// throws <see cref="ZxvException"/> with <see cref="ZxvStatus.Range"/>.
    /// <c>default(Rational)</c> is zero (its stored <c>den</c> of 0 reads as 1).
    /// </para>
    /// <para>
    /// For drawing, <see cref="ToFloatLossy"/> gives the nearest float. That
    /// is LOSSY: use it at the very edge (a Transform, a UI label) and never
    /// feed the result back into game state.
    /// </para>
    /// </remarks>
    [Serializable]
    public struct Rational : IEquatable<Rational>, IComparable<Rational>
    {
#if UNITY_5_3_OR_NEWER
        [UnityEngine.SerializeField]
#endif
        private long num;

#if UNITY_5_3_OR_NEWER
        [UnityEngine.SerializeField]
#endif
        private long den;

        private Rational(long n, long d)
        {
            num = n;
            den = d;
        }

        /// <summary>Zero.</summary>
        public static Rational Zero => default;

        /// <summary>One.</summary>
        public static Rational One => new Rational(1, 1);

        /// <summary>The numerator (carries the sign).</summary>
        public long Numerator => num;

        /// <summary>The denominator (positive).</summary>
        public long Denominator => num == 0 && den == 0 ? 1 : den;

        /// <summary>True when the stored fields are reduced with a positive denominator
        /// (always, unless a serialized asset was edited by hand).</summary>
        public bool IsCanonical
        {
            get
            {
                long d = Denominator;
                if (d <= 0) return false;
                return Native.zxv_rat_normalize(num, d, out long n2, out long d2) == 0 && n2 == num && d2 == d;
            }
        }

        /// <summary>num/den, reduced. Throws <see cref="ZxvStatus.Arg"/> for a zero denominator.</summary>
        public static Rational Of(long numerator, long denominator)
        {
            ZxvException.ThrowZxv(Native.zxv_rat_normalize(numerator, denominator, out long n, out long d));
            return new Rational(n, d);
        }

        /// <summary>Parses "12.75", "-0.1", "3/4" or "42" exactly; anything else
        /// (exponents, "NaN", spaces) throws <see cref="ZxvStatus.Format"/>.</summary>
        public static Rational Parse(string text)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            ZxvException.ThrowZxv(Native.zxv_rat_parse(Native.Utf8Z(text), out long n, out long d));
            return new Rational(n, d);
        }

        /// <summary>Like <see cref="Parse"/>, without throwing.</summary>
        public static bool TryParse(string text, out Rational value)
        {
            value = default;
            if (text == null) return false;
            if (Native.zxv_rat_parse(Native.Utf8Z(text), out long n, out long d) != 0) return false;
            value = new Rational(n, d);
            return true;
        }

        /// <summary>An integer is exact.</summary>
        public static implicit operator Rational(long value) => new Rational(value, 1);

        private Rational Checked()
        {
            if (den > 0 || (num == 0 && den == 0)) return this;
            return Of(num, den); // throws for a zero denominator
        }

        public static Rational operator +(Rational a, Rational b) => Op(Native.zxv_rat_add, a, b);
        public static Rational operator -(Rational a, Rational b) => Op(Native.zxv_rat_sub, a, b);
        public static Rational operator *(Rational a, Rational b) => Op(Native.zxv_rat_mul, a, b);
        /// <summary>Division by zero throws <see cref="ZxvStatus.Arg"/>.</summary>
        public static Rational operator /(Rational a, Rational b) => Op(Native.zxv_rat_div, a, b);
        public static Rational operator -(Rational a) => Zero - a;

        private delegate int BinOp(long an, long ad, long bn, long bd, out long n, out long d);

        private static Rational Op(BinOp f, Rational a, Rational b)
        {
            a = a.Checked();
            b = b.Checked();
            ZxvException.ThrowZxv(f(a.num, a.Denominator, b.num, b.Denominator, out long n, out long d));
            return new Rational(n, d);
        }

        /// <summary>-1, 0 or +1; exact, no epsilon.</summary>
        public int CompareTo(Rational other)
        {
            Rational a = Checked(), b = other.Checked();
            ZxvException.ThrowZxv(Native.zxv_rat_cmp(a.num, a.Denominator, b.num, b.Denominator, out int c));
            return c;
        }

        public static bool operator <(Rational a, Rational b) => a.CompareTo(b) < 0;
        public static bool operator >(Rational a, Rational b) => a.CompareTo(b) > 0;
        public static bool operator <=(Rational a, Rational b) => a.CompareTo(b) <= 0;
        public static bool operator >=(Rational a, Rational b) => a.CompareTo(b) >= 0;
        public static bool operator ==(Rational a, Rational b) => a.Equals(b);
        public static bool operator !=(Rational a, Rational b) => !a.Equals(b);

        public bool Equals(Rational other) => num == other.num && Denominator == other.Denominator;

        public override bool Equals(object obj) => obj is Rational r && Equals(r);

        public override int GetHashCode() => (num.GetHashCode() * 397) ^ Denominator.GetHashCode();

        /// <summary>Exact text: a terminating decimal when one exists ("12.75"), else "n/d".</summary>
        public override string ToString()
        {
            long d = Denominator;
            if (d <= 0) return ToFractionString();
            Native.zxv_rat_format(num, d, null, UIntPtr.Zero, out UIntPtr need);
            var buf = new byte[(int)need + 1];
            if (Native.zxv_rat_format(num, d, buf, (UIntPtr)buf.Length, out need) != 0) return ToFractionString();
            return Encoding.UTF8.GetString(buf, 0, (int)need);
        }

        /// <summary>Always "num/den" (the inspector's text form).</summary>
        public string ToFractionString() => num.ToString(System.Globalization.CultureInfo.InvariantCulture) + "/" +
                                            Denominator.ToString(System.Globalization.CultureInfo.InvariantCulture);

        /// <summary>
        /// LOSSY: the nearest float, for rendering only. Never store the result
        /// in game state or send it to a peer; the simulation must only ever see
        /// the exact value.
        /// </summary>
        public float ToFloatLossy() => (float)((double)num / Denominator);
    }
}
