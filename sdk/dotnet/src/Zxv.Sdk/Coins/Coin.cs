// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Coins;

/// <summary>
/// The VFV coin ladder (ABI 1.5, <c>zxv_coin_*</c>): Fibonacci faces and the
/// unique fewest-coins (Zeckendorf) split of an amount. Pure: no context,
/// no capability, integers only.
/// </summary>
/// <remarks>
/// <para>
/// Rung r (1..<see cref="Rungs"/>) has face F(r+1): 1, 2, 3, 5, 8, 13, ...
/// up to <see cref="TopFace"/>. Rung 0 is the ceremonial V0 note, face 0.
/// Any other face (4, 6, 7, 100, ...) is not a coin and is refused with
/// <see cref="ZxvStatus.Denomination"/>.
/// </para>
/// <para>
/// HONEST LIMITS. Minting VFV coins is not in this ABI: the kernel's mint
/// path uses a floating-point amount type in the host build, so it is not
/// wrapped. <see cref="CapabilityRights.Mint"/> is used only by
/// <see cref="TokenWallet"/>.
/// </para>
/// </remarks>
public static class Coin
{
    /// <summary>Rungs 1..45 carry value (<c>ZXV_COIN_RUNGS</c>).</summary>
    public const int Rungs = 45;

    /// <summary>The face of rung 45, F(46) (<c>ZXV_COIN_TOP_FACE</c>).</summary>
    public const ulong TopFace = 1_836_311_903;

    /// <summary>The face of ladder rung <paramref name="rung"/>; rung 0 is the V0 note, face 0.</summary>
    /// <param name="rung">0..<see cref="Rungs"/>.</param>
    /// <returns>The face.</returns>
    /// <exception cref="ZxvUsageException">The rung is above 45 (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static ulong Face(int rung)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(rung);
        NativeCall.Check(NativeMethods.CoinFace((uint)rung, out ulong face), "Coin.Face");
        return face;
    }

    /// <summary>The rung whose face is exactly <paramref name="face"/> (face 1 is rung 1).</summary>
    /// <param name="face">A face on the ladder.</param>
    /// <returns>1..<see cref="Rungs"/>.</returns>
    /// <exception cref="ZxvUsageException">Not a face on the ladder, including 0 (<see cref="ZxvStatus.Denomination"/>).</exception>
    public static int RungOf(ulong face)
    {
        NativeCall.Check(NativeMethods.CoinRung(face, out uint rung), "Coin.RungOf");
        return (int)rung;
    }

    /// <summary>
    /// The coins for <paramref name="amount"/>: its Zeckendorf split into
    /// rungs, largest first. Amounts at or above the top face take whole
    /// top-rung coins first; the rest uses each lower rung at most once and
    /// never two neighbouring rungs, so the split is unique and the fewest
    /// coins possible. 0 is an empty split.
    /// </summary>
    /// <param name="amount">The whole amount.</param>
    /// <returns>The rungs, largest first.</returns>
    /// <exception cref="ZxvUsageException">The split would need more than 4,294,967,294 coins (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    /// <exception cref="OutOfMemoryException">The split is too large to hold in memory (amounts of about 4e18 and up need billions of coins).</exception>
    public static unsafe int[] Split(ulong amount)
    {
        int rc = NativeMethods.CoinSplit(amount, null, 0, out nuint count);
        if (rc == (int)ZxvStatus.Ok)
        {
            return Array.Empty<int>();
        }

        if (rc != (int)ZxvStatus.BufferTooSmall)
        {
            NativeCall.Check(rc, "Coin.Split");
        }

        var rungs = new int[checked((int)count)];
        fixed (int* p = rungs)
        {
            NativeCall.Check(NativeMethods.CoinSplit(amount, (uint*)p, count, out count), "Coin.Split");
        }

        return rungs;
    }

    /// <summary>The number of coins in <paramref name="amount"/>'s split, without listing them.</summary>
    /// <param name="amount">The whole amount.</param>
    /// <returns>The coin count.</returns>
    /// <exception cref="ZxvUsageException">The split would need more than 4,294,967,294 coins (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe long SplitCount(ulong amount)
    {
        int rc = NativeMethods.CoinSplit(amount, null, 0, out nuint count);
        if (rc != (int)ZxvStatus.Ok && rc != (int)ZxvStatus.BufferTooSmall)
        {
            NativeCall.Check(rc, "Coin.SplitCount");
        }

        return (long)count;
    }
}

