// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Diagnostics.HealthChecks;

namespace Zxv.Gateway.Infrastructure;

/// <summary>Ready when the native core is loaded and every ledger invariant holds.</summary>
internal sealed class LedgerHealthCheck : IHealthCheck
{
    private readonly ZxvPlatform _zxv;

    public LedgerHealthCheck(ZxvPlatform zxv) => _zxv = zxv;

    public Task<HealthCheckResult> CheckHealthAsync(HealthCheckContext context, CancellationToken cancellationToken = default)
    {
        var data = new Dictionary<string, object>
        {
            ["abi"] = _zxv.AbiVersion.ToString(),
            ["features"] = _zxv.Features.ToString(),
        };
        return Task.FromResult(_zxv.Ledger.TryVerify(out string detail)
            ? HealthCheckResult.Healthy("Ledger invariants hold.", data)
            : HealthCheckResult.Unhealthy("Ledger invariant failed: " + detail, data: data));
    }
}
