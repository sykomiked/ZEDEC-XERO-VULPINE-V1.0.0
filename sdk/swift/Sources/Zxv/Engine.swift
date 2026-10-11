// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv
import Foundation

/// Model metadata keys.
public enum ModelInfo: Int32, Sendable {
    /// 1 qwen2, 2 qwen3, 3 llama.
    case arch = 1
    case vocabularySize = 2, trainingContext = 3, layers = 4, embedding = 5, heads = 6, kvHeads = 7
    /// End-of-sequence token id.
    case eos = 8
}

/// KV cache representation.
public enum KvType: Int32, Sendable {
    /// Exact, 4 bytes per value.
    case q16 = 0
    /// 8-bit, about 1.16 bytes per value.
    case q8 = 1
}

/// Why generation stopped.
public enum StopReason: Int32, Sendable {
    case none = 0
    /// End-of-sequence token.
    case eos = 1
    /// `maxTokens` reached.
    case maxTokens = 2
    /// The callback (or the stream's consumer) stopped it.
    case callback = 4
    /// The context window is full.
    case contextFull = 5
    /// The bound budget ran out.
    case budget = 6
}

/// Sampling parameters, all exact (converted to Q16.16 with
/// `Rational.toQ16`). Temperature 0 is greedy. The same seed and inputs give
/// the same tokens on every machine.
public struct GenerateOptions: Sendable {
    /// Upper bound on generated tokens.
    public var maxTokens: UInt32 = 64
    /// 0 = greedy.
    public var temperature: Rational = .zero
    /// 0 = off.
    public var topK: UInt32 = 0
    /// 0 = off.
    public var topP: Rational = .zero
    /// 0 = off.
    public var repeatPenalty: Rational = .zero
    /// Window for the penalty.
    public var repeatLastN: UInt32 = 0
    /// Sampling seed.
    public var seed: UInt64 = 0
    /// Defaults: 64 tokens, greedy.
    public init(maxTokens: UInt32 = 64, temperature: Rational = .zero, topK: UInt32 = 0, topP: Rational = .zero,
                repeatPenalty: Rational = .zero, repeatLastN: UInt32 = 0, seed: UInt64 = 0) {
        self.maxTokens = maxTokens
        self.temperature = temperature
        self.topK = topK
        self.topP = topP
        self.repeatPenalty = repeatPenalty
        self.repeatLastN = repeatLastN
        self.seed = seed
    }
}

/// One generated token.
public struct Token: Sendable, Equatable {
    /// Token id.
    public let id: Int32
    /// Its UTF-8 bytes (possibly a partial character).
    public let piece: [UInt8]
}

/// The outcome of a generation.
public struct GenerateResult: Sendable, Equatable {
    /// Concatenated pieces, decoded as UTF-8.
    public var text: String { String(decoding: bytes, as: UTF8.self) }
    /// Raw concatenated bytes.
    public let bytes: [UInt8]
    /// Generated token ids.
    public let tokens: [Int32]
    /// Prompt length in tokens.
    public let promptTokens: UInt32
    /// Why it stopped.
    public let stop: StopReason
}

/// A GGUF model in the integer inference engine (qwen2, qwen3, llama),
/// bit-identical on every machine. Read-only after loading: share it across
/// tasks and threads. Needs `.aiInference`.
public final class Model: @unchecked Sendable {
    let cap: Capability
    let handle: OpaquePointer

    /// Open a GGUF file; `tokenizer` nil = the model's own tokenizer.
    public init(_ cap: Capability, path: String, tokenizer: String? = nil) throws(ZxvError) {
        self.cap = cap
        var m: OpaquePointer?
        try cap.call { zxv_model_open($0, path, tokenizer, &m) }
        handle = m!
    }

    /// Open from memory (the bytes are copied).
    public init(_ cap: Capability, modelBytes: [UInt8], tokenizerBytes: [UInt8] = []) throws(ZxvError) {
        self.cap = cap
        var m: OpaquePointer?
        try cap.call {
            zxv_model_open_memory($0, modelBytes, modelBytes.count, tokenizerBytes.isEmpty ? nil : tokenizerBytes,
                                  tokenizerBytes.count, &m)
        }
        handle = m!
    }

    deinit { zxv_model_destroy(handle) }

    /// A metadata value.
    public func info(_ key: ModelInfo) throws(ZxvError) -> Int64 {
        var v: Int64 = 0
        try check(zxv_model_info(handle, key.rawValue, &v))
        return v
    }

    /// Token ids of UTF-8 text.
    public func tokenize(_ text: String, parseSpecial: Bool = true) throws(ZxvError) -> [Int32] {
        let b = Array(text.utf8)
        var n = 0
        let st = zxv_tokenize(handle, b, b.count, parseSpecial ? 1 : 0, nil, 0, &n)
        if st != ZXV_E_BUFFER { try check(st) }
        var ids = [Int32](repeating: 0, count: n)
        if st == ZXV_E_BUFFER { try check(zxv_tokenize(handle, b, b.count, parseSpecial ? 1 : 0, &ids, n, &n)) }
        return Array(ids.prefix(n))
    }

