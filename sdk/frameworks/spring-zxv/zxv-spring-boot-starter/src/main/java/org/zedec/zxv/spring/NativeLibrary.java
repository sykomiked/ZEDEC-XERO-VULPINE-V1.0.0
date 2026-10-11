// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.zedec.zxv.Zxv;

/** Applies {@code zxv.library}. */
final class NativeLibrary {
    private NativeLibrary() {}

    /**
     * Loads libzxv from {@code library} when it is set. Several application
     * contexts in one JVM (tests) share the one loaded library, so asking
     * for the same file again is fine; asking for a different one fails.
     */
    static void ensureLoaded(String library) {
        if (library == null || library.isBlank()) return;
        Path path = Path.of(library).toAbsolutePath().normalize();
        if (!Files.isRegularFile(path)) {
            throw new IllegalStateException("zxv.library: no file at " + path);
        }
        try {
            Zxv.load(path);
        } catch (IllegalStateException alreadyLoaded) {
            String from = Zxv.libraryLocation();
            if (!sameFile(from, path)) {
                throw new IllegalStateException("zxv.library is " + path + " but libzxv is already loaded from " + from,
                        alreadyLoaded);
            }
        }
    }

    private static boolean sameFile(String loadedFrom, Path wanted) {
        // "bundled:..." and "system:..." are not the file the property names.
        if (loadedFrom == null || loadedFrom.startsWith("bundled:") || loadedFrom.startsWith("system:")) return false;
        try {
            return Files.isSameFile(Path.of(loadedFrom), wanted);
        } catch (IOException e) {
            return false;
        }
    }
}