/// <summary>
/// Named octane grades for <see cref="Token"/> holdings: the densest tier a
/// holding may use. Any octane 0..<see cref="Max"/> is allowed; these are
/// the named grades, like a fuel pump's.
/// </summary>
public static class Octane
{
    /// <summary>Face 1 only.</summary>
    public const int Plain = 0;

    /// <summary>Up to face 8.</summary>
    public const int Regular = 4;

    /// <summary>Up to face 89.</summary>
    public const int Midgrade = 9;

    /// <summary>Up to face 987.</summary>
    public const int Premium = 14;

    /// <summary>Up to face 75025.</summary>
    public const int Racing = 23;

    /// <summary>Up to face F(65), the densest.</summary>
    public const int Max = 63;
}

/// <summary>A token holding's value and number of pieces.</summary>
/// <param name="Value">Sum of count times face over the tiers.</param>
/// <param name="Pieces">Sum of the counts.</param>
public readonly record struct TokenAmount(ulong Value, ulong Pieces);

/// <summary>
/// Swarm AI token denominations (ABI 1.5, <c>zxv_token_*</c>). Tier k
/// (0..<see cref="Tiers"/>-1) has face F(k+2): 1, 2, 3, 5, 8, ... A holding
/// is a span of exactly <see cref="Tiers"/> counts, one per tier. Pure: no
/// context, no capability.
/// </summary>
/// <remarks>
/// NO PREMIUM. A dense token is worth exactly its face in plain tokens.
/// Density earns no interest, costs no fee and buys no priority; changing
/// octane re-expresses the same value and never changes it. It only means
/// fewer pieces (1,000,000 tokens are 1,000,000 pieces at
/// <see cref="Octane.Plain"/>, 17 at <see cref="Octane.Racing"/>). Every
/// in-place operation leaves the counts unchanged when it fails.
/// </remarks>
public static class Token
{
    /// <summary>The number of tiers, and the length of every holding (<c>ZXV_TOKEN_TIERS</c>).</summary>
    public const int Tiers = 64;

    /// <summary>The first super-token tier (face 144) (<c>ZXV_TOKEN_SUPER_TIER</c>).</summary>
    public const int SuperTier = 10;

