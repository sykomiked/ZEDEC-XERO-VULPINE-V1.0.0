// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Zxv.Cards;
using Zxv.Conformance;
using Zxv.Currencies;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Messaging;
using Zxv.Netting;
using Zxv.Payments;

namespace Zxv;

/// <summary>
/// One instance of the ZXV platform core: a ledger, its currency
/// configuration and the services over them. Thread-safe. Register it as a
/// singleton (<c>services.AddZxv(...)</c>) or create one with <see cref="Create"/>.
/// </summary>
/// <remarks>
/// <para>
/// State is in-process and in-memory. Disposing the platform releases it.
/// Calls that are in flight on other threads when <see cref="Dispose"/> runs
/// complete safely; later calls throw <see cref="ObjectDisposedException"/>.
/// </para>
/// <para>
/// No network connectivity is included: the platform books and serializes,
/// and an <see cref="IMxTransport"/> supplied through scheme onboarding (PAPSS,
/// SWIFT, CIPS, an RTGS) carries the messages.
/// </para>
/// </remarks>
public sealed class ZxvPlatform : IDisposable
{
    private readonly ZxvContextHandle _ctx;
    private readonly ILogger _logger;

    /// <summary>Initializes a new platform from options (the DI constructor).</summary>
    /// <param name="options">The options.</param>
    /// <param name="logger">Logger; optional.</param>
    /// <param name="transport">Message transport; defaults to <see cref="NullMxTransport"/>.</param>
    /// <param name="timeProvider">Clock; defaults to <see cref="TimeProvider.System"/>.</param>
    public ZxvPlatform(IOptions<ZxvOptions> options, ILogger<ZxvPlatform>? logger = null, IMxTransport? transport = null, TimeProvider? timeProvider = null)
    {
        ArgumentNullException.ThrowIfNull(options);
        ZxvOptions o = options.Value;
        ValidateOptionsResult validation = new ZxvOptionsValidator().Validate(Options.DefaultName, o);
        if (validation.Failed)
        {
            throw new OptionsValidationException(Options.DefaultName, typeof(ZxvOptions), validation.Failures);
        }

        _logger = (ILogger?)logger ?? NullLogger.Instance;
        TimeProvider time = timeProvider ?? TimeProvider.System;

        NativeCall.Check(NativeMethods.Version(out uint major, out uint minor, out uint patch), "Platform.Version");
        if (major != SupportedAbiMajor)
        {
            throw new ZxvException($"libzxv ABI {major}.{minor}.{patch} is not supported; this SDK needs {SupportedAbiMajor}.x.");
        }

        AbiVersion = new Version((int)major, (int)minor, (int)patch);
        NativeCall.Check(NativeMethods.Features(out uint mask), "Platform.Features");
        Features = (ZxvFeatures)mask;

        NativeCall.Check(NativeMethods.CtxCreate(out _ctx), "Platform.Create");
        try
        {
            var ids = new IdGenerator(o.IdentifierPrefix, time);
            Currencies = new CurrencyService(_ctx);
            Ledger = new LedgerService(_ctx, Currencies, _logger);
            Messages = new MessageService(_ctx, _logger);
            Payments = new PaymentService(Ledger, Messages, transport ?? NullMxTransport.Instance, ids, time, o.IdentifierPrefix, _logger);
            Statements = new StatementService(Ledger, Messages, Payments, ids, time);
            Netting = new NettingService(_ctx, Currencies);
            Cards = new CardService(_ctx);
            Conformance = new ConformanceService(_ctx);
            Configure(o);
        }
        catch
        {
            _ctx.Dispose();
            throw;
        }

        Log.PlatformStarted(_logger, AbiVersion.ToString(), Features);
    }

    /// <summary>The major version of the C ABI this SDK was written against.</summary>
    public const int SupportedAbiMajor = 1;

    /// <summary>Gets the version of the loaded native C ABI.</summary>
    public Version AbiVersion { get; }

    /// <summary>Gets the feature groups live in the loaded native library.</summary>
    public ZxvFeatures Features { get; }

    /// <summary>Gets ISO 4217 reference data and currency configuration.</summary>
    public CurrencyService Currencies { get; }

    /// <summary>Gets the ledger.</summary>
    public LedgerService Ledger { get; }

    /// <summary>Gets the ISO 20022 serializers.</summary>
    public MessageService Messages { get; }

    /// <summary>Gets customer credit transfers (ledger + pacs.008 + transport).</summary>
    public PaymentService Payments { get; }

    /// <summary>Gets camt.053 statements.</summary>
    public StatementService Statements { get; }

    /// <summary>Gets multilateral netting (pending kernel/src/cbank).</summary>
    public NettingService Netting { get; }

    /// <summary>Gets charge cards (check digits live; lifecycle pending kernel/src/cardnet).</summary>
    public CardService Cards { get; }

    /// <summary>Gets VSS conformance checks (pending kernel/src/cbank).</summary>
    public ConformanceService Conformance { get; }

    /// <summary>Creates a platform without dependency injection.</summary>
    /// <param name="configure">Configures the options.</param>
    /// <param name="loggerFactory">Optional logger factory.</param>
    /// <param name="transport">Optional transport.</param>
    /// <returns>The platform; dispose it when done.</returns>
    public static ZxvPlatform Create(Action<ZxvOptions>? configure = null, ILoggerFactory? loggerFactory = null, IMxTransport? transport = null)
    {
        var o = new ZxvOptions();
        configure?.Invoke(o);
        return new ZxvPlatform(Options.Create(o), loggerFactory?.CreateLogger<ZxvPlatform>(), transport);
    }

    /// <summary>Returns whether every flag in <paramref name="features"/> is live.</summary>
    /// <param name="features">The features to test.</param>
    /// <returns><see langword="true"/> when all are available.</returns>
    public bool Supports(ZxvFeatures features) => (Features & features) == features;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (!_ctx.IsClosed)
        {
            _ctx.Dispose();
            Log.PlatformDisposed(_logger);
        }
    }

    private void Configure(ZxvOptions o)
    {
        foreach (string code in o.Currencies)
        {
            Currency c = o.MinorUnits.TryGetValue(code, out byte mu) ? Currencies.Enable(code, mu) : Currencies.Enable(code);
            Log.CurrencyEnabled(_logger, c.Code, c.MinorUnits);
        }

        foreach (KeyValuePair<string, byte> kv in o.MinorUnits)
        {
            if (!o.Currencies.Contains(kv.Key))
            {
                throw new OptionsValidationException(Options.DefaultName, typeof(ZxvOptions), new[] { $"MinorUnits: '{kv.Key}' is not listed in Currencies." });
            }
        }

        foreach (PrivateUnitOptions p in o.PrivateUnits)
        {
            Currencies.RegisterPrivateUnit(p.Code, p.MinorUnits);
        }
    }
}
