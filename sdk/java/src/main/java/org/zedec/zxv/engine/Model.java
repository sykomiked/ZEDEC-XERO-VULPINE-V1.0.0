// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import org.zedec.zxv.Capability;
import org.zedec.zxv.NotImplementedException;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * A GGUF model loaded into libzxv's integer inference engine (qwen2, qwen3,
 * llama; F32/F16/BF16/Q8_0/Q4_0/Q5_x/Q4_K/Q6_K). Inference is integer-only
 * and bit-identical on every machine. A model is read-only after loading and
 * may be shared by sessions on any threads. Opening and generating need
 * {@code AI_INFERENCE}; the model keeps the capability it was opened with and
 * its sessions generate through it.
 *
 * <pre>{@code
 * try (Model model = Model.open(cap, Path.of("model.gguf"), null);
 *      Session chat = model.newSession()) {
 *     System.out.println(chat.generate("Hello", GenerateOptions.greedy(32)).text());
 * }
 * }</pre>
 */
public final class Model implements AutoCloseable {
    /** Keys of {@link #info(Info)}. */
    public enum Info {
        /** 1 qwen2, 2 qwen3, 3 llama. */
        ARCH(1),
        /** Vocabulary size. */
        N_VOCAB(2),
        /** Training context length. */
        N_CTX_TRAIN(3),
        /** Layer count. */
        N_LAYER(4),
        /** Embedding width. */
        N_EMBD(5),
        /** Attention heads. */
        N_HEAD(6),
        /** Key/value heads. */
        N_HEAD_KV(7),
        /** End-of-sequence token id. */
        EOS(8);

        private final int code;

        Info(int code) {
            this.code = code;
        }
    }

    final Capability cap;
    final ZxvContext ctx;
    final NativeResource res;

    private Model(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("model", h, x -> Ffi.callVoid(Native.model_destroy, x), ctx, Caps.res(ctx));
    }

    /**
     * Opens a GGUF file.
     *
     * @param tokenizer a separate tokenizer GGUF, or null for the model's own
     * @throws org.zedec.zxv.ZxvSystemException {@code ZXV_E_IO} if a file cannot be read
     * @throws org.zedec.zxv.ZxvUsageException {@code ZXV_E_FORMAT} for a malformed file
     */
    public static Model open(Capability cap, Path model, Path tokenizer) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.model_open, l.h(0), Ffi.cstr(a, model.toString()),
                    tokenizer == null ? MemorySegment.NULL : Ffi.cstr(a, tokenizer.toString()), out), ctx);
            return new Model(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /** Opens a model from memory; the bytes are copied, so the arrays may be reused. */
    public static Model openMemory(Capability cap, byte[] model, byte[] tokenizer) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.model_open_memory, l.h(0), Ffi.bytes(a, model), (long) model.length,
                    Ffi.bytes(a, tokenizer), (long) (tokenizer == null ? 0 : tokenizer.length), out), ctx);
            return new Model(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /** One metadata value. */
    public long info(Info key) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.model_info, l.h(0), key.code, v), ctx);
            return v.get(JAVA_LONG, 0);
        }
    }

    /** The vocabulary size. */
    public int vocabSize() {
        return (int) info(Info.N_VOCAB);
    }

    /** The end-of-sequence token id. */
    public int eosToken() {
        return (int) info(Info.EOS);
    }

    /** Token ids of UTF-8 text. */
    public int[] tokenize(String text, boolean parseSpecial) {
        return tokenize(text.getBytes(StandardCharsets.UTF_8), parseSpecial);
    }

    /** Token ids of UTF-8 bytes. */
    public int[] tokenize(byte[] utf8, boolean parseSpecial) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment t = Ffi.bytes(a, utf8), n = a.allocate(JAVA_LONG);
            int sp = parseSpecial ? 1 : 0;
            int st = Ffi.call(Native.tokenize, l.h(0), t, (long) utf8.length, sp, MemorySegment.NULL, 0L, n);
            if (st == 0) return new int[0];
            if (st != Ffi.E_BUFFER) Ffi.check(st, ctx);
            long count = n.get(JAVA_LONG, 0);
            MemorySegment ids = a.allocate(JAVA_INT, Math.max(1, count));
            Ffi.check(Ffi.call(Native.tokenize, l.h(0), t, (long) utf8.length, sp, ids, count, n), ctx);
            return ids.asSlice(0, n.get(JAVA_LONG, 0) * 4).toArray(JAVA_INT);
        }
    }

    /** The text of token ids. */
    public String detokenize(int[] ids) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment s = a.allocate(JAVA_INT, Math.max(1, ids.length));
            MemorySegment.copy(ids, 0, s, JAVA_INT, 0, ids.length);
            return Ffi.text(ctx, (b, c, n) -> Ffi.call(Native.detokenize, l.h(0), s, (long) ids.length, b, c, n));
        }
    }

    /** A session with the default context ({@code min(n_ctx_train, 4096)}) and an exact Q16 KV cache. */
    public Session newSession() {
        return newSession(0, KvType.Q16);
    }

    /**
     * A new session (conversation state).
     *
     * @param nCtx context window in tokens; 0 for {@code min(n_ctx_train, 4096)}
     * @param kv   KV cache representation
     */
    public Session newSession(int nCtx, KvType kv) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.session_create, l.h(0), nCtx, kv.ordinal(), out), ctx);
            return new Session(this, out.get(ADDRESS, 0));
        }
    }

    /**
     * Loads a LoRA adapter. Declared by the ABI but not built into the
     * engine yet: this raises {@link NotImplementedException} while
     * {@code Feature.LORA} is off.
     */
    public void loadLora(Path adapter, Rational scale) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.lora_load, l.h(0), l.h(1), Ffi.cstr(a, adapter.toString()), scale.toQ16(), out), ctx);
            // Unreachable while LoRA is not built; free the handle if a future library returns one.
            Ffi.callVoid(Native.lora_destroy, out.get(ADDRESS, 0));
            throw Errors.usage(-3, "LoRA adapters are not supported by this SDK version");
        }
    }

    /** The capability sessions generate through. */
    public Capability capability() {
        return cap;
    }

    /** Frees the model and closes its sessions. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
