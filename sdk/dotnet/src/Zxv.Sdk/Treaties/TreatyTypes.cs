// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Treaties;

/// <summary>What a treaty asks the settlement engine to do (<c>ZXV_TREATY_OP_*</c>).</summary>
public enum TreatyOperation
{
    /// <summary>An empty treaty has no operation yet.</summary>
    None = 0,

    /// <summary>Net the obligations only (<c>NET</c>).</summary>
    Net = 1,

    /// <summary>Net, post the legs with the assurance fee, then pay the claims back out (<c>SETL</c>).</summary>
    Settle = 2,
}

/// <summary>Why a treaty was refused (<c>ZXV_TREATY_FAULT_*</c>).</summary>
public enum TreatyFault
{
    /// <summary>No fault: the treaty is valid.</summary>
    None = 0,

    /// <summary>The record does not start with <c>ZXT1</c>.</summary>
    Magic = 2,

    /// <summary>The Sutra text is outside the grammar (this includes any non-ASCII character).</summary>
    Syntax = 3,

    /// <summary>The treaty name is not 1 to 30 characters of A-Z, 0-9 and '-', starting with a letter.</summary>
    Name = 4,

    /// <summary>The asset is not three capital letters.</summary>
    Asset = 5,

    /// <summary>The operation is not NET or SETL.</summary>
    Operation = 6,

    /// <summary>The party count (1..16) or obligation count (0..64) is out of range.</summary>
    Count = 7,

    /// <summary>A number is malformed (for example a leading zero) or outside PIC S9(18).</summary>
    Number = 8,

    /// <summary>A party id is malformed, repeated, or not a known party.</summary>
    Party = 9,

    /// <summary>The dividend weights break rule R1: each num/den in lowest terms with num &lt;= den, adding up to exactly 1.</summary>
    Weight = 10,

    /// <summary>A party claims back more than it deposits (rule R2).</summary>
    Claim = 11,

    /// <summary>An obligation is owed to oneself, or is not above zero.</summary>
    Obligation = 12,

    /// <summary>An unused record slot or field is not exactly what COBOL's INITIALIZE leaves.</summary>
    Padding = 13,
}

/// <summary>One party of a treaty.</summary>
/// <param name="Id">Its id: 1 to 8 characters of A-Z, 0-9 and '-'.</param>
/// <param name="Deposit">What it deposits, in minor units of the treaty's asset (never negative).</param>
/// <param name="Claim">What it may claim back, in minor units (at most <paramref name="Deposit"/>).</param>
/// <param name="WeightNumerator">The numerator of its dividend weight.</param>
/// <param name="WeightDenominator">The denominator of its dividend weight; the weight is in lowest terms.</param>
public readonly record struct TreatyParty(string Id, long Deposit, long Claim, uint WeightNumerator, uint WeightDenominator);

/// <summary>One obligation of a treaty: party <paramref name="From"/> owes party <paramref name="To"/> <paramref name="Amount"/>.</summary>
/// <param name="From">The debtor's party index (0-based, in treaty order).</param>
/// <param name="To">The creditor's party index (0-based).</param>
/// <param name="Amount">The amount owed in minor units (above zero).</param>
public readonly record struct TreatyObligation(int From, int To, long Amount);

/// <summary>One party's line in a <see cref="TreatyResult"/>.</summary>
/// <param name="Net">What it is owed minus what it owes (gross, before netting).</param>
/// <param name="Balance">For NET its deposit; for SETL what it holds after the legs, the fee and its claim.</param>
/// <param name="Dividend">Its reported share of the dividend bucket (0 for NET). Reported only, never paid out.</param>
public readonly record struct TreatyResultParty(long Net, long Balance, long Dividend);

/// <summary>One netted leg of a <see cref="TreatyResult"/>: party <paramref name="From"/> pays party <paramref name="To"/>.</summary>
/// <param name="From">The payer's party index (0-based).</param>
/// <param name="To">The payee's party index (0-based).</param>
/// <param name="Amount">The amount in minor units.</param>
public readonly record struct TreatyLeg(int From, int To, long Amount);

