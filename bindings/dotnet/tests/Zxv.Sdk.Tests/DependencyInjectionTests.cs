// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Zxv.Payments;

namespace Zxv.Sdk.Tests;

public class DependencyInjectionTests
{
    [Fact]
    public void Binds_from_configuration()
    {
        IConfiguration config = new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string, string?>
        {
            ["Zxv:Currencies:0"] = "NGN",
            ["Zxv:Currencies:1"] = "XOF",
            ["Zxv:PrivateUnits:0:Code"] = "VFV",
            ["Zxv:PrivateUnits:0:MinorUnits"] = "2",
            ["Zxv:IdentifierPrefix"] = "BANK1",
        }).Build();

        var services = new ServiceCollection().AddLogging(b => b.SetMinimumLevel(LogLevel.Debug));
        services.AddZxv(config.GetSection(ZxvOptions.SectionName));
        using ServiceProvider sp = services.BuildServiceProvider(validateScopes: true);

        ZxvPlatform p = sp.GetRequiredService<ZxvPlatform>();
        Assert.Same(p, sp.GetRequiredService<ZxvPlatform>());
        Assert.True(p.Currencies.IsEnabled("XOF"));
        Assert.True(p.Currencies.IsEnabled("VFV"));
        Assert.Equal("BANK1-000007", p.Payments.ToIdentifier(new Ledger.AccountId(7)));
        Assert.Same(NullMxTransport.Instance, sp.GetRequiredService<IMxTransport>());
    }

    [Fact]
    public void Invalid_options_fail_validation()
    {
        var services = new ServiceCollection();
        services.AddZxv(o => o.Currencies.Add("naira"));
        using ServiceProvider sp = services.BuildServiceProvider();
        var ex = Assert.Throws<OptionsValidationException>(() => sp.GetRequiredService<IOptions<ZxvOptions>>().Value);
        Assert.Contains("naira", ex.Message, System.StringComparison.Ordinal);
    }

    [Fact]
    public void Unknown_currency_fails_at_start()
    {
        var services = new ServiceCollection();
        services.AddZxv(o => o.Currencies.Add("QQQ"));
        using ServiceProvider sp = services.BuildServiceProvider();
        Assert.Equal(Interop.ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => sp.GetRequiredService<ZxvPlatform>()).Status);
    }
}