    /// UTF-8 text of token ids.
    public func detokenize(_ ids: [Int32]) throws(ZxvError) -> String {
        try readText { zxv_detokenize(handle, ids, ids.count, $0, $1, $2) }
    }

    /// A new session. `contextLength` 0 = min(trainingContext, 4096).
    public func newSession(contextLength: UInt32 = 0, kv: KvType = .q16) throws(ZxvError) -> Session {
        var s: OpaquePointer?
        try check(zxv_session_create(handle, contextLength, kv.rawValue, &s))
        return Session(model: self, handle: s!)
    }

    /// LoRA adapters: declared in the ABI, not built yet. Throws
    /// `.refused` (notImplemented) while `Features.lora` is off.
    public func loadLora(path: String, scale: Rational) throws(ZxvError) {
        let q: Int32
        do throws(ZxvUsageError) { q = try scale.toQ16() } catch { throw .usage(error) }
        var l: OpaquePointer?
        try cap.call { zxv_lora_load($0, handle, path, q, &l) }
        zxv_lora_destroy(l)
        throw .refused(ZxvRefusedError(status: ZXV_E_NOTIMPL, detail: "LoRA handles are not exposed by this SDK version"))
    }
}

/// A conversation: a KV cache. Generation appends, so a chat continues
/// turn after turn; `reset()` forgets. The C session is single-threaded:
/// the SDK refuses overlapping calls (`.usage`, `ZXV_E_STATE`), which is
/// what makes it safe to mark Sendable and hand to a background task.
public final class Session: @unchecked Sendable {
    /// The model this session runs.
    public let model: Model
    private let handle: OpaquePointer
    private let busy = Locked(false)
    private let bound = Locked<Budgets?>(nil) // keeps a bound registry alive

    init(model: Model, handle: OpaquePointer) {
        self.model = model
        self.handle = handle
    }
    deinit { zxv_session_destroy(handle) }

    private func exclusive<R>(_ body: () throws(ZxvError) -> R) throws(ZxvError) -> R {
        let was = busy.withLock { b -> Bool in
            defer { b = true }
            return b
        }
        if was { throw .usage(ZxvUsageError(status: ZXV_E_STATE, detail: "the session is in use by another call or stream")) }
        defer { busy.withLock { $0 = false } }
        return try body()
    }

    private final class CallbackBox {
        var ids: [Int32] = []
        var bytes: [UInt8] = []
        let onToken: ((Token) -> Bool)?
        init(_ f: ((Token) -> Bool)?) { onToken = f }
    }

    /// Generate from `prompt`, blocking the calling thread. `onToken`
    /// (optional) sees each token and returns false to stop. Needs `.aiInference`.
    public func generate(_ prompt: String, options: GenerateOptions = GenerateOptions(),
                         onToken: ((Token) -> Bool)? = nil) throws(ZxvError) -> GenerateResult {
        try exclusive { () throws(ZxvError) -> GenerateResult in
            let temp: Int32, topP: Int32, rep: Int32
            do throws(ZxvUsageError) {
                temp = try options.temperature.toQ16()
                topP = try options.topP.toQ16()
                rep = try options.repeatPenalty.toQ16()
            } catch { throw .usage(error) }
            let box = CallbackBox(onToken)
            let user = Unmanaged.passUnretained(box).toOpaque()
            let p = Array(prompt.utf8)
            var np: UInt32 = 0, ng: UInt32 = 0, stop: Int32 = 0
            try model.cap.call {
                zxv_generate($0, handle, p, p.count, options.maxTokens, temp, options.topK, topP, rep,
                             options.repeatLastN, options.seed, { user, id, piece, len in
                                 let box = Unmanaged<CallbackBox>.fromOpaque(user!).takeUnretainedValue()
                                 let bytes = len > 0 ? Array(UnsafeBufferPointer(start: piece, count: len)) : []
                                 box.ids.append(id)
                                 box.bytes += bytes
                                 return box.onToken.map { $0(Token(id: id, piece: bytes)) ? 1 : 0 } ?? 1
                             }, user, &np, &ng, &stop)
            }
            withExtendedLifetime(box) {}
            return GenerateResult(bytes: box.bytes, tokens: box.ids, promptTokens: np,
                                  stop: StopReason(rawValue: stop) ?? .none)
        }
    }

