// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Netting;

/// <summary>A participant's net position after a cycle closes.</summary>
/// <param name="Participant">Participant code (e.g. a BIC).</param>
/// <param name="Net">Positive receives, negative pays. Positions sum to zero.</param>
public sealed record NetPosition(string Participant, Money Net);

/// <summary>
/// Multilateral netting cycles. Obtain it from <see cref="ZxvPlatform.Netting"/>.
/// </summary>
/// <remarks>
/// Pending kernel/src/cbank: until the native library reports
/// <see cref="ZxvFeatures.Netting"/>, <see cref="OpenCycle"/> throws
/// <see cref="ZxvFeatureNotAvailableException"/>.
/// </remarks>
public sealed class NettingService
{
    private readonly ZxvContextHandle _ctx;
    private readonly Currencies.CurrencyService _currencies;

    internal NettingService(ZxvContextHandle ctx, Currencies.CurrencyService currencies)
    {
        _ctx = ctx;
        _currencies = currencies;
    }

    /// <summary>Opens a netting cycle in one settlement currency.</summary>
    /// <param name="cycleId">Cycle identifier.</param>
    /// <param name="currency">ISO 4217 settlement currency.</param>
    /// <returns>The open cycle; dispose it when done.</returns>
    public NettingCycle OpenCycle(string cycleId, string currency)
    {
        Guard.Text(cycleId, nameof(cycleId));
        Guard.Text(currency, nameof(currency));
        NativeCall.Check(NativeMethods.NettingOpen(_ctx, cycleId, currency, out ZxvNettingHandle h), "Netting.OpenCycle");
        return new NettingCycle(_ctx, h, cycleId, _currencies.GetEnabled(currency));
    }
}

/// <summary>An open netting cycle. Not thread-safe: use from one thread at a time.</summary>
public sealed class NettingCycle : IDisposable
{
    private readonly ZxvContextHandle _ctx;
    private readonly ZxvNettingHandle _handle;
    private readonly Currencies.Currency _currency;
    private bool _ctxRef;

    internal NettingCycle(ZxvContextHandle ctx, ZxvNettingHandle handle, string cycleId, Currencies.Currency currency)
    {
        _ctx = ctx;
        _handle = handle;
        _currency = currency;
        CycleId = cycleId;
        _ctx.DangerousAddRef(ref _ctxRef); // R6: the cycle must not outlive the context.
    }

    /// <summary>Gets the cycle identifier.</summary>
    public string CycleId { get; }

    /// <summary>Gets the settlement currency of the cycle.</summary>
    public Currencies.Currency Currency => _currency;

    /// <summary>Adds a participant.</summary>
    /// <param name="participant">Participant code.</param>
    public void AddParticipant(string participant)
    {
        Guard.Text(participant, nameof(participant));
        NativeCall.Check(NativeMethods.NettingAddParticipant(_handle, participant), "Netting.AddParticipant");
    }

    /// <summary>Submits one obligation; idempotent on <paramref name="paymentId"/>.</summary>
    /// <param name="paymentId">Payment identifier (e.g. the UETR or EndToEndId).</param>
    /// <param name="debtor">Paying participant.</param>
    /// <param name="creditor">Receiving participant.</param>
    /// <param name="amount">Positive amount in the cycle currency.</param>
    public void Submit(string paymentId, string debtor, string creditor, Money amount)
    {
        Guard.Text(paymentId, nameof(paymentId));
        Guard.Text(debtor, nameof(debtor));
        Guard.Text(creditor, nameof(creditor));
        if (!string.Equals(amount.Currency, _currency.Code, StringComparison.Ordinal))
        {
            throw new ArgumentException($"The cycle settles in {_currency.Code}.", nameof(amount));
        }

        NativeCall.Check(NativeMethods.NettingSubmit(_handle, paymentId, debtor, creditor, amount.MinorUnits), "Netting.Submit");
    }

    /// <summary>Closes the cycle and returns the net positions.</summary>
    /// <returns>Net positions (sum to zero).</returns>
    public unsafe IReadOnlyList<NetPosition> Close()
    {
        NativeCall.Check(NativeMethods.NettingClose(_handle), "Netting.Close");
        NativeCall.Check(NativeMethods.NettingPositionCount(_handle, out uint count), "Netting.Close");
        var list = new List<NetPosition>((int)count);
        byte scale = _currency.MinorUnits ?? 0;
        for (uint i = 0; i < count; i++)
        {
            uint index = i;
            long net = 0;
            string p = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.NettingPositionAt(_handle, index, b, c, out l, out net), "Netting.Close");
            list.Add(new NetPosition(p, new Money(net, _currency.Code, scale)));
        }

        return list;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        _handle.Dispose();
        if (_ctxRef)
        {
            _ctxRef = false;
            _ctx.DangerousRelease();
        }
    }
}