    /// <summary>The face of <paramref name="tier"/>.</summary>
    /// <param name="tier">0..63.</param>
    /// <returns>F(tier+2).</returns>
    /// <exception cref="ZxvUsageException">The tier is 64 or more (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static ulong Face(int tier)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(tier);
        NativeCall.Check(NativeMethods.TokenFace((uint)tier, out ulong face, out _), "Token.Face");
        return face;
    }

    /// <summary>Whether <paramref name="tier"/> is a super token (tier 10, face 144, and up).</summary>
    /// <param name="tier">0..63.</param>
    /// <returns><see langword="true"/> for a super token.</returns>
    /// <exception cref="ZxvUsageException">The tier is 64 or more (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static bool IsSuper(int tier)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(tier);
        NativeCall.Check(NativeMethods.TokenFace((uint)tier, out _, out int isSuper), "Token.IsSuper");
        return isSuper != 0;
    }

    /// <summary>The tier whose face is exactly <paramref name="face"/>.</summary>
    /// <param name="face">One of the 64 faces.</param>
    /// <returns>0..63.</returns>
    /// <exception cref="ZxvUsageException">Not one of the faces (<see cref="ZxvStatus.Denomination"/>).</exception>
    public static int TierOf(ulong face)
    {
        NativeCall.Check(NativeMethods.TokenTier(face, out uint tier), "Token.TierOf");
        return (int)tier;
    }

    /// <summary>The value and piece count of a holding.</summary>
    /// <param name="counts">Exactly <see cref="Tiers"/> counts.</param>
    /// <returns>Value and pieces.</returns>
    /// <exception cref="ZxvUsageException">The value or piece count passes 64 bits (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe TokenAmount Measure(ReadOnlySpan<ulong> counts)
    {
        RequireHolding(counts, nameof(counts));
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenValue(c, out ulong value, out ulong pieces), "Token.Measure");
            return new TokenAmount(value, pieces);
        }
    }

    /// <summary>
    /// The canonical holding of <paramref name="value"/> at
    /// <paramref name="octane"/>: whole tokens of the octane's tier, and the
    /// remainder in Zeckendorf form below it. Every value has exactly one.
    /// </summary>
    /// <param name="value">The value.</param>
    /// <param name="octane">0..<see cref="Octane.Max"/>.</param>
    /// <returns>A new holding of <see cref="Tiers"/> counts.</returns>
    /// <exception cref="ZxvUsageException">The octane is 64 or more (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe ulong[] Canonical(ulong value, int octane)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        var counts = new ulong[Tiers];
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenCanonical(value, (uint)octane, c), "Token.Canonical");
        }

        return counts;
    }

    /// <summary>Whether <paramref name="counts"/> is the canonical form of its value at <paramref name="octane"/> (false also when a denser tier is used).</summary>
    /// <param name="counts">Exactly <see cref="Tiers"/> counts.</param>
    /// <param name="octane">0..<see cref="Octane.Max"/>.</param>
    /// <returns>Whether it is canonical.</returns>
    /// <exception cref="ZxvUsageException">Bad octane, or the value passes 64 bits (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe bool IsCanonical(ReadOnlySpan<ulong> counts, int octane)
    {
        RequireHolding(counts, nameof(counts));
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenIsCanonical(c, (uint)octane, out int canonical), "Token.IsCanonical");
            return canonical != 0;
        }
    }

    /// <summary>Rewrites <paramref name="counts"/> in place into the canonical form of its value at <paramref name="octane"/>. The value never changes.</summary>
    /// <param name="counts">Exactly <see cref="Tiers"/> counts.</param>
    /// <param name="octane">0..<see cref="Octane.Max"/>.</param>
    /// <exception cref="ZxvUsageException">Bad octane, or the value passes 64 bits (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static unsafe void Compress(Span<ulong> counts, int octane)
    {
        RequireHolding(counts, nameof(counts));
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenCompress(c, (uint)octane), "Token.Compress");
        }
    }

    /// <summary>Fuses in place: one tier-k and one tier-(k+1) token become one tier-(k+2) token (F(k+2) + F(k+3) = F(k+4)).</summary>
    /// <param name="counts">Exactly <see cref="Tiers"/> counts.</param>
    /// <param name="octane">The densest tier allowed.</param>
    /// <param name="tier">k, 0..61.</param>
    /// <exception cref="ZxvException">Bad octane or tier, a full count (<see cref="ZxvStatus.AmountOutOfRange"/>), or tier k+2 above the octane (<see cref="ZxvStatus.InvalidState"/>).</exception>
    /// <exception cref="InsufficientFundsException">No tier-k or no tier-(k+1) token.</exception>
    public static unsafe void Fuse(Span<ulong> counts, int octane, int tier)
    {
        RequireHolding(counts, nameof(counts));
        ArgumentOutOfRangeException.ThrowIfNegative(octane);
        ArgumentOutOfRangeException.ThrowIfNegative(tier);
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenFuse(c, (uint)octane, (uint)tier), "Token.Fuse");
        }
    }

    /// <summary>Splits in place: one tier-k token becomes one tier-(k-1) and one tier-(k-2) token; tier 1 becomes two plain tokens. Tier 0 is atomic.</summary>
    /// <param name="counts">Exactly <see cref="Tiers"/> counts.</param>
    /// <param name="tier">k, 1..63.</param>
    /// <exception cref="ZxvUsageException">Tier 0 or 64+, or a lower count is full (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    /// <exception cref="InsufficientFundsException">No tier-k token.</exception>
    public static unsafe void Split(Span<ulong> counts, int tier)
    {
        RequireHolding(counts, nameof(counts));
        ArgumentOutOfRangeException.ThrowIfNegative(tier);
        fixed (ulong* c = counts)
        {
            NativeCall.Check(NativeMethods.TokenSplit(c, (uint)tier), "Token.Split");
        }
    }

    internal static void RequireHolding(ReadOnlySpan<ulong> counts, string paramName)
    {
        if (counts.Length != Tiers)
        {
            throw new ArgumentException($"A holding is exactly {Tiers} counts (got {counts.Length}).", paramName);
        }
    }
}

