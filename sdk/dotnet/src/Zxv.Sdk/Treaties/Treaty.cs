// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Treaties;

/// <summary>
/// A treaty (ABI 1.5, <c>zxv_treaty</c>): one batch of settlement work. It
/// says who takes part, what each party deposits and may claim back, how the
/// batch's dividend is shared (each party's weight num/den), and who owes whom.
/// </summary>
/// <remarks>
/// <para>
/// A treaty has two exact spellings that hold the same content: the
/// fixed <see cref="RecordLength"/>-byte COBOL copybook record (ZXVTRTY.cpy,
/// plain ASCII) and compact Sutra text such as:
/// </para>
/// <code>
/// TREATY CORRIDOR-7 USD SETL
/// PARTY ACME 5000 4000 1/2
/// PARTY BETA 3000 3000 1/2
/// OWE ACME BETA 1200
/// END
/// </code>
/// <para>
/// Both spellings are canonical: parsing then emitting gives the input back
/// byte for byte, and the record and Sutra forms of one treaty compare equal
/// (<see cref="ContentEquals"/>). Parsing checks four rules: R1 the dividend
/// weights are in lowest terms and add up to exactly 1; R2 no claim exceeds
/// its deposit; R3 every amount fits PIC S9(18); R4 1..16 parties with
/// distinct ids and 0..64 obligations between two different known parties.
/// A refusal is a <see cref="TreatyException"/> with the fault and its offset.
/// </para>
/// <para>
/// Everything here is pure: no platform, no capability. <see cref="Run"/>
/// works in a private scratch engine that is freed before it returns, so it
/// changes no ledger the caller holds; its numbers are a deterministic
/// report, not a payment.
/// </para>
/// <para>
/// HONEST LIMITS. A treaty is a batch description, not a contract in law:
/// signatures, consent and authority live elsewhere. One asset per treaty.
/// The dividend shares are reported only; nothing is paid out of the bucket.
/// </para>
/// <para>
/// Thread-safe: the SDK serializes calls on one treaty (the native object is
/// not thread-safe on its own). Dispose it when done.
/// </para>
/// </remarks>
public sealed class Treaty : IDisposable
{
    /// <summary>The length of a copybook record in bytes (<c>ZXV_TREATY_REC_LEN</c>).</summary>
    public const int RecordLength = 2542;

    /// <summary>The length of a result record in bytes (<c>ZXV_TREATY_RESULT_LEN</c>).</summary>
    public const int ResultRecordLength = 2422;

    /// <summary>The longest canonical Sutra text in bytes (<c>ZXV_TREATY_SUTRA_MAX</c>).</summary>
    public const int MaxSutraBytes = 3843;

    /// <summary>The most parties in one treaty (<c>ZXV_TREATY_MAX_PARTIES</c>).</summary>
    public const int MaxParties = 16;

    /// <summary>The most obligations in one treaty (<c>ZXV_TREATY_MAX_OWES</c>).</summary>
    public const int MaxObligations = 64;

    /// <summary>The longest treaty name (<c>ZXV_TREATY_NAME_MAX</c>).</summary>
    public const int MaxNameLength = 30;

    /// <summary>The longest party id (<c>ZXV_TREATY_ID_MAX</c>).</summary>
    public const int MaxPartyIdLength = 8;

    /// <summary>The largest weight numerator or denominator (<c>ZXV_TREATY_WEIGHT_MAX</c>, PIC 9(9)).</summary>
    public const uint MaxWeight = 999_999_999;

    private readonly object _gate = new();
    private readonly ZxvTreatyHandle _handle;

