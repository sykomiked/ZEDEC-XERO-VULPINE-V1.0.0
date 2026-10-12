// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Treaties;

/// <summary>
/// What running a <see cref="Treaty"/> returned (<c>zxv_treaty_result</c>):
/// the netted legs, the fee and each party's position. Immutable, so any
/// number of threads may read it. It does not depend on the treaty it came
/// from; either may be disposed first.
/// </summary>
/// <remarks>The numbers are a deterministic report from a private scratch engine, not a payment.</remarks>
public sealed class TreatyResult : IDisposable
{
    private readonly ZxvTreatyResultHandle _handle;

    internal TreatyResult(ZxvTreatyResultHandle handle)
    {
        _handle = handle;
    }

    /// <summary>Gets the number of parties (as in the treaty).</summary>
    public int PartyCount => (int)Summary().Parties;

    /// <summary>Gets the number of netted legs (at most parties - 1).</summary>
    public int LegCount => (int)Summary().Legs;

    /// <summary>Gets the total assurance fee charged, in minor units of the asset (0 for NET).</summary>
    public long TotalFees => Summary().Fees;

    /// <summary>Gets every party's line, in treaty order.</summary>
    public IReadOnlyList<TreatyResultParty> Parties
    {
        get
        {
            var list = new TreatyResultParty[PartyCount];
            for (int i = 0; i < list.Length; i++)
            {
                list[i] = GetParty(i);
            }

            return list;
        }
    }

    /// <summary>Gets every netted leg.</summary>
    public IReadOnlyList<TreatyLeg> Legs
    {
        get
        {
            var list = new TreatyLeg[LegCount];
            for (int i = 0; i < list.Length; i++)
            {
                list[i] = GetLeg(i);
            }

            return list;
        }
    }

    /// <summary>Reads party <paramref name="index"/>'s line (treaty order).</summary>
    /// <param name="index">The 0-based party index.</param>
    /// <returns>Net, balance and dividend share.</returns>
    /// <exception cref="ZxvUsageException">No such party (<see cref="ZxvStatus.NotFound"/>).</exception>
    public TreatyResultParty GetParty(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter();
        NativeCall.Check(NativeMethods.TreatyResultParty(_handle, (uint)index, out long net, out long bal, out long div), "TreatyResult.GetParty");
        return new TreatyResultParty(net, bal, div);
    }

    /// <summary>Reads netted leg <paramref name="index"/>.</summary>
    /// <param name="index">The 0-based leg index.</param>
    /// <returns>The leg.</returns>
    /// <exception cref="ZxvUsageException">No such leg (<see cref="ZxvStatus.NotFound"/>).</exception>
    public TreatyLeg GetLeg(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter();
        NativeCall.Check(NativeMethods.TreatyResultLeg(_handle, (uint)index, out uint from, out uint to, out long amount), "TreatyResult.GetLeg");
        return new TreatyLeg((int)from, (int)to, amount);
    }

    /// <summary>
    /// The ZXVRSLT.cpy result record: <see cref="Treaty.ResultRecordLength"/>
    /// bytes of ASCII. Party numbers inside it are 1-based, as the copybook says.
    /// </summary>
    /// <returns>The record.</returns>
    public unsafe byte[] ToRecord()
    {
        Enter();
        return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.TreatyResultRecord(_handle, b, c, out l), rc => NativeCall.Check(rc, "TreatyResult.ToRecord"));
    }

    /// <summary>Releases the native result.</summary>
    public void Dispose() => _handle.Dispose();

    private (uint Parties, uint Legs, long Fees) Summary()
    {
        Enter();
        NativeCall.Check(NativeMethods.TreatyResultSummary(_handle, out uint p, out uint l, out long f), "TreatyResult.Summary");
        return (p, l, f);
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