/// <summary>
/// Fibonacci codes for amounts (ABI 1.5, <c>zxv_token_code_*</c>): the
/// Zeckendorf bits of value+1, lowest first, then a closing 1, so every code
/// ends in "11" and needs no length field. Bit i of a buffer is bit i%8 of
/// byte i/8.
/// </summary>
public static class FibonacciCode
{
    /// <summary>The longest code: 92 Zeckendorf bits and the closing 1 (<c>ZXV_TOKEN_CODE_MAX_BITS</c>).</summary>
    public const int MaxBits = 93;

    /// <summary>The largest buffer the code functions take (<c>ZXV_TOKEN_CODE_MAX_BYTES</c>).</summary>
    public const int MaxBufferBytes = 536_870_911;

    /// <summary>The number of bits in the code of <paramref name="value"/> (2..93).</summary>
    /// <param name="value">Any value but <see cref="ulong.MaxValue"/>.</param>
    /// <returns>The bit count.</returns>
    /// <exception cref="ZxvUsageException"><see cref="ulong.MaxValue"/> is not encodable (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static int BitLength(ulong value)
    {
        NativeCall.Check(NativeMethods.TokenCodeBits(value, out uint bits), "FibonacciCode.BitLength");
        return (int)bits;
    }

    /// <summary>Writes the code of <paramref name="value"/> at bit <paramref name="bit"/> of <paramref name="buffer"/>; only the code's bits are touched and <paramref name="bit"/> advances past them.</summary>
    /// <param name="buffer">The buffer.</param>
    /// <param name="bit">The bit position; advanced on success, unchanged on failure.</param>
    /// <param name="value">Any value but <see cref="ulong.MaxValue"/>.</param>
    /// <exception cref="ZxvUsageException">Not encodable or a bad position (<see cref="ZxvStatus.AmountOutOfRange"/>), or the code does not fit (<see cref="ZxvStatus.BufferTooSmall"/>; nothing written).</exception>
    public static unsafe void Write(Span<byte> buffer, ref int bit, ulong value)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(bit);
        uint b = (uint)bit;
        fixed (byte* p = buffer)
        {
            NativeCall.Check(NativeMethods.TokenCodePut(p, (nuint)buffer.Length, &b, value), "FibonacciCode.Write");
        }

        bit = (int)b;
    }

    /// <summary>Reads one code from bit <paramref name="bit"/> of <paramref name="buffer"/>; <paramref name="bit"/> advances past it.</summary>
    /// <param name="buffer">The buffer.</param>
    /// <param name="bit">The bit position; advanced on success, unchanged on failure.</param>
    /// <returns>The value.</returns>
    /// <exception cref="ZxvUsageException">The position is past the end (<see cref="ZxvStatus.AmountOutOfRange"/>), or no closing "11" or a value past 64 bits (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe ulong Read(ReadOnlySpan<byte> buffer, ref int bit)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(bit);
        uint b = (uint)bit;
        ulong value;
        fixed (byte* p = buffer)
        {
            NativeCall.Check(NativeMethods.TokenCodeGet(p, (nuint)buffer.Length, &b, out value), "FibonacciCode.Read");
        }

        bit = (int)b;
        return value;
    }

    /// <summary>Encodes <paramref name="values"/> one after another into the fewest whole bytes.</summary>
    /// <param name="values">The values (none may be <see cref="ulong.MaxValue"/>).</param>
    /// <param name="bitCount">The number of bits used; the rest of the last byte is zero.</param>
    /// <returns>The bytes.</returns>
    public static byte[] Encode(ReadOnlySpan<ulong> values, out int bitCount)
    {
        long total = 0;
        foreach (ulong v in values)
        {
            total += BitLength(v);
        }

        var buffer = new byte[checked((int)((total + 7) / 8))];
        int bit = 0;
        foreach (ulong v in values)
        {
            Write(buffer, ref bit, v);
        }

        bitCount = bit;
        return buffer;
    }
}
