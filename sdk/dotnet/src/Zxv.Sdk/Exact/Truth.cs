// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// Paraconsistent four-valued logic (the kernel's <c>lpres_state_t</c>,
/// Belnap's FOUR). Values are numbered as the kernel numbers them, forever.
/// </summary>
/// <remarks>
/// <para>
/// There is deliberately no conversion to <see cref="bool"/>: <see cref="Both"/>
/// is a held contradiction (evidence for and against), not an error and not
/// <see langword="null"/>, and <see cref="Neither"/> means nothing is known yet.
/// Turn a truth into a decision with an explicit policy:
/// <see cref="TruthExtensions.IsCertainlyTrue(Truth)"/>,
/// <see cref="TruthExtensions.ToBoolean(Truth, bool, bool)"/> or
/// <see cref="TruthExtensions.RequireTrue(Truth, string)"/>.
/// </para>
/// <para>
/// The kernel plans an orthogonal 2-bit <c>mode</c> beside these four states.
/// It will arrive as a separate accessor, never as new members of this enum,
/// so code that switches over the four values stays complete.
/// </para>
/// </remarks>
#pragma warning disable CA1027 // Not a flags enum: four disjoint states.
public enum Truth
#pragma warning restore CA1027
{
    /// <summary>S0: unknown, unattested, pending.</summary>
    Neither = 0,

    /// <summary>S+: attested present and valid.</summary>
    True = 1,

    /// <summary>S-: attested absent or invalid.</summary>
    False = 2,

    /// <summary>Contradictory evidence, held.</summary>
    Both = 3,
}

/// <summary>Operations and explicit boolean policies for <see cref="Truth"/>.</summary>
public static class TruthExtensions
{
    /// <summary>Four-valued negation: swaps <see cref="Truth.True"/> and <see cref="Truth.False"/>; <see cref="Truth.Both"/> and <see cref="Truth.Neither"/> are fixed points.</summary>
    /// <param name="value">The operand.</param>
    /// <returns>not value.</returns>
    public static Truth Not(this Truth value)
    {
        NativeCall.Check(NativeMethods.LpNot((int)value, out int r), "Truth.Not");
        return (Truth)r;
    }

    /// <summary>Four-valued conjunction (the kernel table; <c>Both and True = Both</c>: a contradiction survives).</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left and right.</returns>
    public static Truth And(this Truth left, Truth right)
    {
        NativeCall.Check(NativeMethods.LpAnd((int)left, (int)right, out int r), "Truth.And");
        return (Truth)r;
    }

    /// <summary>Four-valued disjunction (the kernel table).</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left or right.</returns>
    public static Truth Or(this Truth left, Truth right)
    {
        NativeCall.Check(NativeMethods.LpOr((int)left, (int)right, out int r), "Truth.Or");
        return (Truth)r;
    }

    /// <summary>
    /// The truth that evidence counts support: some for and some against is
    /// <see cref="Truth.Both"/>, only for is <see cref="Truth.True"/>, only
    /// against is <see cref="Truth.False"/>, none is <see cref="Truth.Neither"/>.
    /// </summary>
    /// <param name="positive">Pieces of evidence for.</param>
    /// <param name="negative">Pieces of evidence against.</param>
    /// <returns>The truth.</returns>
    public static Truth FromEvidence(uint positive, uint negative)
    {
        NativeCall.Check(NativeMethods.LpFromEvidence(positive, negative, out int r), "Truth.FromEvidence");
        return (Truth)r;
    }

    /// <summary>Gets the kernel name: <c>NEITHER</c>, <c>TRUE</c>, <c>FALSE</c> or <c>BOTH</c> (<c>INVALID</c> otherwise).</summary>
    /// <param name="value">The value.</param>
    /// <returns>The name.</returns>
    public static unsafe string Name(this Truth value)
    {
        byte* p = NativeMethods.LpName((int)value);
        return p == null ? "INVALID" : Marshal.PtrToStringUTF8((IntPtr)p) ?? "INVALID";
    }

    /// <summary>Gets whether the value is exactly <see cref="Truth.True"/> (no contradiction, not unknown).</summary>
    /// <param name="value">The value.</param>
    /// <returns><see langword="true"/> only for <see cref="Truth.True"/>.</returns>
    public static bool IsCertainlyTrue(this Truth value) => value == Truth.True;

    /// <summary>Gets whether the value is exactly <see cref="Truth.False"/>.</summary>
    /// <param name="value">The value.</param>
    /// <returns><see langword="true"/> only for <see cref="Truth.False"/>.</returns>
    public static bool IsCertainlyFalse(this Truth value) => value == Truth.False;

    /// <summary>Gets whether the value is certain: <see cref="Truth.True"/> or <see cref="Truth.False"/>.</summary>
    /// <param name="value">The value.</param>
    /// <returns><see langword="true"/> when no contradiction and not unknown.</returns>
    public static bool IsCertain(this Truth value) => value is Truth.True or Truth.False;

    /// <summary>Gets whether the value is a held contradiction (<see cref="Truth.Both"/>).</summary>
    /// <param name="value">The value.</param>
    /// <returns><see langword="true"/> for <see cref="Truth.Both"/>.</returns>
    public static bool IsContradiction(this Truth value) => value == Truth.Both;

    /// <summary>
    /// Converts to <see cref="bool"/> with an explicit policy for the two
    /// uncertain states.
    /// </summary>
    /// <param name="value">The value.</param>
    /// <param name="onBoth">The answer for a contradiction.</param>
    /// <param name="onNeither">The answer when nothing is known.</param>
    /// <returns>The decision.</returns>
    public static bool ToBoolean(this Truth value, bool onBoth, bool onNeither) => value switch
    {
        Truth.True => true,
        Truth.False => false,
        Truth.Both => onBoth,
        Truth.Neither => onNeither,
        _ => throw new ArgumentOutOfRangeException(nameof(value), value, "Not a four-valued truth."),
    };

    /// <summary>Throws unless the value is <see cref="Truth.True"/>.</summary>
    /// <param name="value">The value.</param>
    /// <param name="what">What is being required, for the message.</param>
    /// <exception cref="InvalidOperationException">The value is <see cref="Truth.False"/>, <see cref="Truth.Both"/> or <see cref="Truth.Neither"/>.</exception>
    public static void RequireTrue(this Truth value, string what = "condition")
    {
        if (value != Truth.True)
        {
            throw new InvalidOperationException($"{what} is {value.Name()}, not TRUE.");
        }
    }
}