/// <summary>
/// A treaty failed validation (<see cref="ZxvStatus.TreatyInvalid"/>, class USAGE).
/// <see cref="Fault"/> says which rule broke and, for a parse,
/// <see cref="FaultOffset"/> the byte offset of the first fault in the input.
/// </summary>
public sealed class TreatyException : ZxvUsageException
{
    /// <summary>Initializes a new instance.</summary>
    public TreatyException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public TreatyException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public TreatyException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native refusal.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    /// <param name="fault">Which rule broke.</param>
    /// <param name="faultOffset">The byte offset of the first fault in the parsed input (0 when not a parse).</param>
    public TreatyException(string operation, string detail, TreatyFault fault, int faultOffset)
        : base(ZxvStatus.TreatyInvalid, operation, detail)
    {
        Fault = fault;
        FaultOffset = faultOffset;
    }

    /// <summary>Gets which rule the treaty broke.</summary>
    public TreatyFault Fault { get; }

    /// <summary>Gets the byte offset of the first fault in the parsed Sutra text or record (0 when the refusal was not a parse).</summary>
    public int FaultOffset { get; }
}

/// <summary>
/// COBOL's <c>PIC S9(18)</c> arithmetic with <c>ON SIZE ERROR</c>
/// (<c>zxv_treaty_pic_*</c>): exact integers in
/// [-<see cref="Max"/>, <see cref="Max"/>]. Every operand and every exact
/// result must stay in that range, or the call is refused with
/// <see cref="ZxvStatus.AmountOutOfRange"/> (a size error) and nothing is
/// returned. Pure: no context, no capability.
/// </summary>
public static class Pic18
{
    /// <summary>The largest magnitude, 999 999 999 999 999 999 (<c>ZXV_TREATY_PIC18_MAX</c>).</summary>
    public const long Max = 999_999_999_999_999_999L;

    /// <summary>Adds exactly.</summary>
    /// <param name="a">The first operand.</param>
    /// <param name="b">The second operand.</param>
    /// <returns><paramref name="a"/> + <paramref name="b"/>.</returns>
    /// <exception cref="ZxvUsageException">An operand or the sum is outside PIC S9(18) (<see cref="ZxvStatus.AmountOutOfRange"/>).</exception>
    public static long Add(long a, long b)
    {
        ZxvLibrary.EnsureCompatible();
        NativeCall.Check(NativeMethods.TreatyPicAdd(a, b, out long r), "Pic18.Add");
        return r;
    }

    /// <summary>Subtracts exactly.</summary>
    /// <param name="a">The minuend.</param>
    /// <param name="b">The subtrahend.</param>
    /// <returns><paramref name="a"/> - <paramref name="b"/>.</returns>
    /// <exception cref="ZxvUsageException">An operand or the difference is outside PIC S9(18).</exception>
    public static long Subtract(long a, long b)
    {
        ZxvLibrary.EnsureCompatible();
        NativeCall.Check(NativeMethods.TreatyPicSub(a, b, out long r), "Pic18.Subtract");
        return r;
    }

    /// <summary>Multiplies exactly.</summary>
    /// <param name="a">The first factor.</param>
    /// <param name="b">The second factor.</param>
    /// <returns><paramref name="a"/> * <paramref name="b"/>.</returns>
    /// <exception cref="ZxvUsageException">An operand or the product is outside PIC S9(18).</exception>
    public static long Multiply(long a, long b)
    {
        ZxvLibrary.EnsureCompatible();
        NativeCall.Check(NativeMethods.TreatyPicMul(a, b, out long r), "Pic18.Multiply");
        return r;
    }

    /// <summary>
    /// Divides as COBOL's <c>DIVIDE a BY b GIVING q REMAINDER r</c>: the
    /// quotient truncates toward zero and the remainder has the dividend's
    /// sign (-7 / 2 = -3 remainder -1; 7 / -2 = -3 remainder 1).
    /// </summary>
    /// <param name="a">The dividend.</param>
    /// <param name="b">The divisor.</param>
    /// <returns>The quotient and the remainder.</returns>
    /// <exception cref="ZxvUsageException">Division by zero, or an operand outside PIC S9(18).</exception>
    public static (long Quotient, long Remainder) DivRem(long a, long b)
    {
        ZxvLibrary.EnsureCompatible();
        NativeCall.Check(NativeMethods.TreatyPicDiv(a, b, out long q, out long r), "Pic18.DivRem");
        return (q, r);
    }
}
