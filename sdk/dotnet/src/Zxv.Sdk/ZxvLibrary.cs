// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// The loaded native library, libzxv: where it is found, its ABI version and
/// features, status names and classes, and the process-wide
/// <see cref="FatalPolicy"/>. This is the .NET form of the cross-SDK static
/// <c>Zxv</c> entry point (a class named <c>Zxv</c> would collide with the
/// <c>Zxv</c> namespace).
/// </summary>
/// <remarks>
/// The library is found in this order: <see cref="UseLibraryPath(string)"/>,
/// the <c>ZXV_LIBRARY_PATH</c> environment variable (or <c>ZXV_NATIVE_LIBRARY</c>),
/// the copy bundled with the package, then the system loader path. The first
/// SDK call that needs it checks <c>zxv_version</c> and refuses a library whose
/// major version is not 1 or whose minor version is below 4.
/// </remarks>
public static class ZxvLibrary
{
    /// <summary>The major ABI version this SDK binds.</summary>
    public const int SupportedAbiMajor = 1;

    /// <summary>The lowest minor ABI version this SDK accepts (1.1 adds capabilities, rationals, crypto and the engine; 1.2 the <c>_cap</c> forms this SDK calls; 1.3 the hexagram tag; 1.4 hardware emulation).</summary>
    public const int MinimumAbiMinor = 4;

    private static readonly Lazy<(Version Version, ZxvFeatures Features)> Loaded = new(Load, LazyThreadSafetyMode.ExecutionAndPublication);
    private static int _fatalPolicy;

    /// <summary>
    /// Gets or sets what happens when a context halts. The default,
    /// <see cref="FatalPolicy.Throw"/>, raises <see cref="ZxvFatalException"/>;
    /// <see cref="FatalPolicy.FailFast"/> ends the process at once.
    /// </summary>
    public static FatalPolicy FatalPolicy
    {
        get => (FatalPolicy)Volatile.Read(ref _fatalPolicy);
        set => Volatile.Write(ref _fatalPolicy, (int)value);
    }

    /// <summary>Gets the ABI version of the loaded library (checked on first use).</summary>
    /// <exception cref="ZxvSystemException">The library's ABI is not 1.1 or a later 1.x.</exception>
    public static Version AbiVersion => Loaded.Value.Version;

    /// <summary>Gets the feature groups live in the loaded library.</summary>
    public static ZxvFeatures Features => Loaded.Value.Features;

    /// <summary>
    /// Loads libzxv from an explicit path instead of the environment or the
    /// bundled copy. Call it before any other SDK call.
    /// </summary>
    /// <param name="path">Full path to <c>libzxv.so</c>, <c>libzxv.dylib</c> or <c>zxv.dll</c>.</param>
    public static void UseLibraryPath(string path)
    {
        Guard.Text(path, nameof(path));
        NativeLibraryResolver.ExplicitPath = path;
    }

    /// <summary>Returns whether every flag in <paramref name="features"/> is live.</summary>
    /// <param name="features">The features to test.</param>
    /// <returns><see langword="true"/> when all are available.</returns>
    public static bool Supports(ZxvFeatures features) => (Features & features) == features;

    /// <summary>Gets the stable symbolic name of a status, e.g. <c>ZXV_E_BUDGET</c>.</summary>
    /// <param name="status">The status.</param>
    /// <returns>The name; <c>ZXV_E_UNKNOWN</c> for an unknown code.</returns>
    public static string GetStatusName(ZxvStatus status) => NativeText.StatusName((int)status);

    /// <summary>Gets the class of a status (<c>zxv_status_class</c>). Unknown negative codes are <see cref="ZxvStatusClass.System"/>.</summary>
    /// <param name="status">The status.</param>
    /// <returns>The class.</returns>
    public static ZxvStatusClass ClassOf(ZxvStatus status) => (ZxvStatusClass)NativeMethods.StatusClass((int)status);

    /// <summary>Checks the loaded library once; cheap after the first call.</summary>
    internal static void EnsureCompatible() => _ = Loaded.Value;

    /// <summary>Applies <see cref="FatalPolicy"/> to a fatal error about to be thrown.</summary>
    internal static ZxvFatalException Fatal(ZxvFatalException error)
    {
        if (FatalPolicy == FatalPolicy.FailFast)
        {
            Environment.FailFast("ZXV context halted (FatalPolicy.FailFast): " + error.Message, error);
        }

        return error;
    }

    private static (Version, ZxvFeatures) Load()
    {
        int rc = NativeMethods.Version(out uint major, out uint minor, out uint patch);
        if (rc != (int)ZxvStatus.Ok)
        {
            throw new ZxvSystemException((ZxvStatus)rc, "ZxvLibrary.Version", NativeText.LastError());
        }

        if (major != SupportedAbiMajor || minor < MinimumAbiMinor)
        {
            throw new ZxvSystemException($"libzxv ABI {major}.{minor}.{patch} is not supported; this SDK needs {SupportedAbiMajor}.{MinimumAbiMinor} or a later {SupportedAbiMajor}.x.");
        }

        rc = NativeMethods.Features(out uint mask);
        if (rc != (int)ZxvStatus.Ok)
        {
            throw new ZxvSystemException((ZxvStatus)rc, "ZxvLibrary.Features", NativeText.LastError());
        }

        return (new Version((int)major, (int)minor, (int)patch), (ZxvFeatures)mask);
    }
}
