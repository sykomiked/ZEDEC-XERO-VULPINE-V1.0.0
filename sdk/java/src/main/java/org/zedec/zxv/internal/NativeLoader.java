// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.io.IOException;
import java.io.InputStream;
import java.lang.foreign.Arena;
import java.lang.foreign.SymbolLookup;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Finds and opens libzxv, in the order fixed by the SDK design: an explicit
 * path, the {@code zxv.library} system property, the {@code ZXV_LIBRARY_PATH}
 * environment variable, a copy bundled in the jar under
 * {@code /native/<os>-<arch>/}, then the system loader path.
 */
public final class NativeLoader {
    private static volatile Path explicitPath;
    private static volatile String loadedFrom;
    private static volatile boolean loaded;

    private NativeLoader() {}

    /** Records an explicit path; must happen before the first native call. */
    public static synchronized void setExplicitPath(Path path) {
        if (loaded) {
            throw new IllegalStateException("libzxv is already loaded from " + loadedFrom);
        }
        explicitPath = path;
    }

    /** Where the library was loaded from, or null before loading. */
    public static String loadedFrom() {
        return loadedFrom;
    }

    /** The platform folder name used for the bundled copy, e.g. {@code linux-x86_64}. */
    public static String platform() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
        String o = os.contains("win") ? "windows" : (os.contains("mac") || os.contains("darwin")) ? "macos" : "linux";
        String a = switch (arch) {
            case "amd64", "x86_64", "x64" -> "x86_64";
            case "aarch64", "arm64" -> "aarch64";
            default -> arch;
        };
        return o + "-" + a;
    }

    static synchronized SymbolLookup open() {
        List<String> tried = new ArrayList<>();
        Arena global = Arena.global();
        List<Path> candidates = new ArrayList<>();
        if (explicitPath != null) candidates.add(explicitPath);
        String prop = System.getProperty("zxv.library");
        if (prop != null && !prop.isBlank()) candidates.add(Path.of(prop));
        String env = System.getenv("ZXV_LIBRARY_PATH");
        if (env != null && !env.isBlank()) {
            Path p = Path.of(env);
            candidates.add(Files.isDirectory(p) ? p.resolve(System.mapLibraryName("zxv")) : p);
        }
        for (Path p : candidates) {
            if (Files.isRegularFile(p)) {
                SymbolLookup l = SymbolLookup.libraryLookup(p, global);
                loadedFrom = p.toAbsolutePath().toString();
                loaded = true;
                return l;
            }
            tried.add(p + " (not found)");
        }
        Path bundled = extractBundled(tried);
        if (bundled != null) {
            SymbolLookup l = SymbolLookup.libraryLookup(bundled, global);
            loadedFrom = "bundled:" + bundled;
            loaded = true;
            return l;
        }
        String name = System.mapLibraryName("zxv");
        try {
            SymbolLookup l = SymbolLookup.libraryLookup(name, global);
            loadedFrom = "system:" + name;
            loaded = true;
            return l;
        } catch (IllegalArgumentException e) {
            tried.add(name + " on the system loader path (" + e.getMessage() + ")");
        }
        throw new UnsatisfiedLinkError("libzxv could not be loaded. Tried: " + String.join("; ", tried)
                + ". Pass a path to Zxv.load(path), set -Dzxv.library or ZXV_LIBRARY_PATH.");
    }

    private static Path extractBundled(List<String> tried) {
        String resource = "/native/" + platform() + "/" + System.mapLibraryName("zxv");
        try (InputStream in = NativeLoader.class.getResourceAsStream(resource)) {
            if (in == null) {
                tried.add("jar resource " + resource + " (absent)");
                return null;
            }
            Path dir = Files.createTempDirectory("zxv-native");
            Path out = dir.resolve(System.mapLibraryName("zxv"));
            Files.copy(in, out, StandardCopyOption.REPLACE_EXISTING);
            out.toFile().deleteOnExit();
            dir.toFile().deleteOnExit();
            return out;
        } catch (IOException e) {
            tried.add("jar resource " + resource + " (" + e.getMessage() + ")");
            return null;
        }
    }
}
