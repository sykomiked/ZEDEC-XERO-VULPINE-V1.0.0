// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
/**
 * The ZXV SDK for the JVM: Java bindings for libzxv over the Foreign
 * Function and Memory API. Run with {@code --enable-native-access=org.zedec.zxv}.
 */
module org.zedec.zxv {
    exports org.zedec.zxv;
    exports org.zedec.zxv.ledger;
    exports org.zedec.zxv.trust;
    exports org.zedec.zxv.budget;
    exports org.zedec.zxv.phase;
    exports org.zedec.zxv.wire;
    exports org.zedec.zxv.swarm;
    exports org.zedec.zxv.crypto;
    exports org.zedec.zxv.engine;
    exports org.zedec.zxv.evm;
}