    /// <summary>Initializes a new, empty treaty. It fails validation until a parse fills it.</summary>
    public Treaty()
    {
        ZxvLibrary.EnsureCompatible();
        int rc = NativeMethods.TreatyCreate(out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "Treaty.Create");
        }
    }

    /// <summary>Gets the treaty name ("" while empty).</summary>
    public unsafe string Name => Locked(() => NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.TreatyName(_handle, b, c, out l), Thrower("Treaty.Name")));

    /// <summary>Gets the asset: three capital letters ("" while empty).</summary>
    public unsafe string Asset => Locked(() => NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.TreatyAsset(_handle, b, c, out l), Thrower("Treaty.Asset")));

    /// <summary>Gets the operation (<see cref="TreatyOperation.None"/> while empty).</summary>
    public TreatyOperation Operation => (TreatyOperation)Header().Op;

    /// <summary>Gets the number of parties.</summary>
    public int PartyCount => (int)Header().Parties;

    /// <summary>Gets the number of obligations.</summary>
    public int ObligationCount => (int)Header().Owes;

    /// <summary>Gets the parties in treaty order (a snapshot).</summary>
    public IReadOnlyList<TreatyParty> Parties
    {
        get
        {
            lock (_gate)
            {
                var list = new TreatyParty[PartyCount];
                for (int i = 0; i < list.Length; i++)
                {
                    list[i] = GetParty(i);
                }

                return list;
            }
        }
    }

    /// <summary>Gets the obligations in treaty order (a snapshot).</summary>
    public IReadOnlyList<TreatyObligation> Obligations
    {
        get
        {
            lock (_gate)
            {
                var list = new TreatyObligation[ObligationCount];
                for (int i = 0; i < list.Length; i++)
                {
                    list[i] = GetObligation(i);
                }

                return list;
            }
        }
    }

    /// <summary>Parses canonical Sutra text into a new treaty.</summary>
    /// <param name="sutra">The text: single spaces, '\n' line ends, ending in "END\n" (at most <see cref="MaxSutraBytes"/> bytes).</param>
    /// <returns>The treaty; dispose it when done.</returns>
    /// <exception cref="TreatyException">The text breaks the grammar or a rule; <see cref="TreatyException.FaultOffset"/> is the byte offset of the first fault.</exception>
    /// <exception cref="ZxvUsageException">The text is too long (<see cref="ZxvStatus.TooLong"/>).</exception>
    public static Treaty ParseSutra(string sutra)
    {
        var t = new Treaty();
        try
        {
            t.LoadSutra(sutra);
            return t;
        }
        catch
        {
            t.Dispose();
            throw;
        }
    }

    /// <summary>Parses a copybook record of exactly <see cref="RecordLength"/> bytes into a new treaty.</summary>
    /// <param name="record">The record.</param>
    /// <returns>The treaty; dispose it when done.</returns>
    /// <exception cref="TreatyException">The record breaks a rule; <see cref="TreatyException.FaultOffset"/> is the byte offset in the record.</exception>
    /// <exception cref="ArgumentException">The record is not <see cref="RecordLength"/> bytes.</exception>
    public static Treaty ParseRecord(ReadOnlySpan<byte> record)
    {
        var t = new Treaty();
        try
        {
            t.LoadRecord(record);
            return t;
        }
        catch
        {
            t.Dispose();
            throw;
        }
    }

    /// <summary>Replaces this treaty's content with parsed Sutra text. On any failure the treaty is unchanged.</summary>
    /// <param name="sutra">The text.</param>
    /// <exception cref="TreatyException">The text breaks the grammar or a rule.</exception>
    public unsafe void LoadSutra(string sutra)
    {
        byte[] z = NativeBuffers.Utf8Z(sutra, nameof(sutra), allowEmpty: true);
        lock (_gate)
        {
            Enter();
            fixed (byte* p = z)
            {
                int rc = NativeMethods.TreatyParseSutra(_handle, p, out uint off, out int fault);
                Check(rc, "Treaty.ParseSutra", fault, off);
            }
        }
    }

    /// <summary>Replaces this treaty's content with a parsed copybook record. On any failure the treaty is unchanged.</summary>
    /// <param name="record">Exactly <see cref="RecordLength"/> bytes.</param>
    /// <exception cref="TreatyException">The record breaks a rule.</exception>
    public unsafe void LoadRecord(ReadOnlySpan<byte> record)
    {
        NativeBuffers.RequireLength(record, RecordLength, nameof(record));
        lock (_gate)
        {
            Enter();
            fixed (byte* p = record)
            {
                int rc = NativeMethods.TreatyParseRecord(_handle, p, (nuint)record.Length, out uint off, out int fault);
                Check(rc, "Treaty.ParseRecord", fault, off);
            }
        }
    }

    /// <summary>Empties the treaty again, as a new one.</summary>
    public void Clear()
    {
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.TreatyClear(_handle), "Treaty.Clear");
        }
    }

    /// <summary>Checks rules R1 to R4 and the spelling rules without throwing.</summary>
    /// <returns><see cref="TreatyFault.None"/> when valid, else the first fault (an empty treaty gives <see cref="TreatyFault.Name"/>).</returns>
    public TreatyFault Validate()
    {
        lock (_gate)
        {
            Enter();
            int rc = NativeMethods.TreatyValidate(_handle, out int fault);
            if (rc != (int)ZxvStatus.Ok && rc != (int)ZxvStatus.TreatyInvalid)
            {
                NativeCall.Check(rc, "Treaty.Validate");
            }

            return rc == (int)ZxvStatus.Ok ? TreatyFault.None : (TreatyFault)fault;
        }
    }

    /// <summary>The canonical Sutra text.</summary>
    /// <returns>The text, ending in "END\n".</returns>
    /// <exception cref="TreatyException">The treaty does not validate (an empty one, say).</exception>
    public unsafe string ToSutra() => Locked(() => NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.TreatyEmitSutra(_handle, b, c, out l), Thrower("Treaty.ToSutra")));

    /// <summary>The canonical copybook record: <see cref="RecordLength"/> bytes of ASCII, no NUL.</summary>
    /// <returns>The record.</returns>
    /// <exception cref="TreatyException">The treaty does not validate.</exception>
    public unsafe byte[] ToRecord() => Locked(() => NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.TreatyEmitRecord(_handle, b, c, out l), Thrower("Treaty.ToRecord")));

    /// <summary>Returns whether <paramref name="other"/> holds the same treaty: every field and every slot. Two empty treaties are equal.</summary>
    /// <param name="other">The other treaty.</param>
    /// <returns><see langword="true"/> when equal.</returns>
    public bool ContentEquals(Treaty other)
    {
        ArgumentNullException.ThrowIfNull(other);
        lock (_gate)
        {
            Enter();
            other.Enter();
            NativeCall.Check(NativeMethods.TreatyEqual(_handle, other._handle, out int eq), "Treaty.ContentEquals");
            return eq != 0;
        }
    }

    /// <summary>Reads party <paramref name="index"/> (0-based, in treaty order).</summary>
    /// <param name="index">The index.</param>
    /// <returns>The party.</returns>
    /// <exception cref="ZxvUsageException">No such party (<see cref="ZxvStatus.NotFound"/>).</exception>
    public unsafe TreatyParty GetParty(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        lock (_gate)
        {
            Enter();
            uint i = (uint)index;
            string id = NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.TreatyPartyId(_handle, i, b, c, out l), Thrower("Treaty.GetParty"));
            NativeCall.Check(NativeMethods.TreatyParty(_handle, i, out long dep, out long claim, out uint wn, out uint wd), "Treaty.GetParty");
            return new TreatyParty(id, dep, claim, wn, wd);
        }
    }

    /// <summary>Reads obligation <paramref name="index"/> (0-based).</summary>
    /// <param name="index">The index.</param>
    /// <returns>The obligation.</returns>
    /// <exception cref="ZxvUsageException">No such obligation (<see cref="ZxvStatus.NotFound"/>).</exception>
    public TreatyObligation GetObligation(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.TreatyOwe(_handle, (uint)index, out uint from, out uint to, out long amount), "Treaty.GetObligation");
            return new TreatyObligation((int)from, (int)to, amount);
        }
    }

    /// <summary>
    /// Runs the treaty in a fresh private engine for its asset: funds each
    /// deposit, nets the obligations (at most parties - 1 legs) and, for
    /// SETL, posts the legs with the 0.08889% assurance fee (all or nothing),
    /// pays each claim back out and reports each party's weight share of the
    /// fee's dividend bucket. Nothing persists and no capability is needed;
    /// the same treaty always gives the same result. Uses about 500 KB of
    /// native heap for the call.
    /// </summary>
    /// <returns>The result; dispose it when done. It does not depend on this treaty.</returns>
    /// <exception cref="TreatyException">The treaty does not validate.</exception>
    /// <exception cref="InsufficientFundsException">A payer cannot cover its legs plus the fee, or a claim exceeds what is left.</exception>
    /// <exception cref="ZxvUsageException">A net position leaves PIC S9(18), or an amount passes the engine's per-line limit of 2^59.</exception>
    public TreatyResult Run()
    {
        lock (_gate)
        {
            Enter();
            int rc = NativeMethods.TreatyRun(_handle, out ZxvTreatyResultHandle result);
            if (rc != (int)ZxvStatus.Ok)
            {
                result.Dispose();
                CheckWhole(rc, "Treaty.Run");
            }

            return new TreatyResult(result);
        }
    }

    /// <summary>Returns the canonical Sutra text, or a short description when the treaty does not validate.</summary>
    /// <returns>The text.</returns>
    public override string ToString() => Validate() == TreatyFault.None ? ToSutra() : $"Treaty(invalid: {Validate()})";

    /// <summary>Releases the native treaty.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    private static void Check(int rc, string operation, int fault, uint offset)
    {
        if (rc == (int)ZxvStatus.TreatyInvalid)
        {
            throw new TreatyException(operation, NativeText.LastError(), (TreatyFault)fault, checked((int)offset));
        }

        NativeCall.Check(rc, operation);
    }

    private Action<int> Thrower(string operation) => rc => CheckWhole(rc, operation);

    /// <summary>Maps a status from a call on the whole treaty; a refusal reports the treaty's first fault.</summary>
    private void CheckWhole(int rc, string operation)
    {
        if (rc == (int)ZxvStatus.TreatyInvalid)
        {
            string detail = NativeText.LastError();
            _ = NativeMethods.TreatyValidate(_handle, out int fault);
            throw new TreatyException(operation, detail, (TreatyFault)fault, 0);
        }

        NativeCall.Check(rc, operation);
    }

    private (int Op, uint Parties, uint Owes) Header()
    {
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.TreatyHeader(_handle, out int op, out uint p, out uint o), "Treaty.Header");
            return (op, p, o);
        }
    }

    private T Locked<T>(Func<T> read)
    {
        lock (_gate)
        {
            Enter();
            return read();
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
