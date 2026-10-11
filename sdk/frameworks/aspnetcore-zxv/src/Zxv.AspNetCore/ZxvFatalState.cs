// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Threading;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

namespace Zxv.AspNetCore;

/// <summary>
/// Records that the ZXV context halted. Tripped by the problem-details
/// middleware when a request meets <see cref="ZxvFatalException"/> and by the
/// health check when it finds the context poisoned. The context itself stays
/// poisoned in C whatever the application does; this only makes the halt
/// visible and, with <see cref="ZxvAspNetCoreOptions.StopApplicationOnFatal"/>,
/// stops the host.
/// </summary>
public sealed partial class ZxvFatalState
{
    private readonly IHostApplicationLifetime? _lifetime;
    private readonly ILogger _logger;
    private readonly bool _stop;
    private int _tripped;

    /// <summary>Initializes a new instance.</summary>
    /// <param name="options">The options.</param>
    /// <param name="lifetime">The host lifetime; optional.</param>
    /// <param name="logger">The logger; optional.</param>
    public ZxvFatalState(IOptions<ZxvAspNetCoreOptions>? options = null, IHostApplicationLifetime? lifetime = null, ILogger<ZxvFatalState>? logger = null)
    {
        _lifetime = lifetime;
        _logger = (ILogger?)logger ?? NullLogger.Instance;
        _stop = options?.Value.StopApplicationOnFatal ?? false;
    }

    /// <summary>Gets a value indicating whether a halt has been seen.</summary>
    public bool IsTripped => Volatile.Read(ref _tripped) != 0;

    /// <summary>Marks the halt. Idempotent: logs and (optionally) stops the host once.</summary>
    /// <param name="statusName">The status name (e.g. <c>ZXV_E_POISONED</c>), never detail text.</param>
    public void Trip(string statusName)
    {
        if (Interlocked.Exchange(ref _tripped, 1) == 0)
        {
            Halted(_logger, statusName, _stop);
            if (_stop)
            {
                _lifetime?.StopApplication();
            }
        }
    }

    [LoggerMessage(EventId = 3100, Level = LogLevel.Critical, Message = "ZXV context halted ({StatusName}); every ZXV call now fails. Stopping the application: {Stopping}. Restart from trusted state.")]
    private static partial void Halted(ILogger logger, string statusName, bool stopping);
}
