// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using Zxv.Ledger;

namespace Zxv.AspNetCore.Tests;

/// <summary>Start-up, keyed capabilities, disposal order and health, on a generic host with the real libzxv.</summary>
public sealed class HostingTests
{
    private static IHost Build(Action<ZxvAspNetCoreOptions> configure, Action<IServiceCollection>? more = null)
    {
        HostApplicationBuilder b = Host.CreateApplicationBuilder();
        b.Services.AddZxv(configure);
        b.Services.AddHealthChecks().AddZxv();
        more?.Invoke(b.Services);
        return b.Build();
    }

    private static void TellerAuditor(ZxvAspNetCoreOptions o)
    {
        o.Platform.Currencies.Add("NGN");
        o.Capabilities["teller"] = CapabilityRights.LedgerRead | CapabilityRights.LedgerPost;
        o.Capabilities["auditor"] = CapabilityRights.LedgerRead;
    }

    [Fact]
    public async Task Starts_a_live_platform_with_narrow_keyed_capabilities()
    {
        using IHost host = Build(TellerAuditor);
        await host.StartAsync();

        ZxvPlatform zxv = host.Services.GetRequiredService<ZxvPlatform>();
        Assert.False(zxv.IsHalted);
        Capability teller = host.Services.GetRequiredKeyedService<Capability>("teller");
        Capability auditor = host.Services.GetRequiredKeyedService<Capability>("auditor");
        Assert.Equal(CapabilityRights.LedgerRead | CapabilityRights.LedgerPost, teller.Rights);
        Assert.Equal(CapabilityRights.LedgerRead, auditor.Rights);
        Assert.False(teller.Allows(CapabilityRights.CryptoSign));

        CapabilityLedger post = zxv.Ledger.WithCapability(teller);
        Account bank = post.OpenAccount("bank", "NGN", AccountKind.Issuer);
        Account ada = post.OpenAccount("ada", "NGN", AccountKind.Holder);
        post.Post(new Transfer(bank.Id, ada.Id, Money.FromMinorUnits(150_00, zxv.Currencies.GetEnabled("NGN")), "E2E-1"));
        Assert.Equal(150_00, zxv.Ledger.WithCapability(auditor).GetBalances(ada.Id).Equity.MinorUnits);
        Assert.Throws<CapabilityDeniedException>(() =>
            zxv.Ledger.WithCapability(auditor).Post(new Transfer(ada.Id, bank.Id, Money.FromMinorUnits(1, zxv.Currencies.GetEnabled("NGN")), "E2E-2")));

        Assert.Equal(new[] { "auditor", "teller" }, host.Services.GetRequiredService<ZxvCapabilities>().Names);
        await host.StopAsync();
    }

    [Fact]
    public void Root_capability_is_never_injectable()
    {
        using IHost host = Build(TellerAuditor);
        Assert.Null(host.Services.GetService<Capability>());
        Assert.Throws<KeyNotFoundException>(() => host.Services.GetRequiredService<ZxvCapabilities>().Get("root"));
    }

