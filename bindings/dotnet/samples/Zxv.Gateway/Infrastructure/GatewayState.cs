// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using Zxv.Netting;
using Zxv.Payments;

namespace Zxv.Gateway.Infrastructure;

/// <summary>
/// In-memory state of the sample: receipts by EndToEndId and open netting
/// cycles. A production gateway persists receipts in its own store.
/// </summary>
internal sealed class GatewayState : IDisposable
{
    public ConcurrentDictionary<string, PaymentReceipt> Payments { get; } = new(StringComparer.Ordinal);

    public ConcurrentDictionary<string, NettingCycle> Cycles { get; } = new(StringComparer.Ordinal);

    public void Dispose()
    {
        foreach (NettingCycle c in Cycles.Values)
        {
            c.Dispose();
        }

        Cycles.Clear();
    }
}
