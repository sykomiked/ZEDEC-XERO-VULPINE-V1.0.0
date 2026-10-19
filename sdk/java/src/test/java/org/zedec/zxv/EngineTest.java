// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.zedec.zxv.Vectors.map;
import static org.zedec.zxv.Vectors.num;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Flow;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.budget.Budget;
import org.zedec.zxv.budget.Budgets;
import org.zedec.zxv.budget.Resource;
import org.zedec.zxv.engine.GenerateOptions;
import org.zedec.zxv.engine.GenerateResult;
import org.zedec.zxv.engine.Model;
import org.zedec.zxv.engine.Session;
import org.zedec.zxv.engine.StopReason;
import org.zedec.zxv.engine.Token;

/** The integer engine against the generation vectors, every streaming form, budgets and LoRA. */
class EngineTest {
    private static ZxvContext ctx;
    private static Capability cap;
    private static Model model;
    private static String prompt;
    private static Map<String, Object> gen;
    private Session session;

    @BeforeAll
    static void load() throws Exception {
        gen = map(Vectors.root().get("generate"));
        Path dir = Vectors.fixtures();
        prompt = Files.readString(dir.resolve((String) gen.get("prompt")));
        ctx = ZxvContext.create();
        cap = ctx.rootCapability(Permission.all());
        model = Model.open(cap, dir.resolve((String) gen.get("model")), dir.resolve((String) gen.get("tokenizer")));
    }

    @AfterAll
    static void unload() {
        ctx.close();
    }

    @BeforeEach
    void newSession() {
        session = model.newSession((int) num(gen.get("n_ctx")), org.zedec.zxv.engine.KvType.Q16);
    }

    private static int[] greedyTokens() {
        return Vectors.ints(map(gen.get("greedy")).get("tokens"));
    }

    private static GenerateOptions greedy() {
        return GenerateOptions.greedy((int) num(map(gen.get("greedy")).get("max_tokens")));
    }

    @Test
    void tokenizer() {
        assertEquals(1200, model.vocabSize());
        int[] ids = model.tokenize(prompt, true);
        assertArrayEquals(Vectors.ints(gen.get("prompt_tokens")), ids);
        assertEquals(prompt, model.detokenize(ids), "detokenize(tokenize(x)) == x");
    }

    @Test
    void greedyGeneration() {
        GenerateResult r = session.generate(prompt, greedy());
        assertArrayEquals(greedyTokens(), r.tokens());
        assertEquals(16, r.generatedTokens());
        assertEquals(15, r.promptTokens());
        assertEquals(StopReason.MAX_TOKENS, r.stopReason());
        assertEquals(model.detokenize(r.tokens()), r.text());
        assertEquals(15 + 16 - 1, session.position(), "the last sampled token is fed on the next turn");
    }

    @Test
    void seededSampledGeneration() {
        Map<String, Object> s = map(gen.get("sampled"));
        GenerateOptions o = GenerateOptions.builder()
                .maxTokens((int) num(s.get("max_tokens")))
                .temperature(Rational.parse("0.7"))
                .topK((int) num(s.get("top_k")))
                .topP(Rational.parse("0.95"))
                .repeatPenalty(Rational.parse("1.1"))
                .repeatLastN((int) num(s.get("repeat_last_n")))
                .seed(num(s.get("seed")))
                .build();
        assertEquals(num(s.get("temperature_q16")), o.temperature().toQ16());
        assertEquals(num(s.get("top_p_q16")), o.topP().toQ16());
        assertEquals(num(s.get("repeat_penalty_q16")), o.repeatPenalty().toQ16());
        assertArrayEquals(Vectors.ints(s.get("tokens")), session.generate(prompt, o).tokens());
        session.reset();
        assertArrayEquals(Vectors.ints(s.get("tokens")), session.generate(prompt, o).tokens(), "same seed, same tokens");
    }

