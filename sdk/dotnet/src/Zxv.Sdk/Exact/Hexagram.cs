// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// The four-valued base of a <see cref="Hexagram"/>, bits 5-4 (<c>ZXV_HEX_BASE_*</c>).
/// Numbered by the hexagram line pattern (False 00, Neither 01, Both 10, True 11),
/// <em>not</em> like <see cref="Truth"/>: convert with <see cref="Hexagram.ToTruth"/>
/// and <see cref="Hexagram.FromTruth"/>, never with a cast.
/// </summary>
#pragma warning disable CA1027 // Not a flags enum.
public enum HexBase
#pragma warning restore CA1027
{
    /// <summary>False, lines 00.</summary>
    False = 0,

    /// <summary>Neither, lines 01.</summary>
    Neither = 1,

    /// <summary>Both, lines 10.</summary>
    Both = 2,

    /// <summary>True, lines 11.</summary>
    True = 3,
}

/// <summary>How the evidence of a <see cref="Hexagram"/> is moving, bits 3-2 (<c>ZXV_HEX_POL_*</c>).</summary>
public enum HexPolarity
{
    /// <summary>Diverging or stale evidence.</summary>
    Entropic = 0,

    /// <summary>Steady.</summary>
    Neutral = 1,

    /// <summary>A wall: absorbing; the work rolls back and is quarantined.</summary>
    Forbidden = 2,

    /// <summary>Converging evidence.</summary>
    Negentropic = 3,
}

/// <summary>
/// The lifecycle of tagged work, bits 1-0 (<c>ZXV_HEX_PHASE_*</c>): a Gray cycle
/// Potential, Emerging, Manifest, Dissolving, then Potential again.
/// </summary>
#pragma warning disable CA1027 // Not a flags enum.
public enum HexPhase
#pragma warning restore CA1027
{
    /// <summary>Lines 00.</summary>
    Potential = 0,

    /// <summary>Lines 01.</summary>
    Emerging = 1,

    /// <summary>Lines 10.</summary>
    Dissolving = 2,

    /// <summary>Lines 11.</summary>
    Manifest = 3,
}

/// <summary>What happens next to tagged work (<c>ZXV_HEX_ACT_*</c>).</summary>
public enum HexAction
{
    /// <summary>Go ahead.</summary>
    Proceed = 0,

    /// <summary>Hold: unknown, a weakening truth, or a converging dispute.</summary>
    Wait = 1,

    /// <summary>An ordinary refusal; nothing to roll back.</summary>
    Refuse = 2,

    /// <summary>A dispute for a person to settle.</summary>
    Escalate = 3,

    /// <summary>Roll back and quarantine (<see cref="QuarantineLog"/>).</summary>
    Rollback = 4,
}

