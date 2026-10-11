// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A small, honest "agent turn" on the ZXV platform core:
//   1. one root capability, attenuated per component (least privilege);
//   2. the model file is attested before the actuator gate opens;
//   3. a token budget meters generation, and an async consumer waits for it;
//   4. the phase clock defers the reply to the next harmonic fundamental;
//   5. the reply is generated deterministically and signed with ML-DSA-87;
//   6. the swarm economy shares a cycle's tokens across models.
//
// Usage: dotnet run --project samples/Zxv.Samples.Agent [fixtures-dir]
//        dotnet run --project samples/Zxv.Samples.Agent -- --fatal-failfast
//   (the second form halts a context under FatalPolicy.FailFast: the process
//    ends at once with a non-zero exit code; the SDK tests run it.)

using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Zxv;
using Zxv.Engine;
using Zxv.Ledger;

if (args.Length > 0 && args[0] == "--fatal-failfast")
{
    ZxvLibrary.FatalPolicy = FatalPolicy.FailFast;
    using ZxvPlatform doomed = ZxvPlatform.Create(o => o.Currencies.Add("NGN"));
    using Capability teller = doomed.CreateRootCapability(CapabilityRights.LedgerPost | CapabilityRights.LedgerRead);
    Account a = doomed.Ledger.WithCapability(teller).OpenAccount("a", "NGN", AccountKind.Issuer);
    doomed.Halt("demonstrating FatalPolicy.FailFast");
    Console.WriteLine("halted; the next call ends the process");
    doomed.Ledger.GetBalances(a.Id); // Environment.FailFast: never returns
    Console.WriteLine("unreachable");
    return 0;
}

string fixtures = args.Length > 0 ? args[0] : FindFixtures();
Console.WriteLine($"libzxv ABI {ZxvLibrary.AbiVersion}; features: {ZxvLibrary.Features}");

using ZxvPlatform zxv = ZxvPlatform.Create();
using Capability root = zxv.CreateRootCapability();

// 1. Least privilege: each component gets only what it needs.
using Capability inference = root.Attenuate(CapabilityRights.AiInference | CapabilityRights.Budget);
using Capability signer = root.Attenuate(CapabilityRights.CryptoSign);
using Capability scheduler = root.Attenuate(CapabilityRights.Phase | CapabilityRights.Swarm | CapabilityRights.Attest);
Console.WriteLine($"inference cap: {inference.Rights}; signer cap: {signer.Rights}");

// 2. Exact numbers: no floats cross the boundary.
Rational temperature = Rational.Parse("0.7");
Console.WriteLine($"0.1 + 0.2 = {Rational.Parse("0.1") + Rational.Parse("0.2")} exactly; 0.1 as a double is {Rational.FromDoubleExact(0.1)}");

// 3. Attest the model file; the actuator gate opens only when every claim is certain.
string modelPath = Path.Combine(fixtures, "tiny-qwen2.gguf");
using Attestations attestations = zxv.CreateAttestations(scheduler);
int model = attestations.Attest("tiny-qwen2.gguf", AttestationSubject.ModelPack, Truth.Neither, "loader", Hashes.Sha3256(File.ReadAllBytes(modelPath)));
Console.WriteLine($"gate before evidence: {(attestations.IsGateClear ? "clear" : "closed")} ({attestations.GetState(model).Truth.Name()})");
attestations.AddEvidence(model, positive: true);
Console.WriteLine($"gate after evidence:  {(attestations.IsGateClear ? "clear" : "closed")} ({attestations.GetState(model).Truth.Name()})");

// 4. A token budget, and an async consumer that waits for replenishment.
using Budgets budgets = zxv.CreateBudgets(inference);
budgets.Exhausted += (_, e) => Console.WriteLine($"  budget '{e.Budget.Name}' exhausted");
Budget tokens = budgets.Create("tokens", BudgetResource.Tokens, 64);
Budget reserve = budgets.Create("reserve", BudgetResource.Tokens, 4);
reserve.Consume(4);
Task waiting = reserve.ConsumeAsync(2);
reserve.Release(2);
await waiting;
Console.WriteLine($"reserve consumed after release; remaining {reserve.Remaining}");

// 5. Defer the reply to the next fundamental and let the real-time driver clear it.
using PhaseClock clock = zxv.CreatePhaseClock(scheduler);
DeferredEvent reply = clock.Defer(clock.Now + (PhaseClock.TicksPerCycle / 10));
await using (clock.StartRealtime(PhaseClock.TicksPerCycle))
{
    await reply.WaitClearedAsync();
}

Console.WriteLine($"phase event cleared at tick {clock.Now}");

// 6. Deterministic generation, streamed, metered by the budget.
using Model qwen = Model.Open(inference, modelPath, Path.Combine(fixtures, "tiny-tokenizer.gguf"));
using Session session = qwen.NewSession(128);
session.BindBudget(tokens);
string prompt = File.ReadAllText(Path.Combine(fixtures, "prompt.txt"), Encoding.UTF8);
var text = new StringBuilder();
await foreach (GeneratedToken t in session.StreamAsync(prompt, new GenerateOptions { MaxTokens = 16, Temperature = temperature, TopK = 40, Seed = 42 }))
{
    text.Append(t.Text);
}

Console.WriteLine($"generated {text.Length} chars; budget left {tokens.Remaining} tokens");

// 7. Sign the reply with ML-DSA-87; the secret key never touches the GC heap.
var dsa = new MlDsa87(signer);
using KeyPair keys = dsa.GenerateKey();
byte[] reply8 = Encoding.UTF8.GetBytes(text.ToString());
byte[] signature = dsa.Sign(keys.SecretKey, reply8, "zxv-agent"u8);
Console.WriteLine($"ML-DSA-87 signature {signature.Length} bytes, verifies: {dsa.Verify(keys.PublicKey, reply8, signature, "zxv-agent"u8)}");

// 8. The swarm economy: a cycle of tokens shared across Fibonacci levels and feelings.
using Swarm swarm = zxv.CreateSwarm(scheduler, levels: 3, tokensPerCycle: 1000);
swarm.Register(1, 0);
swarm.Register(2, 1);
swarm.Register(3, 1);
swarm.SetMood(new Rune(0x1F914), 5);
swarm.SetFeeling(2, new Rune(0x1F604), 3);
swarm.BeginCycle();
foreach (uint id in new uint[] { 1, 2, 3 })
{
    SwarmAllotment a = swarm.GetAllotment(id);
    Console.WriteLine($"  model {id}: {a.Allotted} tokens ({a.Imaginary} emotional)");
}

Console.WriteLine($"cycle closed, {swarm.EndCycle()} unused tokens expired");
return 0;

static string FindFixtures()
{
    for (DirectoryInfo? d = new(AppContext.BaseDirectory); d is not null; d = d.Parent)
    {
        string candidate = Path.Combine(d.FullName, "sdk", "conformance", "fixtures");
        if (Directory.Exists(candidate))
        {
            return candidate;
        }

        candidate = Path.Combine(d.FullName, "conformance", "fixtures");
        if (Directory.Exists(candidate))
        {
            return candidate;
        }
    }

    throw new DirectoryNotFoundException("Pass the fixtures directory (sdk/conformance/fixtures) as the first argument.");
}
