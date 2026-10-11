// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Threading;
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
    private readonly ContextRef _context;
    private readonly Capability _service;
    private readonly ILogger _logger;
    private readonly CurrencyService _currencies;
    private readonly LedgerService _ledger;
    private readonly MessageService _messages;
    private readonly PaymentService _payments;
    private readonly StatementService _statements;
    private readonly NettingService _netting;
    private readonly CardService _cards;
    private readonly ConformanceService _conformance;
    private int _disposed;

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

        AbiVersion = ZxvLibrary.AbiVersion;
        Features = ZxvLibrary.Features;

        NativeCall.Check(NativeMethods.CtxCreate(out _ctx), "Platform.Create");
        _context = new ContextRef(_ctx);
        Capability? service = null;
        try
        {
            // The platform's own capability, never exposed: the members that
            // take no capability present it, so every native call the platform
            // services make is an ABI 1.2 *_cap form (R10).
            service = Capability.CreateRoot(_context, ServiceRights, 0, default);
            _service = service;
            var ids = new IdGenerator(o.IdentifierPrefix, time);
            _currencies = new CurrencyService(_context, service);
            _ledger = new LedgerService(_context, _currencies, service, _logger);
            _messages = new MessageService(_context, service, _logger);
            _payments = new PaymentService(_ledger, _messages, transport ?? NullMxTransport.Instance, ids, time, o.IdentifierPrefix, _logger);
            _statements = new StatementService(_ledger, _messages, _payments, ids, time);
            _netting = new NettingService(_context, _currencies);
            _cards = new CardService(_context);
            _conformance = new ConformanceService(_context);
            Configure(o);
        }
        catch
        {
            service?.Dispose();
            _ctx.Dispose();
            throw;
        }

        Log.PlatformStarted(_logger, AbiVersion.ToString(), Features);
    }

    /// <summary>The rights of the platform's own capability, presented by the members that take none.</summary>
    private const CapabilityRights ServiceRights = CapabilityRights.LedgerRead | CapabilityRights.LedgerPost;

    /// <summary>The major version of the C ABI this SDK was written against.</summary>
    public const int SupportedAbiMajor = ZxvLibrary.SupportedAbiMajor;

    /// <summary>Gets the version of the loaded native C ABI.</summary>
    public Version AbiVersion { get; }

    /// <summary>Gets the feature groups live in the loaded native library.</summary>
    public ZxvFeatures Features { get; }

    /// <summary>Gets ISO 4217 reference data and currency configuration.</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public CurrencyService Currencies => Live(_currencies);

    /// <summary>Gets the ledger.</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public LedgerService Ledger => Live(_ledger);

    /// <summary>Gets the ISO 20022 serializers.</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public MessageService Messages => Live(_messages);

    /// <summary>Gets customer credit transfers (ledger + pacs.008 + transport).</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public PaymentService Payments => Live(_payments);

    /// <summary>Gets camt.053 statements.</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public StatementService Statements => Live(_statements);

    /// <summary>Gets multilateral netting (pending kernel/src/cbank).</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public NettingService Netting => Live(_netting);

    /// <summary>Gets charge cards (check digits live; lifecycle pending kernel/src/cardnet).</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public CardService Cards => Live(_cards);

    /// <summary>Gets VSS conformance checks (pending kernel/src/cbank).</summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public ConformanceService Conformance => Live(_conformance);

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

    /// <summary>
    /// Gets a value indicating whether the context has halted (a broken
    /// invariant was rolled back, or <see cref="Halt(string)"/> was called).
    /// A halted platform refuses every call with <see cref="ZxvFatalException"/>.
    /// </summary>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public bool IsHalted
    {
        get
        {
            ThrowIfDisposed();
            if (_context.IsHalted)
            {
                return true;
            }

            NativeCall.Check(NativeMethods.CtxIsPoisoned(_ctx, out int poisoned), "Platform.IsHalted");
            return poisoned != 0;
        }
    }

    /// <summary>The internal context reference shared with ABI 1.1 objects.</summary>
    internal ContextRef Context => _context;

    /// <summary>
    /// Halts the context on purpose: an operator kill switch. Afterwards it
    /// behaves exactly like a context that broke an invariant: every call
    /// raises <see cref="ZxvFatalException"/> and it can only be disposed.
    /// </summary>
    /// <param name="reason">Why (at most 150 bytes of UTF-8; longer text is cut).</param>
    public void Halt(string reason)
    {
        ThrowIfDisposed();
        Guard.Text(reason, nameof(reason), allowEmpty: true);
        NativeCall.Check(NativeMethods.CtxHalt(_ctx, reason), "Platform.Halt");
        _context.MarkHalted();
    }

    /// <summary>
    /// Creates a root capability for this platform. Hand narrower ones
    /// (<see cref="Capability.Attenuate(CapabilityRights)"/>) to the parts of a
    /// program that need less. Dispose it after every object that uses it.
    /// </summary>
    /// <param name="rights">The rights to hold; default every right.</param>
    /// <param name="processId">The process id recorded in the capability.</param>
    /// <param name="identity">A 21-byte (168-bit) identity, or empty for zeros.</param>
    /// <returns>The capability, with attestation <see cref="Truth.True"/>.</returns>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    public Capability CreateRootCapability(CapabilityRights rights = CapabilityRights.All, uint processId = 0, ReadOnlySpan<byte> identity = default)
    {
        ThrowIfDisposed();
        return Capability.CreateRoot(_context, rights, processId, identity);
    }

    /// <summary>Creates an attestation registry (<c>lpres</c>) that attests through <paramref name="capability"/>.</summary>
    /// <param name="capability">A capability of this platform; attesting needs <see cref="CapabilityRights.Attest"/>.</param>
    /// <returns>The registry; dispose it when done.</returns>
    public Attestations CreateAttestations(Capability capability) => new(RequireOwn(capability));

    /// <summary>Creates a quarantine log for forbidden work (ABI 1.3, <c>zxv_hexlog</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Attest"/>; appends go through it.</param>
    /// <param name="capacity">The most records it holds, 1..<see cref="QuarantineLog.MaxRecords"/>.</param>
    /// <returns>The log; dispose it when done.</returns>
    public QuarantineLog CreateQuarantineLog(Capability capability, int capacity) => new(RequireOwn(capability), capacity);

    /// <summary>Creates a set of exact rational resource budgets (<c>rmag</c>).</summary>
    /// <param name="capability">A capability of this platform; budget calls need <see cref="CapabilityRights.Budget"/>.</param>
    /// <returns>The budgets; dispose them when done.</returns>
    public Budgets CreateBudgets(Capability capability) => new(RequireOwn(capability));

    /// <summary>Creates a phase coordinator clock starting at tick 0.</summary>
    /// <param name="capability">A capability of this platform; advancing, admitting and deferring need <see cref="CapabilityRights.Phase"/>.</param>
    /// <returns>The clock; dispose it when done.</returns>
    public PhaseClock CreatePhaseClock(Capability capability) => new(RequireOwn(capability));

    /// <summary>Creates a swarm token economy.</summary>
    /// <param name="capability">A capability of this platform; swarm calls need <see cref="CapabilityRights.Swarm"/>.</param>
    /// <param name="levels">Number of Fibonacci levels (level d holds at most F(d+2) models).</param>
    /// <param name="tokensPerCycle">Tokens shared out each cycle.</param>
    /// <returns>The swarm; dispose it when done.</returns>
    public Swarm CreateSwarm(Capability capability, int levels, ulong tokensPerCycle) => new(RequireOwn(capability), levels, tokensPerCycle);

    /// <summary>
    /// Disposes the platform. From now on its members throw
    /// <see cref="ObjectDisposedException"/>, even while objects created from
    /// it (capabilities, budgets, clocks, logs, ...) are still alive.
    /// </summary>
    /// <remarks>
    /// Native teardown follows ABI.md R6: each child handle keeps a reference
    /// on the context, so the native context is destroyed only after the last
    /// child created from it is disposed (or finalized). Those children stay
    /// usable until then; dispose them before, or soon after, the platform.
    /// </remarks>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _context.MarkDisposed();
        _service.Dispose();
        _ctx.Dispose();
        Log.PlatformDisposed(_logger);
    }

    private T Live<T>(T service)
    {
        ThrowIfDisposed();
        return service;
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);

    private Capability RequireOwn(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        ThrowIfDisposed();
        if (!ReferenceEquals(capability.Context, _context))
        {
            throw new ArgumentException("The capability belongs to a different platform.", nameof(capability));
        }

        return capability;
    }

    private void Configure(ZxvOptions o)
    {
        foreach (string code in o.Currencies)
        {
            Currency c = _currencies.EnableUnchecked(code, o.MinorUnits.TryGetValue(code, out byte mu) ? mu : null);
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
            _currencies.RegisterPrivateUnitUnchecked(p.Code, p.MinorUnits);
        }
    }
}
