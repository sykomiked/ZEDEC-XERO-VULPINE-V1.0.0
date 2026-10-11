// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv-quickstart: the README quickstart, runnable. Exact money, a narrowed
// capability, four-valued logic, an awaited budget and a phase deferral,
// streamed tokens (if fixtures are given), and the fail-closed path.
//
// Usage: swift run zxv-quickstart [fixtures dir]
import Foundation
import Zxv

@main
struct Quickstart {
    static func main() async throws {
        let ctx = try ZxvContext()
        let root = try ctx.rootCapability()
        try ctx.enableCurrency("NGN", capability: root) // needs .ledgerPost
        let ledger = Ledger(root)
        let bank = try ledger.openAccount("bank", currency: "NGN", kind: .issuer)
        let ada = try ledger.openAccount("ada", currency: "NGN")
        try ledger.post(from: bank, to: ada, amount: 125_075, currency: "NGN", reference: "salary-001")
        print("ada equity:", try ledger.balance(ada).equity, "kobo")

        // Exact rationals; a Double enters only through fromDoubleExact.
        print("0.1 + 0.2 =", try Rational("0.1") + Rational("0.2"))
        print("the double 0.1 is", try Rational.fromDoubleExact(0.1))

        // Least privilege.
        do {
            try Ledger(root.attenuate(.ledgerRead)).post(from: bank, to: ada, amount: 1, currency: "NGN", reference: "x")
        } catch .refused(let e) where e.reason == .capabilityDenied {
            print("refused as expected:", e.statusName)
        }

        // Four-valued logic: a contradiction is held.
        let t = Truth(positive: 3, negative: 1)
        print("evidence 3 for, 1 against:", t, "certainly true?", t.isCertainlyTrue)

        // Await a budget replenishment.
        let budgets = try Budgets(root)
        let tokens = try budgets.create("tokens", resource: .tokens, limit: 10)
        try tokens.consume(10)
        async let waited: Void = tokens.consumeAsync(2, timeout: .seconds(5))
        try await Task.sleep(for: .milliseconds(20))
        try tokens.release(3)
        try await waited
        print("budget used after replenish:", try tokens.status().used)

        // Phase deferral with the manual driver.
        let clock = try PhaseClock(root)
        let ev = try clock.defer(until: phaseCycle)
        try clock.advance(phaseCycle)
        try await ev.awaitCleared()
        print("event cleared at tick", try clock.now())

        // Stream tokens if fixtures were given.
        if CommandLine.arguments.count > 1 {
            let dir = CommandLine.arguments[1]
            let model = try Model(root, path: "\(dir)/tiny-qwen2.gguf", tokenizer: "\(dir)/tiny-tokenizer.gguf")
            let session = try model.newSession(contextLength: 128)
            var ids: [Int32] = []
            for try await token in session.stream("Hi there", options: GenerateOptions(maxTokens: 8)) { ids.append(token.id) }
            print("streamed token ids:", ids)
        }

        // Fail closed.
        try ctx.halt("operator kill switch")
        do { try ledger.check() } catch .fatal(let e) { print("fatal (by design):", e) }
    }
}
