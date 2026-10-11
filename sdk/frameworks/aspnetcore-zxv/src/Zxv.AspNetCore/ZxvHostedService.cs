// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace Zxv.AspNetCore;

/// <summary>
/// Creates the platform, the root capability and every named capability when
/// the host starts, before the server accepts requests, so a bad
/// configuration or a missing native library fails start-up instead of the
/// first request. Disposal is left to the container (see
/// <see cref="ZxvServiceCollectionExtensions.AddZxv"/>).
/// </summary>
internal sealed partial class ZxvHostedService : IHostedService
{
    private readonly IServiceProvider _services;
    private readonly ILogger<ZxvHostedService> _logger;

    public ZxvHostedService(IServiceProvider services, ILogger<ZxvHostedService> logger)
    {
        _services = services;
        _logger = logger;
    }

    public Task StartAsync(CancellationToken cancellationToken)
    {
        ZxvPlatform platform = _services.GetRequiredService<ZxvPlatform>();
        _ = _services.GetRequiredKeyedService<Capability>(ZxvServiceKeys.Root);
        ZxvCapabilities caps = _services.GetRequiredService<ZxvCapabilities>();
        foreach (string name in caps.Names)
        {
            _ = caps.Get(name);
        }

        Started(_logger, platform.AbiVersion.ToString(), caps.Names.Count);
        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    [LoggerMessage(EventId = 3000, Level = LogLevel.Information, Message = "ZXV platform started (ABI {Abi}) with {Count} named capabilities.")]
    private static partial void Started(ILogger logger, string abi, int count);
}
