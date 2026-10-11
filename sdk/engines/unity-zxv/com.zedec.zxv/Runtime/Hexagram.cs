// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;

namespace Zedec.Zxv
{
    /// <summary>Four-valued logic, numbered as the kernel numbers it. No conversion to bool.</summary>
    public enum Truth
    {
        Neither = 0,
        True = 1,
        False = 2,
        Both = 3,
    }

    /// <summary>A hexagram base, numbered by its line pattern (NOT like <see cref="Truth"/>).</summary>
    public enum HexBase : byte { False = 0, Neither = 1, Both = 2, True = 3 }

    /// <summary>Polarity: how the evidence is moving. Forbidden is absorbing.</summary>
    public enum HexPolarity : byte { Entropic = 0, Neutral = 1, Forbidden = 2, Negentropic = 3 }

    /// <summary>Lifecycle phase, a Gray cycle: Potential, Emerging, Manifest, Dissolving.</summary>
    public enum HexPhase : byte { Potential = 0, Emerging = 1, Dissolving = 2, Manifest = 3 }

    /// <summary>What a tag says to do next.</summary>
    public enum HexAction { Proceed = 0, Wait = 1, Refuse = 2, Escalate = 3, Rollback = 4 }

    /// <summary>
    /// The 64-state hexagram tag (libzxv ABI 1.3): one byte, base &lt;&lt; 4 |
    /// polarity &lt;&lt; 2 | phase. Immutable and Unity-serializable (one byte
    /// field, <c>tag</c>). Every rule comes from libzxv's <c>zxv_hex_*</c>.
    /// </summary>
    [Serializable]
    public struct Hexagram : IEquatable<Hexagram>
    {
#if UNITY_5_3_OR_NEWER
        [UnityEngine.SerializeField]
#endif
        private byte tag;

        private Hexagram(byte t) { tag = t; }

        /// <summary>A tag 0..63; anything above throws <see cref="ZxvStatus.Arg"/>.</summary>
        public static Hexagram FromTag(int value)
        {
            if (value < 0 || value > 63) throw new ZxvException(-1, "a hexagram tag is 0..63");
            return new Hexagram((byte)value);
        }

        public static Hexagram Of(HexBase b, HexPolarity polarity, HexPhase phase)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_pack((byte)b, (byte)polarity, (byte)phase, out byte t));
            return new Hexagram(t);
        }

        /// <summary>The explicit four-valued bridge (zxv_hex_from_lp): never a cast.</summary>
        public static Hexagram FromTruth(Truth truth, HexPolarity polarity, HexPhase phase)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_from_lp((int)truth, out byte b));
            return Of((HexBase)b, polarity, phase);
        }

        public int Tag => tag;
        public HexBase Base => (HexBase)(tag >> 4 & 3);
        public HexPolarity Polarity => (HexPolarity)(tag >> 2 & 3);
        public HexPhase Phase => (HexPhase)(tag & 3);
        public bool IsForbidden => Polarity == HexPolarity.Forbidden;

        public HexAction Action
        {
            get
            {
                ZxvException.ThrowZxv(Native.zxv_hex_action(Valid(), out int a));
                return (HexAction)a;
            }
        }

        public string Name => Native.Ansi(Native.zxv_hex_name(tag));

        public Truth ToTruth()
        {
            ZxvException.ThrowZxv(Native.zxv_hex_to_lp((byte)Base, out int lp));
            return (Truth)lp;
        }

        /// <summary>The same or the next phase only; a jump throws <see cref="ZxvStatus.State"/>.</summary>
        public Hexagram Step(HexPhase phase)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_step(Valid(), (byte)phase, out byte t));
            return new Hexagram(t);
        }

        /// <summary>The next phase on the cycle; cannot fail.</summary>
        public Hexagram Advance()
        {
            ZxvException.ThrowZxv(Native.zxv_hex_phase_next((byte)Phase, out byte next));
            return Step((HexPhase)next);
        }

        /// <summary>New base and polarity; a forbidden tag stays forbidden (<see cref="ZxvStatus.State"/>).</summary>
        public Hexagram WithState(HexBase b, HexPolarity polarity)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_set(Valid(), (byte)b, (byte)polarity, out byte t));
            return new Hexagram(t);
        }

        public Hexagram Forbid()
        {
            ZxvException.ThrowZxv(Native.zxv_hex_forbid(Valid(), out byte t));
            return new Hexagram(t);
        }

        public static Hexagram operator &(Hexagram a, Hexagram b)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_and(a.Valid(), b.Valid(), out byte t));
            return new Hexagram(t);
        }

        public static Hexagram operator |(Hexagram a, Hexagram b)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_or(a.Valid(), b.Valid(), out byte t));
            return new Hexagram(t);
        }

        public static Hexagram operator !(Hexagram a)
        {
            ZxvException.ThrowZxv(Native.zxv_hex_not(a.Valid(), out byte t));
            return new Hexagram(t);
        }

        private byte Valid()
        {
            if (tag > 63) throw new ZxvException(-1, "a hexagram tag is 0..63 (edited asset?)");
            return tag;
        }

        public bool Equals(Hexagram other) => tag == other.tag;
        public override bool Equals(object obj) => obj is Hexagram h && Equals(h);
        public override int GetHashCode() => tag;
        public static bool operator ==(Hexagram a, Hexagram b) => a.tag == b.tag;
        public static bool operator !=(Hexagram a, Hexagram b) => a.tag != b.tag;
        public override string ToString() => Name;
    }
}
