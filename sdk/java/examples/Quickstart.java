// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A runnable tour of the ZXV SDK for the JVM. Single-file source program:
//
//   java --enable-native-access=org.zedec.zxv -p target/zxv-1.6.0.jar --add-modules org.zedec.zxv \
//        examples/Quickstart.java [fixtures-dir]
//
// With a fixtures directory (sdk/conformance/fixtures) it also runs the engine.

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Permission;
import org.zedec.zxv.Rational;
import org.zedec.zxv.Truth;
import org.zedec.zxv.Zxv;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.budget.Budget;
import org.zedec.zxv.budget.Budgets;
import org.zedec.zxv.budget.Resource;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.engine.GenerateOptions;
import org.zedec.zxv.engine.Model;
import org.zedec.zxv.engine.Session;
import org.zedec.zxv.ledger.Account;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Ledger;
import org.zedec.zxv.phase.DeferredEvent;
import org.zedec.zxv.phase.PhaseClock;
import org.zedec.zxv.wire.Ubh168Frame;

public class Quickstart {
    public static void main(String[] args) throws Exception {
        System.out.println("libzxv " + Zxv.version() + " from " + Zxv.libraryLocation());

        // Exact numbers: 0.1 + 0.2 is 3/10, and a double shows its true value.
        System.out.println("0.1 + 0.2 = " + Rational.parse("0.1").add(Rational.parse("0.2")));
        System.out.println("double 0.1 = " + Rational.fromDoubleExact(0.1).toFractionString());

        // Four-valued logic: a contradiction is held, not collapsed.
        Truth t = Truth.TRUE.and(Truth.BOTH);
        System.out.println("TRUE and BOTH = " + t + ", acted on? " + t.toBoolean(false, false));

        try (ZxvContext ctx = ZxvContext.create();
             Capability root = ctx.rootCapability(Permission.all());
             Capability teller = root.attenuate(Permission.LEDGER_READ, Permission.LEDGER_POST)) {

            // Ledger: exact minor units, through a narrowed capability.
            ctx.enableCurrency(root, "NGN");
            Ledger ledger = Ledger.of(teller);
            Account bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER);
            Account ada = ledger.openAccount("ada", "NGN", AccountKind.HOLDER);
            ledger.post(bank, ada, 150_00, "NGN", "E2E-0001");
            System.out.println("ada: " + ledger.balance(ada));

            // Budgets: consumeAsync waits for a release instead of failing.
            try (Budgets budgets = Budgets.create(root)) {
                Budget energy = budgets.budget("energy", Resource.ENERGY, Rational.of(10), true);
                energy.consume(Rational.parse("9.5"));
                CompletableFuture<Void> later = energy.consumeAsync(Rational.of(2), Duration.ofSeconds(5));
                energy.release(Rational.of(3, 2));
                later.join();
                System.out.println("energy used: " + energy.used());
            }

            // Phase ticks: a deferred S0 event clears when the clock passes its tick.
            try (PhaseClock clock = PhaseClock.create(root)) {
                DeferredEvent ev = clock.defer(PhaseClock.CYCLE);
                CompletableFuture<DeferredEvent> cleared = ev.awaitCleared();
                clock.advance(PhaseClock.CYCLE);
                System.out.println("cleared at tick " + clock.now() + ": " + cleared.isDone());
            }

            // Post-quantum signature on an agent message; the secret key is off-heap and wiped on close.
            MlDsa87 dsa = MlDsa87.with(root);
            byte[] msg = "pay ada 150.00 NGN".getBytes(StandardCharsets.UTF_8);
            try (MlDsa87.KeyPair kp = dsa.generateKeyPair()) {
                byte[] sig = dsa.sign(kp.secretKey(), msg, "zxv".getBytes(StandardCharsets.UTF_8));
                System.out.println("ML-DSA-87 signature " + sig.length + " bytes, valid: "
                        + dsa.verify(kp.publicKey(), msg, "zxv".getBytes(StandardCharsets.UTF_8), sig));
            }

            // UBH-168 wire frame, packed in pure Java.
            System.out.println("idle pilot trunk 0: " + Ubh168Frame.idle(0).toHex());

            // The integer engine (bit-identical on every machine).
            if (args.length > 0) {
                Path dir = Path.of(args[0]);
                String prompt = Files.readString(dir.resolve("prompt.txt"));
                try (Model model = Model.open(root, dir.resolve("tiny-qwen2.gguf"), dir.resolve("tiny-tokenizer.gguf"));
                     Session chat = model.newSession()) {
                    System.out.print("tokens:");
                    var result = chat.generate(prompt, GenerateOptions.greedy(16), tok -> {
                        System.out.print(" " + tok.id());
                        return true;
                    });
                    System.out.println("\nstop: " + result.stopReason());
                }
            }
        }
    }
}
