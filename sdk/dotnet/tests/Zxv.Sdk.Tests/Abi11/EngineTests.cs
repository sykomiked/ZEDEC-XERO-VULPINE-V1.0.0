// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Engine;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>The integer inference engine against the tiny-qwen2 fixture and its reference tokens.</summary>
public sealed class EngineTests : IDisposable
{
    private readonly ZxvPlatform _p = ZxvPlatform.Create();
    private readonly Capability _cap;
    private readonly Model _model;
    private readonly string _prompt;
    private readonly JsonElement _v = Conformance.Vectors.GetProperty("generate");

    public EngineTests()
    {
        _cap = _p.CreateRootCapability();
        _model = Model.Open(_cap, Conformance.Fixture(_v.GetProperty("model").GetString()!), Conformance.Fixture(_v.GetProperty("tokenizer").GetString()!));
        _prompt = File.ReadAllText(Conformance.Fixture(_v.GetProperty("prompt").GetString()!), Encoding.UTF8);
    }

    public void Dispose()
    {
        _model.Dispose();
        _cap.Dispose();
        _p.Dispose();
    }

    private int[] Greedy => Conformance.Ints(_v.GetProperty("greedy").GetProperty("tokens"));

    [Fact]
    public void Model_info_and_tokenizer_round_trip()
    {
        Assert.Equal(1200, _model.VocabularySize);
        Assert.Equal(ModelArchitecture.Qwen2, _model.Architecture);
        int[] ids = _model.Tokenize(_prompt);
        Assert.Equal(Conformance.Ints(_v.GetProperty("prompt_tokens")), ids);
        Assert.Equal(_prompt, _model.Detokenize(ids));
        Assert.Empty(_model.Tokenize(string.Empty));
    }

    [Fact]
    public void Greedy_generation_matches_reference_tokens()
    {
        using Session s = _model.NewSession(_v.GetProperty("n_ctx").GetInt32());
        GenerationResult r = s.Generate(_prompt, new GenerateOptions { MaxTokens = _v.GetProperty("greedy").GetProperty("max_tokens").GetInt32() });
        Assert.Equal(Greedy, r.Tokens);
        Assert.Equal(StopReason.MaxTokens, r.StopReason);
        Assert.Equal(15, r.PromptTokenCount);
        Assert.Equal(_model.Detokenize(Greedy), r.Text);
        Assert.Equal(15 + 16 - 1, s.Position); // the last sampled token is not evaluated into the KV cache yet
    }

    [Fact]
    public void Seeded_sampling_matches_reference_tokens()
    {
        JsonElement v = _v.GetProperty("sampled");
        var options = new GenerateOptions
        {
            MaxTokens = v.GetProperty("max_tokens").GetInt32(),
            Temperature = Rational.Parse("0.7"),
            TopK = v.GetProperty("top_k").GetInt32(),
            TopP = Rational.Parse("0.95"),
            RepeatPenalty = Rational.Parse("1.1"),
            RepeatLastN = v.GetProperty("repeat_last_n").GetInt32(),
            Seed = v.GetProperty("seed").GetUInt64(),
        };
        Assert.Equal(v.GetProperty("temperature_q16").GetInt32(), options.Temperature.ToQ16());
        Assert.Equal(v.GetProperty("top_p_q16").GetInt32(), options.TopP.ToQ16());
        Assert.Equal(v.GetProperty("repeat_penalty_q16").GetInt32(), options.RepeatPenalty.ToQ16());
        using Session s = _model.NewSession(128);
        Assert.Equal(Conformance.Ints(v.GetProperty("tokens")), s.Generate(_prompt, options).Tokens);
        s.Reset();
        Assert.Equal(Conformance.Ints(v.GetProperty("tokens")), s.Generate(_prompt, options).Tokens);
    }

    [Fact]
    public async Task Async_and_streaming_give_the_same_tokens()
    {
        using Session s = _model.NewSession(128);
        GenerationResult r = await s.GenerateAsync(_prompt, new GenerateOptions { MaxTokens = 16 });
        Assert.Equal(Greedy, r.Tokens);

        s.Reset();
        var streamed = new List<int>();
        var text = new StringBuilder();
        await foreach (GeneratedToken t in s.StreamAsync(_prompt, new GenerateOptions { MaxTokens = 16 }))
        {
            streamed.Add(t.Id);
            text.Append(t.Text);
        }

        Assert.Equal(Greedy, streamed);
        Assert.Equal(r.Text, text.ToString());

        // Leaving the loop early stops generation.
        s.Reset();
        int n = 0;
        await foreach (GeneratedToken t in s.StreamAsync(_prompt, new GenerateOptions { MaxTokens = 16 }))
        {
            if (++n == 3)
            {
                break;
            }
        }

        Assert.True(s.Position < 15 + 16);
    }

