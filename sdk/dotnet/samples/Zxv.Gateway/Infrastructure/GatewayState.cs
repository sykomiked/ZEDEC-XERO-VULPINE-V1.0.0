// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using Zxv.Ledger;
using Zxv.Netting;
using Zxv.Payments;

namespace Zxv.Gateway.Infrastructure;

/// <summary>
/// In-memory state of the sample: the operator capability, receipts by
/// EndToEndId and open netting cycles. A production gateway persists receipts
/// in its own store.
/// </summary>
internal sealed class GatewayState : IDisposable
{
    public GatewayState(ZxvPlatform platform)
    {
        ArgumentNullException.ThrowIfNull(platform);

        // ABI 1.2: every gated call carries a capability. The operator API
        // reads and posts on the ledger, and nothing more.
        Operator = platform.CreateRootCapability(CapabilityRights.LedgerRead | CapabilityRights.LedgerPost);
        Ledger = platform.Ledger.WithCapability(Operator);
    }

    /// <summary>Gets the capability the operator API presents on every gated call.</summary>
    public Capability Operator { get; }

    /// <summary>Gets the ledger seen through <see cref="Operator"/>.</summary>
    public CapabilityLedger Ledger { get; }

    public ConcurrentDictionary<string, PaymentReceipt> Payments { get; } = new(StringComparer.Ordinal);

    public ConcurrentDictionary<string, NettingCycle> Cycles { get; } = new(StringComparer.Ordinal);

    public void Dispose()
    {
        foreach (NettingCycle c in Cycles.Values)
        {
            c.Dispose();
        }

        Cycles.Clear();
        Operator.Dispose();
    }
}