/// <summary>
/// The 64-state hexagram tag of ABI 1.3 (the kernel's <c>lpres_hex</c>): an
/// immutable value tagging one unit of work with a <see cref="HexBase"/>, a
/// <see cref="HexPolarity"/> and a <see cref="HexPhase"/>, packed as
/// <c>Tag = base &lt;&lt; 4 | polarity &lt;&lt; 2 | phase</c> (0..63).
/// Every rule is computed by libzxv.
/// </summary>
/// <remarks>
/// <see cref="Action"/> says what happens next. <see cref="Step"/> moves one
/// phase at a time; a forbidden tag stays forbidden; <c>&amp;</c>, <c>|</c>
/// and <c>~</c> combine tags. The default value is tag 0
/// (false/entropic/potential).
/// </remarks>
public readonly struct Hexagram : IEquatable<Hexagram>
{
    private readonly byte _tag;

    private Hexagram(byte tag) => _tag = tag;

    /// <summary>Initializes a new instance of the <see cref="Hexagram"/> struct from its three fields.</summary>
    /// <param name="base">Bits 5-4.</param>
    /// <param name="polarity">Bits 3-2.</param>
    /// <param name="phase">Bits 1-0.</param>
    /// <exception cref="ZxvUsageException">A field is not a defined value.</exception>
    public Hexagram(HexBase @base, HexPolarity polarity, HexPhase phase)
    {
        NativeCall.Check(NativeMethods.HexPack(Field((int)@base), Field((int)polarity), Field((int)phase), out byte t), "Hexagram");
        _tag = t;
    }

    /// <summary>Gets the byte value, 0..63.</summary>
    public byte Tag => _tag;

    /// <summary>Gets bits 5-4.</summary>
    public HexBase Base => (HexBase)(_tag >> 4);

    /// <summary>Gets bits 3-2.</summary>
    public HexPolarity Polarity => (HexPolarity)((_tag >> 2) & 3);

    /// <summary>Gets bits 1-0.</summary>
    public HexPhase Phase => (HexPhase)(_tag & 3);

    /// <summary>Gets a value indicating whether the polarity is <see cref="HexPolarity.Forbidden"/> (absorbing).</summary>
    public bool IsForbidden => Polarity == HexPolarity.Forbidden;

    /// <summary>Gets what happens next: one lookup in the kernel's 64-entry table.</summary>
    public HexAction Action
    {
        get
        {
            NativeCall.Check(NativeMethods.HexAction(_tag, out int a), "Hexagram.Action");
            return (HexAction)a;
        }
    }

    /// <summary>Gets the text form "base/polarity/phase", e.g. <c>true/negentropic/manifest</c>.</summary>
    public string Name => Marshal.PtrToStringUTF8(NativeMethods.HexName(_tag)) ?? string.Empty;

    /// <summary>Combines two tags (<see cref="And"/>).</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left and right.</returns>
    public static Hexagram operator &(Hexagram left, Hexagram right) => left.And(right);

    /// <summary>Combines two tags (<see cref="Or"/>).</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left or right.</returns>
    public static Hexagram operator |(Hexagram left, Hexagram right) => left.Or(right);

    /// <summary>Negates the base (<see cref="Not"/>).</summary>
    /// <param name="value">The operand.</param>
    /// <returns>not value.</returns>
    public static Hexagram operator ~(Hexagram value) => value.Not();

    /// <summary>Compares tags.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether they are the same tag.</returns>
    public static bool operator ==(Hexagram left, Hexagram right) => left.Equals(right);

    /// <summary>Compares tags.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(Hexagram left, Hexagram right) => !left.Equals(right);

    /// <summary>The tag with this byte value.</summary>
    /// <param name="tag">0..63.</param>
    /// <returns>The tag.</returns>
    /// <exception cref="ZxvUsageException">The value is above 63.</exception>
    public static unsafe Hexagram FromTag(int tag)
    {
        if (tag is < 0 or > 255)
        {
            throw new ZxvUsageException(ZxvStatus.InvalidArgument, "Hexagram.FromTag", $"tag {tag} is not 0..63");
        }

        NativeCall.Check(NativeMethods.HexUnpack((byte)tag, null, null, null), "Hexagram.FromTag");
        return new Hexagram((byte)tag);
    }

    /// <summary>A tag whose base is <paramref name="truth"/> (<c>zxv_hex_from_lp</c>).</summary>
    /// <param name="truth">The four-valued state.</param>
    /// <param name="polarity">The polarity.</param>
    /// <param name="phase">The phase.</param>
    /// <returns>The tag.</returns>
    public static Hexagram FromTruth(Truth truth, HexPolarity polarity, HexPhase phase)
    {
        NativeCall.Check(NativeMethods.HexFromLp((int)truth, out byte b), "Hexagram.FromTruth");
        return new Hexagram((HexBase)b, polarity, phase);
    }

    /// <summary>
    /// Polarity from evidence counts before and after: stale gives
    /// <see cref="HexPolarity.Entropic"/>; otherwise the margin |pos - neg|
    /// growing gives <see cref="HexPolarity.Negentropic"/>, shrinking
    /// <see cref="HexPolarity.Entropic"/>, unchanged <see cref="HexPolarity.Neutral"/>.
    /// </summary>
    /// <param name="pos0">Evidence for, before.</param>
    /// <param name="neg0">Evidence against, before.</param>
    /// <param name="pos1">Evidence for, after.</param>
    /// <param name="neg1">Evidence against, after.</param>
    /// <param name="stale">Whether the newest evidence is older than the caller's freshness limit.</param>
    /// <returns>The polarity (never <see cref="HexPolarity.Forbidden"/>).</returns>
    public static HexPolarity PolarityFromEvidence(uint pos0, uint neg0, uint pos1, uint neg1, bool stale)
    {
        NativeCall.Check(NativeMethods.HexPolarity(pos0, neg0, pos1, neg1, stale ? 1 : 0, out byte p), "Hexagram.PolarityFromEvidence");
        return (HexPolarity)p;
    }

    /// <summary>The phase after <paramref name="phase"/> on the Gray cycle.</summary>
    /// <param name="phase">A phase.</param>
    /// <returns>The next phase.</returns>
    public static HexPhase NextPhase(HexPhase phase)
    {
        NativeCall.Check(NativeMethods.HexPhaseNext(Field((int)phase), out byte n), "Hexagram.NextPhase");
        return (HexPhase)n;
    }

    /// <summary>The base as a <see cref="Truth"/> (<c>zxv_hex_to_lp</c>): an explicit conversion.</summary>
    /// <returns>The four-valued state.</returns>
    public Truth ToTruth()
    {
        NativeCall.Check(NativeMethods.HexToLp((byte)(_tag >> 4), out int lp), "Hexagram.ToTruth");
        return (Truth)lp;
    }

    /// <summary>The same work in <paramref name="phase"/>: the same phase or the next one only.</summary>
    /// <param name="phase">The target phase.</param>
    /// <returns>The moved tag.</returns>
    /// <exception cref="ZxvRefusedException">(<see cref="ZxvStatus.InvalidState"/>) the move skips a phase.</exception>
    public Hexagram Step(HexPhase phase)
    {
        NativeCall.Check(NativeMethods.HexStep(_tag, Field((int)phase), out byte r), "Hexagram.Step");
        return new Hexagram(r);
    }

    /// <summary>One step along the cycle.</summary>
    /// <returns>The moved tag.</returns>
    public Hexagram Advance() => Step(NextPhase(Phase));

    /// <summary>New base and polarity, same phase.</summary>
    /// <param name="base">The new base.</param>
    /// <param name="polarity">The new polarity.</param>
    /// <returns>The new tag.</returns>
    /// <exception cref="ZxvRefusedException">(<see cref="ZxvStatus.InvalidState"/>) this tag is forbidden and <paramref name="polarity"/> is not.</exception>
    public Hexagram WithState(HexBase @base, HexPolarity polarity)
    {
        NativeCall.Check(NativeMethods.HexSet(_tag, Field((int)@base), Field((int)polarity), out byte r), "Hexagram.WithState");
        return new Hexagram(r);
    }

    /// <summary>The same tag with polarity <see cref="HexPolarity.Forbidden"/>.</summary>
    /// <returns>The forbidden tag.</returns>
    public Hexagram Forbid()
    {
        NativeCall.Check(NativeMethods.HexForbid(_tag, out byte r), "Hexagram.Forbid");
        return new Hexagram(r);
    }

    /// <summary>Conjunction: four-valued AND on the base, the worst polarity, the least advanced phase.</summary>
    /// <param name="other">The other tag.</param>
    /// <returns>this and other.</returns>
    public Hexagram And(Hexagram other)
    {
        NativeCall.Check(NativeMethods.HexAnd(_tag, other._tag, out byte r), "Hexagram.And");
        return new Hexagram(r);
    }

    /// <summary>Disjunction: four-valued OR on the base, the worst polarity, the least advanced phase.</summary>
    /// <param name="other">The other tag.</param>
    /// <returns>this or other.</returns>
    public Hexagram Or(Hexagram other)
    {
        NativeCall.Check(NativeMethods.HexOr(_tag, other._tag, out byte r), "Hexagram.Or");
        return new Hexagram(r);
    }

    /// <summary>Negation of the base; polarity and phase are kept.</summary>
    /// <returns>not this.</returns>
    public Hexagram Not()
    {
        NativeCall.Check(NativeMethods.HexNot(_tag, out byte r), "Hexagram.Not");
        return new Hexagram(r);
    }

    /// <summary>Same as <c>&amp;</c>.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left and right.</returns>
    public static Hexagram BitwiseAnd(Hexagram left, Hexagram right) => left.And(right);

    /// <summary>Same as <c>|</c>.</summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns>left or right.</returns>
    public static Hexagram BitwiseOr(Hexagram left, Hexagram right) => left.Or(right);

    /// <summary>Same as <c>~</c>.</summary>
    /// <param name="value">The operand.</param>
    /// <returns>not value.</returns>
    public static Hexagram OnesComplement(Hexagram value) => value.Not();

    /// <inheritdoc/>
    public bool Equals(Hexagram other) => _tag == other._tag;

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is Hexagram h && Equals(h);

    /// <inheritdoc/>
    public override int GetHashCode() => _tag;

    /// <inheritdoc/>
    public override string ToString() => $"Hexagram({Name}, tag={_tag})";

    /// <summary>A field value for a by-value <c>uint8_t</c>: anything outside 0..255 becomes 255, which libzxv refuses.</summary>
    private static byte Field(int value) => value is >= 0 and <= 255 ? (byte)value : (byte)255;
}