    [Fact]
    public void Reads_the_configuration_section()
    {
        IConfiguration config = new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string, string?>
        {
            ["Zxv:Currencies:0"] = "NGN",
            ["Zxv:Currencies:1"] = "KES",
            ["Zxv:PrivateUnits:0:Code"] = "VFV",
            ["Zxv:PrivateUnits:0:MinorUnits"] = "2",
            ["Zxv:RootRights"] = "LedgerRead, LedgerPost, CryptoSign",
            ["Zxv:Capabilities:teller"] = "ledgerread, LedgerPost",
            ["Zxv:Capabilities:signer"] = "CryptoSign",
        }).Build();
        using IHost host = Build(o => o.ReadFrom(config.GetSection("Zxv")));
        ZxvAspNetCoreOptions opts = host.Services.GetRequiredService<IOptions<ZxvAspNetCoreOptions>>().Value;
        Assert.Equal(new[] { "NGN", "KES" }, opts.Platform.Currencies);
        Assert.Equal(CapabilityRights.CryptoSign, host.Services.GetRequiredKeyedService<Capability>("signer").Rights);
        Assert.Equal("VFV", host.Services.GetRequiredService<ZxvPlatform>().Currencies.GetEnabled("VFV").Code);
    }

    [Theory]
    [InlineData("LedgerRead, Root")]
    [InlineData("1")]
    [InlineData("LedgerRead|LedgerPost")]
    public void Refuses_malformed_rights(string text)
    {
        IConfiguration config = new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string, string?>
        {
            ["Zxv:Capabilities:x"] = text,
        }).Build();
        Assert.Throws<FormatException>(() => Build(o => o.ReadFrom(config.GetSection("Zxv"))));
    }

    [Fact]
    public void Refuses_a_capability_wider_than_the_root()
    {
        var e = Assert.Throws<OptionsValidationException>(() => Build(o =>
        {
            o.RootRights = CapabilityRights.LedgerRead;
            o.Capabilities["signer"] = CapabilityRights.CryptoSign;
        }));
        Assert.Contains("signer asks for CryptoSign", e.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Refuses_a_capability_with_no_rights_and_bad_currencies()
    {
        var e = Assert.Throws<OptionsValidationException>(() => Build(o =>
        {
            o.Capabilities["empty"] = CapabilityRights.None;
            o.Platform.Currencies.Add("ngn");
        }));
        Assert.Contains("empty holds no rights", e.Message, StringComparison.Ordinal);
        Assert.Contains("ngn", e.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Refuses_a_second_registration()
    {
        var services = new ServiceCollection();
        services.AddZxv(_ => { });
        Assert.Throws<InvalidOperationException>(() => services.AddZxv(_ => { }));
    }

    [Fact]
    public async Task Unknown_currency_fails_start_up()
    {
        using IHost host = Build(o => o.Platform.Currencies.Add("XYZ"));
        await Assert.ThrowsAnyAsync<ZxvException>(() => host.StartAsync());
    }

    [Fact]
    public async Task Disposes_named_capabilities_then_root_then_platform()
    {
        IHost host = Build(TellerAuditor);
        await host.StartAsync();
        ZxvPlatform zxv = host.Services.GetRequiredService<ZxvPlatform>();
        Capability teller = host.Services.GetRequiredKeyedService<Capability>("teller");
        await host.StopAsync();
        host.Dispose();

        Assert.Throws<ObjectDisposedException>(() => teller.GetInfo());
        Assert.Throws<ObjectDisposedException>(() => zxv.IsHalted);
    }

    [Fact]
    public async Task Disposal_order_follows_abi_r6()
    {
        // Each ZXV singleton's factory resolves a transient sentinel, which the
        // container captures just before the singleton itself. Disposal runs
        // in reverse, so each sentinel is disposed right after its object and
        // records that it is gone: the log is the objects' disposal order.
        var disposed = new List<string>();
        IHost host = Build(TellerAuditor, s =>
        {
            Decorate<ZxvPlatform>(s, null, disposed, "platform");
            Decorate<Capability>(s, ZxvServiceKeys.Root, disposed, "root");
            Decorate<Capability>(s, "teller", disposed, "teller");
            Decorate<Capability>(s, "auditor", disposed, "auditor");
        });
        await host.StartAsync();
        await host.StopAsync();
        host.Dispose();

        // ABI.md R6: attenuated caps, then their parent (the root), then the context.
        Assert.Equal(4, disposed.Count);
        Assert.Equal(new[] { "auditor gone", "teller gone" }, disposed.Take(2).Order(StringComparer.Ordinal));
        Assert.Equal(new[] { "root gone", "platform gone" }, disposed.Skip(2));
    }

    private sealed class Sentinel : IDisposable
    {
        private readonly object _target;
        private readonly string _name;
        private readonly List<string> _log;

        public Sentinel(object target, string name, List<string> log)
        {
            _target = target;
            _name = name;
            _log = log;
        }

        public void Dispose()
        {
            bool alive;
            try
            {
                alive = _target switch
                {
                    Capability c => c.GetInfo().Rights != CapabilityRights.None,
                    ZxvPlatform p => !p.IsHalted,
                    _ => false,
                };
            }
            catch (ObjectDisposedException)
            {
                alive = false;
            }

            _log.Add(_name + (alive ? " alive" : " gone"));
        }
    }

    private static void Decorate<T>(IServiceCollection services, object? key, List<string> log, string name)
        where T : class
    {
        ServiceDescriptor d = services.Single(x => x.ServiceType == typeof(T) && Equals(x.ServiceKey, key));
        services.Remove(d);
        object? pending = null;
        string sentinelKey = "sentinel:" + name;
        services.AddKeyedTransient<Sentinel>(sentinelKey, (_, _) => new Sentinel(pending!, name, log));
        object Track(IServiceProvider sp, object o)
        {
            pending = o;
            _ = sp.GetRequiredKeyedService<Sentinel>(sentinelKey); // captured just before o
            return o;
        }

        if (key is null)
        {
            Func<IServiceProvider, object> f = d.ImplementationFactory!;
            services.AddSingleton(typeof(T), sp => Track(sp, f(sp)));
        }
        else
        {
            Func<IServiceProvider, object?, object> f = d.KeyedImplementationFactory!;
            services.AddKeyedSingleton(typeof(T), key, (sp, k) => Track(sp, f(sp, k)));
        }
    }

    [Fact]
    public async Task Health_is_healthy_then_unhealthy_once_halted()
    {
        using IHost host = Build(TellerAuditor);
        await host.StartAsync();
        HealthCheckService health = host.Services.GetRequiredService<HealthCheckService>();

        HealthReport ok = await health.CheckHealthAsync();
        Assert.Equal(HealthStatus.Healthy, ok.Status);
        Assert.Equal("live", ok.Entries["zxv"].Data["state"]);
        Assert.Contains("attestation=True", (string)ok.Entries["zxv"].Data["capability:teller"], StringComparison.Ordinal);
        Assert.False(host.Services.GetRequiredService<ZxvFatalState>().IsTripped);

        host.Services.GetRequiredService<ZxvPlatform>().Halt("secret-ish operator reason");
        HealthReport down = await health.CheckHealthAsync();
        Assert.Equal(HealthStatus.Unhealthy, down.Status);
        Assert.Equal("halted", down.Entries["zxv"].Data["state"]);
        Assert.DoesNotContain("operator reason", down.Entries["zxv"].Description, StringComparison.Ordinal);
        Assert.True(host.Services.GetRequiredService<ZxvFatalState>().IsTripped);
        await host.StopAsync();
    }

    [Fact]
    public async Task Health_is_unhealthy_once_disposed()
    {
        using IHost host = Build(TellerAuditor);
        await host.StartAsync();
        // The named capabilities and the root are still alive and still hold
        // the native context (ABI.md R6), but the platform itself reports
        // disposed at once, so the check fails closed.
        Capability teller = host.Services.GetRequiredKeyedService<Capability>("teller");
        host.Services.GetRequiredService<ZxvPlatform>().Dispose();
        Assert.False(teller.GetInfo().IsRevoked);
        HealthReport down = await host.Services.GetRequiredService<HealthCheckService>().CheckHealthAsync();
        Assert.Equal(HealthStatus.Unhealthy, down.Status);
        Assert.Equal("disposed", down.Entries["zxv"].Data["state"]);
        await host.StopAsync();
    }

    [Fact]
    public async Task Stops_the_application_on_fatal_when_asked()
    {
        using IHost host = Build(o =>
        {
            TellerAuditor(o);
            o.StopApplicationOnFatal = true;
        });
        await host.StartAsync();
        IHostApplicationLifetime life = host.Services.GetRequiredService<IHostApplicationLifetime>();
        host.Services.GetRequiredService<ZxvPlatform>().Halt("test");
        _ = await host.Services.GetRequiredService<HealthCheckService>().CheckHealthAsync();
        Assert.True(life.ApplicationStopping.IsCancellationRequested);
        await host.StopAsync();
    }
}
