// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>
/// <see cref="FatalPolicy.FailFast"/> ends the process, so it is tested in a
/// child process: the agent sample's <c>--fatal-failfast</c> mode.
/// </summary>
public sealed class FatalPolicyTests
{
    [Fact]
    public void FailFast_terminates_the_process()
    {
        string configuration = typeof(FatalPolicyTests).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>()
            .Single(a => a.Key == "ZxvConfiguration").Value!;
        string dotnetRoot = new DirectoryInfo(AppContext.BaseDirectory).Parent!.Parent!.Parent!.Parent!.Parent!.FullName; // sdk/dotnet
        string sample = Path.Combine(dotnetRoot, "samples", "Zxv.Samples.Agent", "bin", configuration, "net8.0", "Zxv.Samples.Agent.dll");
        Assert.True(File.Exists(sample), "sample not built: " + sample);

        var psi = new ProcessStartInfo(Environment.GetEnvironmentVariable("DOTNET_HOST_PATH") ?? "dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        psi.ArgumentList.Add(sample);
        psi.ArgumentList.Add("--fatal-failfast");
        using Process child = Process.Start(psi)!;
        string stdout = child.StandardOutput.ReadToEnd();
        string stderr = child.StandardError.ReadToEnd();
        Assert.True(child.WaitForExit(60_000));

        Assert.NotEqual(0, child.ExitCode);
        Assert.Contains("halted; the next call ends the process", stdout, StringComparison.Ordinal);
        Assert.DoesNotContain("unreachable", stdout, StringComparison.Ordinal);
        Assert.Contains("FatalPolicy.FailFast", stdout + stderr, StringComparison.Ordinal);
    }
}
