// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// Resolves the native library. Order: the <c>ZXV_NATIVE_LIBRARY</c>
/// environment variable (a full path), then the default probing of the
/// runtime (application directory and <c>runtimes/&lt;rid&gt;/native</c>).
/// </summary>
internal static class NativeLibraryResolver
{
    /// <summary>Environment variable holding an explicit path to libzxv.</summary>
    internal const string PathVariable = "ZXV_NATIVE_LIBRARY";

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

        string? explicitPath = Environment.GetEnvironmentVariable(PathVariable);
        if (!string.IsNullOrWhiteSpace(explicitPath))
        {
            if (!File.Exists(explicitPath))
            {
                throw new DllNotFoundException($"{PathVariable} points to '{explicitPath}', which does not exist.");
            }

            return NativeLibrary.Load(explicitPath);
        }

        return NativeLibrary.TryLoad(libraryName, assembly, searchPath, out IntPtr handle) ? handle : IntPtr.Zero;
    }
}