    /// Generate on a dedicated thread (off the cooperative pool) and await
    /// the result. Task cancellation stops generation at the next token.
    public func generateAsync(_ prompt: String, options: GenerateOptions = GenerateOptions()) async throws -> GenerateResult {
        let cancelled = Locked(false)
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (c: CheckedContinuation<GenerateResult, Error>) in
                Thread.detachNewThread {
                    do {
                        c.resume(returning: try self.generate(prompt, options: options) { _ in !cancelled.withLock { $0 } })
                    } catch {
                        c.resume(throwing: error)
                    }
                }
            }
        } onCancel: {
            cancelled.withLock { $0 = true }
        }
    }

    /// Stream tokens as they are generated (on a dedicated thread).
    /// Terminating the stream (breaking out of the loop, or task
    /// cancellation) stops generation at the next token.
    ///
    ///     for try await token in session.stream("Hello", options: opts) { ... }
    public func stream(_ prompt: String, options: GenerateOptions = GenerateOptions()) -> AsyncThrowingStream<Token, Error> {
        AsyncThrowingStream { continuation in
            let stopped = Locked(false)
            continuation.onTermination = { _ in stopped.withLock { $0 = true } }
            Thread.detachNewThread {
                do {
                    _ = try self.generate(prompt, options: options) { token in
                        if stopped.withLock({ $0 }) { return false }
                        continuation.yield(token)
                        return true
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
        }
    }

    /// Forget the conversation (empty KV cache).
    public func reset() throws(ZxvError) {
        try exclusive { () throws(ZxvError) in try check(zxv_session_reset(handle), model.cap.context) }
    }

    /// Tokens currently in the KV cache.
    public func position() throws(ZxvError) -> UInt32 {
        var p: UInt32 = 0
        try check(zxv_session_position(handle, &p), model.cap.context)
        return p
    }

    /// Charge `budget` one unit per prompt and generated token. Prompt
    /// tokens are charged up front: if they do not fit, `generate` throws
    /// `.refused` (budgetExhausted) and runs nothing; running out mid-way
    /// stops with `.budget`.
    public func bindBudget(_ budget: Budget?) throws(ZxvError) {
        try exclusive { () throws(ZxvError) in
            try check(zxv_session_bind_budget(handle, budget?.registry.handle, budget?.id ?? 0), model.cap.context)
            bound.withLock { $0 = budget?.registry }
        }
    }
}

// MARK: - EVM state channels

/// `abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
/// int256[] balances)` and its keccak256 digest.
public struct EvmState: Sendable, Equatable {
    /// The ABI encoding.
    public let encoding: [UInt8]
    /// keccak256(encoding): what parties sign (EIP-191) in their own wallet.
    public let digest: [UInt8]
}

/// Encodes ledger balances for the Solidity state-channel contract. Needs
/// `.evm` (and `.ledgerRead` for `ledgerState`). ZXV has no secp256k1.
public struct StateChannel: Sendable {
    private let cap: Capability
    /// Acting with `cap`.
    public init(_ cap: Capability) { self.cap = cap }

    private func run(_ f: (UnsafeMutablePointer<UInt8>?, Int, UnsafeMutablePointer<Int>, UnsafeMutablePointer<UInt8>) -> Int32) throws(ZxvError) -> EvmState {
        var need = 0
        var digest = [UInt8](repeating: 0, count: 32)
        try cap.context.ensureAlive()
        let st = f(nil, 0, &need, &digest)
        if st != ZXV_E_BUFFER { try check(st, cap.context) }
        var buf = [UInt8](repeating: 0, count: need)
        let rc = buf.withUnsafeMutableBufferPointer { b in f(b.baseAddress, b.count, &need, &digest) }
        try check(rc, cap.context)
        return EvmState(encoding: Array(buf.prefix(need)), digest: digest)
    }

    private static func flat(_ parties: [[UInt8]]) throws(ZxvError) -> [UInt8] {
        for p in parties where p.count != 20 {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "addresses are 20 bytes"))
        }
        return parties.flatMap { $0 }
    }

    /// Encode explicit balances, one per 20-byte address.
    public func encodeState(channelId: [UInt8], nonce: UInt64, parties: [[UInt8]], balances: [Int64]) throws(ZxvError) -> EvmState {
        guard channelId.count == 32, parties.count == balances.count else {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "32-byte channel id and one balance per party"))
        }
        let addrs = try Self.flat(parties)
        return try run { buf, cap, len, dg in
            zxv_evm_encode_state(self.cap.handle, channelId, nonce, addrs, balances, UInt32(parties.count), buf, cap, len, dg)
        }
    }

    /// Encode each account's EQUITY (888) balance, all in `currency`.
    public func ledgerState(channelId: [UInt8], nonce: UInt64, currency: String, accounts: [UInt32],
                            parties: [[UInt8]]) throws(ZxvError) -> EvmState {
        guard channelId.count == 32, parties.count == accounts.count else {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "32-byte channel id and one account per party"))
        }
        let addrs = try Self.flat(parties)
        return try run { buf, cap, len, dg in
            zxv_evm_ledger_state(self.cap.handle, channelId, nonce, currency, accounts, addrs, UInt32(parties.count),
                                 buf, cap, len, dg)
        }
    }
}