    [Fact]
    public async Task Callback_stop_and_cancellation()
    {
        using Session s = _model.NewSession(128);
        int seen = 0;
        GenerationResult r = s.Generate(_prompt, new GenerateOptions { MaxTokens = 16 }, _ => ++seen < 3);
        Assert.Equal(StopReason.Callback, r.StopReason);
        Assert.Equal(3, r.Tokens.Count);

        s.Reset();
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => s.GenerateAsync(_prompt, null, cts.Token));

        s.Reset();
        Assert.Throws<InvalidOperationException>(() => s.Generate(_prompt, GenerateOptions.Greedy, _ => throw new InvalidOperationException("from callback")));
    }

    [Fact]
    public void Disposed_model_and_budgets_throw_while_a_session_still_holds_them()
    {
        Model model = Model.Open(_cap, Conformance.Fixture(_v.GetProperty("model").GetString()!), Conformance.Fixture(_v.GetProperty("tokenizer").GetString()!));
        Budgets budgets = _p.CreateBudgets(_cap);
        Budget b = budgets.Create("gen", BudgetResource.Tokens, 1000);
        using Session s = model.NewSession(128);
        s.BindBudget(b);

        model.Dispose();
        model.Dispose();
        budgets.Dispose();
        budgets.Dispose();

        Assert.Throws<ObjectDisposedException>(() => model.Tokenize("hi"));
        Assert.Throws<ObjectDisposedException>(() => model.NewSession(16));
        Assert.Throws<ObjectDisposedException>(() => budgets.Create("late", BudgetResource.Tokens, 1));
        s.BindBudget(null);
        Assert.Equal(4, s.Generate(_prompt, new GenerateOptions { MaxTokens = 4 }).Tokens.Count);
    }

    [Fact]
    public void Bound_budget_stops_generation_and_refuses_an_unaffordable_prompt()
    {
        using Budgets budgets = _p.CreateBudgets(_cap);
        Budget b = budgets.Create("gen", BudgetResource.Tokens, 15 + 8);
        using Session s = _model.NewSession(128);
        s.BindBudget(b);
        GenerationResult r = s.Generate(_prompt, new GenerateOptions { MaxTokens = 16 });
        Assert.Equal(StopReason.Budget, r.StopReason);
        Assert.Equal(8, r.Tokens.Count);
        Assert.Equal(Greedy.Take(8), r.Tokens);

        s.Reset();
        Assert.Throws<BudgetExhaustedException>(() => s.Generate(_prompt, new GenerateOptions { MaxTokens = 16 }));
        s.BindBudget(null);
        s.Reset();
        Assert.Equal(16, s.Generate(_prompt, new GenerateOptions { MaxTokens = 16 }).Tokens.Count);
    }

    [Fact]
    public void Engine_needs_its_capability_and_reports_honest_errors()
    {
        using Capability noAi = _cap.Attenuate(CapabilityRights.LedgerRead);
        Assert.Throws<CapabilityDeniedException>(() => Model.Open(noAi, Conformance.Fixture("tiny-qwen2.gguf")));
        Assert.Equal(ZxvStatus.IOError, Assert.Throws<ZxvSystemException>(() => Model.Open(_cap, "/nonexistent.gguf")).Status);
        Assert.Throws<ZxvFeatureNotAvailableException>(() => _model.LoadLora("adapter.gguf", Rational.One));

        using Model fromMemory = Model.OpenMemory(_cap, File.ReadAllBytes(Conformance.Fixture("tiny-qwen2.gguf")), File.ReadAllBytes(Conformance.Fixture("tiny-tokenizer.gguf")));
        using Session s = fromMemory.NewSession(128);
        Assert.Equal(Greedy, s.Generate(_prompt, new GenerateOptions { MaxTokens = 16 }).Tokens);
    }
}
