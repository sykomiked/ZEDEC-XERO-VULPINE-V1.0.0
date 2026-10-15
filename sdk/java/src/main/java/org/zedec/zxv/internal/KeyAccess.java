// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.lang.foreign.MemorySegment;
import org.zedec.zxv.crypto.SecretKey;

/**
 * Lets other SDK packages pass a {@link SecretKey}'s off-heap bytes straight
 * to a native call, under the key's lock, without copying them to the heap
 * or making the accessors public API. {@link SecretKey} installs the bridge
 * when its class is initialized (which has happened by the time anyone holds
 * a key). Internal.
 */
public final class KeyAccess {
    private KeyAccess() {}

    /** What the crypto package exposes to the rest of the SDK. */
    public interface Keys {
        /**
         * Takes the key's read lock and returns its off-heap bytes; pair with {@link #release}.
         *
         * @param key      the key
         * @param expected the kind the call needs (a different kind is ZXV_E_ARG)
         * @return the key's native segment, valid until {@link #release}
         */
        MemorySegment acquire(SecretKey key, SecretKey.Kind expected);

        /**
         * Releases the lock taken by {@link #acquire}.
         *
         * @param key the key
         */
        void release(SecretKey key);
    }

    private static volatile Keys keys;

    /**
     * Installs the bridge (called once by {@link SecretKey}).
     *
     * @param k the bridge
     */
    public static void install(Keys k) {
        keys = k;
    }

    /**
     * The bridge.
     *
     * @return the installed bridge
     */
    public static Keys keys() {
        Keys k = keys;
        if (k == null) {
            try {
                Class.forName(SecretKey.class.getName(), true, SecretKey.class.getClassLoader());
            } catch (ClassNotFoundException e) {
                throw new IllegalStateException(e);
            }
            k = keys;
        }
        return k;
    }
}
