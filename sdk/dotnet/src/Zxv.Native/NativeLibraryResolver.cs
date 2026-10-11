// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// Resolves the native library, in the order every ZXV SDK uses: an explicit
/// path set in code (<see cref="ExplicitPath"/>, via <c>ZxvLibrary.UseLibraryPath</c>),
/// the <c>ZXV_LIBRARY_PATH</c> environment variable (or the older
/// <c>ZXV_NATIVE_LIBRARY</c>), the copy bundled with the package
/// (application directory and <c>runtimes/&lt;rid&gt;/native</c>), then the
/// system loader path (<c>LD_LIBRARY_PATH</c>, <c>DYLD_LIBRARY_PATH</c>, <c>PATH</c>).
/// </summary>
internal static class NativeLibraryResolver
{
    /// <summary>Environment variable holding an explicit path to libzxv.</summary>
    internal const string PathVariable = "ZXV_NATIVE_LIBRARY";

    /// <summary>The cross-SDK environment variable holding an explicit path to libzxv.</summary>
    internal const string LibraryPathVariable = "ZXV_LIBRARY_PATH";

    /// <summary>Gets or sets a path set in code; it wins over the environment. Must be set before the first native call.</summary>
    internal static string? ExplicitPath { get; set; }

#pragma warning disable CA2255 // ModuleInitializer is the intended mechanism for a library-wide resolver.
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void Register()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeLibraryResolver).Assembly, Resolve);
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, NativeMethods.Library, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        if (!string.IsNullOrWhiteSpace(ExplicitPath))
        {
            return LoadExplicit(ExplicitPath, "ZxvLibrary.UseLibraryPath");
        }

        foreach (string variable in new[] { LibraryPathVariable, PathVariable })
        {
            string? explicitPath = Environment.GetEnvironmentVariable(variable);
            if (!string.IsNullOrWhiteSpace(explicitPath))
            {
                return LoadExplicit(explicitPath, variable);
            }
        }

        return NativeLibrary.TryLoad(libraryName, assembly, searchPath, out IntPtr handle) ? handle : IntPtr.Zero;
    }

    private static IntPtr LoadExplicit(string path, string source)
    {
        if (!File.Exists(path))
        {
            throw new DllNotFoundException($"{source} points to '{path}', which does not exist.");
        }

        return NativeLibrary.Load(path);
    }
}
