// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Path;
import java.util.Collections;
import java.util.EnumSet;
import java.util.Set;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeLoader;

/**
 * Library entry point: loading, version, features, status names.
 *
 * <p>The native library is found, on first use, in this order: the path given
 * to {@link #load(Path)}; the {@code zxv.library} system property; the
 * {@code ZXV_LIBRARY_PATH} environment variable (a file or a directory); a
 * copy bundled in the jar under {@code /native/<os>-<arch>/}; then
 * {@link System#mapLibraryName(String) libzxv.so / libzxv.dylib / zxv.dll} on
 * the system loader path. The library's ABI must be 1.x with x at least 1.
 *
 * <p>Run with {@code --enable-native-access=org.zedec.zxv} (module path) or
 * {@code --enable-native-access=ALL-UNNAMED} (class path).
 */
public final class Zxv {
    private Zxv() {}

    /** The ABI version of a library. */
    public record Version(int major, int minor, int patch) {
        @Override
        public String toString() {
            return major + "." + minor + "." + patch;
        }
    }

    /**
     * Uses the library at {@code path}. Call before anything else touches ZXV.
     *
     * @throws IllegalStateException if the library is already loaded
     */
    public static void load(Path path) {
        NativeLoader.setExplicitPath(path);
        version();
    }

    /** Where the library was loaded from (loads it if needed). */
    public static String libraryLocation() {
        version();
        return NativeLoader.loadedFrom();
    }

    /** The loaded library's ABI version. */
    public static Version version() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ma = a.allocate(JAVA_INT), mi = a.allocate(JAVA_INT), pa = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.version, ma, mi, pa), null);
            return new Version(ma.get(JAVA_INT, 0), mi.get(JAVA_INT, 0), pa.get(JAVA_INT, 0));
        }
    }

    /** The feature groups live in this build. */
    public static Set<Feature> features() {
        int mask = featureMask();
        EnumSet<Feature> s = EnumSet.noneOf(Feature.class);
        for (Feature f : Feature.values()) if ((mask & f.bit()) != 0) s.add(f);
        return Collections.unmodifiableSet(s);
    }

    /** True if {@code feature} is live in this build. */
    public static boolean hasFeature(Feature feature) {
        return (featureMask() & feature.bit()) != 0;
    }

    private static int featureMask() {
        int[] out = new int[1];
        try (Arena a = Arena.ofConfined()) {
            MemorySegment m = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.features, m), null);
            out[0] = m.get(JAVA_INT, 0);
        }
        return out[0];
    }

    /** The symbolic name of a status ({@code "ZXV_E_FUNDS"}); {@code "ZXV_E_UNKNOWN"} for unknown codes. */
    public static String statusName(int status) {
        return Errors.name(status);
    }

    /** The class of a status; unknown negative codes are SYSTEM. */
    public static StatusClass statusClass(int status) {
        return Errors.statusClass(status);
    }
}