    @Test
    void callbackStreamingAndStop() {
        List<Integer> seen = new ArrayList<>();
        GenerateResult r = session.generate(prompt, greedy(), t -> {
            seen.add(t.id());
            return seen.size() < 3;
        });
        assertEquals(StopReason.CALLBACK, r.stopReason());
        assertEquals(3, seen.size());
        assertEquals(List.of(670, 414, 246), seen);

        session.reset();
        IllegalStateException boom = assertThrows(IllegalStateException.class,
                () -> session.generate(prompt, greedy(), t -> {
                    throw new IllegalStateException("listener failed");
                }));
        assertEquals("listener failed", boom.getMessage(), "a listener's exception is rethrown, not lost in C");
    }

    @Test
    void javaStream() {
        try (Stream<Token> tokens = session.stream(prompt, greedy())) {
            assertArrayEquals(greedyTokens(), tokens.mapToInt(Token::id).toArray());
        }
        session.reset();
        try (Stream<Token> tokens = session.stream(prompt, greedy())) {
            assertEquals(List.of(670, 414), tokens.limit(2).map(Token::id).toList());
        }
        // Closing the stream stopped the producer; the session is usable again.
        session.reset();
        assertArrayEquals(greedyTokens(), session.generate(prompt, greedy()).tokens());
    }

    @Test
    void flowPublisherWithBackPressure() throws Exception {
        List<Integer> ids = new ArrayList<>();
        CountDownLatch done = new CountDownLatch(1);
        session.publisher(prompt, greedy()).subscribe(new Flow.Subscriber<>() {
            private Flow.Subscription sub;

            @Override
            public void onSubscribe(Flow.Subscription s) {
                sub = s;
                s.request(1);
            }

            @Override
            public void onNext(Token t) {
                ids.add(t.id());
                sub.request(1);
            }

            @Override
            public void onError(Throwable t) {
                done.countDown();
            }

            @Override
            public void onComplete() {
                done.countDown();
            }
        });
        assertTrue(done.await(60, TimeUnit.SECONDS));
        assertArrayEquals(greedyTokens(), ids.stream().mapToInt(Integer::intValue).toArray());
    }

    @Test
    void generateAsyncOnVirtualThread() throws Exception {
        CompletableFuture<GenerateResult> f = session.generateAsync(prompt, greedy(),
                t -> {
                    assertTrue(Thread.currentThread().isVirtual());
                    return true;
                });
        assertArrayEquals(greedyTokens(), f.get(60, TimeUnit.SECONDS).tokens());
    }

    @Test
    void budgetBoundGeneration() {
        try (Budgets budgets = Budgets.create(cap)) {
            Budget b = budgets.budget("gen", Resource.TOKENS, Rational.of(15 + 8), true);
            session.bindBudget(b);
            GenerateResult r = session.generate(prompt, greedy());
            assertEquals(StopReason.BUDGET, r.stopReason());
            assertEquals(8, r.tokens().length, "the budget stops generation after 8 tokens");
            session.reset();
            assertThrows(BudgetExhaustedException.class, () -> session.generate(prompt, greedy()),
                    "an exhausted budget refuses the prompt up front");
            session.unbindBudget();
            session.reset();
            assertArrayEquals(greedyTokens(), session.generate(prompt, greedy()).tokens());
        }
    }

    @Test
    void capabilityAndLora() {
        try (Capability noAi = cap.attenuate(Permission.LEDGER_READ)) {
            Path dir = Vectors.fixtures();
            assertThrows(CapabilityDeniedException.class,
                    () -> Model.open(noAi, dir.resolve((String) gen.get("model")), null));
        }
        ZxvSystemException io = assertThrows(ZxvSystemException.class,
                () -> Model.open(cap, Path.of("/nonexistent.gguf"), null));
        assertEquals("ZXV_E_IO", io.statusName());
        assertThrows(NotImplementedException.class, () -> model.loadLora(Path.of("x"), Rational.ONE));
    }
}
