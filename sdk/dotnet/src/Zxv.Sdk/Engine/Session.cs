// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using Zxv.Interop;

namespace Zxv.Engine;

/// <summary>Why generation stopped.</summary>
public enum StopReason
{
    /// <summary>Not reported.</summary>
    None = 0,

    /// <summary>The model produced its end-of-sequence token.</summary>
    EndOfSequence = 1,

    /// <summary><see cref="GenerateOptions.MaxTokens"/> was reached.</summary>
    MaxTokens = 2,

    /// <summary>A stop string matched (reserved).</summary>
    StopString = 3,

    /// <summary>The token callback asked to stop (or the call was cancelled).</summary>
    Callback = 4,

    /// <summary>The context window is full.</summary>
    ContextFull = 5,

    /// <summary>The budget bound with <see cref="Session.BindBudget(Budget?)"/> ran out.</summary>
    Budget = 6,
}

/// <summary>
/// Sampling settings. Every value is exact: rationals go to the engine as
/// Q16.16 integers (<see cref="Rational.ToQ16"/>), so the same settings and
/// seed give the same tokens on every machine.
/// </summary>
public sealed record GenerateOptions
{
    /// <summary>Gets greedy decoding (temperature 0) with 128 tokens.</summary>
    public static GenerateOptions Greedy { get; } = new();

    /// <summary>Gets the most tokens to generate. Default 128.</summary>
    public int MaxTokens { get; init; } = 128;

    /// <summary>Gets the temperature; zero means greedy. E.g. <c>Rational.Parse("0.7")</c>.</summary>
    public Rational Temperature { get; init; }

    /// <summary>Gets top-k; zero turns it off.</summary>
    public int TopK { get; init; }

    /// <summary>Gets top-p (nucleus); zero turns it off. E.g. <c>Rational.Parse("0.95")</c>.</summary>
    public Rational TopP { get; init; }

    /// <summary>Gets the repetition penalty; zero turns it off. E.g. <c>Rational.Parse("1.1")</c>.</summary>
    public Rational RepeatPenalty { get; init; }

    /// <summary>Gets how many recent tokens the repetition penalty looks at.</summary>
    public int RepeatLastN { get; init; }

    /// <summary>Gets the sampling seed.</summary>
    public ulong Seed { get; init; }
}

/// <summary>One generated token.</summary>
/// <param name="Id">The token id.</param>
/// <param name="Text">The text this token completes; may be empty when it carries only part of a UTF-8 character (the rest arrives with the next token).</param>
/// <param name="Bytes">The token's raw UTF-8 bytes (possibly a partial character).</param>
public readonly record struct GeneratedToken(int Id, string Text, ReadOnlyMemory<byte> Bytes);

/// <summary>The result of a generation.</summary>
/// <param name="Text">All generated text.</param>
/// <param name="Tokens">The generated token ids.</param>
/// <param name="PromptTokenCount">How many prompt tokens were evaluated.</param>
/// <param name="StopReason">Why generation stopped.</param>
public sealed record GenerationResult(string Text, IReadOnlyList<int> Tokens, int PromptTokenCount, StopReason StopReason);

/// <summary>
/// A conversation with its own KV cache over a <see cref="Model"/>. Each
/// <see cref="Generate(string, GenerateOptions?)"/> appends to the session, so a chat
/// continues turn after turn; <see cref="Reset"/> forgets it. Calls on one
/// session are serialized; use one session per concurrent conversation.
/// </summary>
public sealed class Session : IDisposable
{
    private readonly Model _model;
    private readonly ZxvSessionHandle _handle;
    private readonly object _gate = new();
    private Budget? _budget;

    internal Session(Model model, ZxvSessionHandle handle)
    {
        _model = model;
        _handle = handle;
    }

    /// <summary>Gets the number of tokens in the KV cache.</summary>
    public int Position
    {
        get
        {
            lock (_gate)
            {
                Enter("Session.Position");
                _model.Capability.Check(NativeMethods.SessionPosition(_handle, out uint pos), "Session.Position");
                return checked((int)pos);
            }
        }
    }

    internal ZxvSessionHandle Handle => _handle;

    /// <summary>Forgets the conversation (empties the KV cache).</summary>
    public void Reset()
    {
        lock (_gate)
        {
            Enter("Session.Reset");
            _model.Capability.Check(NativeMethods.SessionReset(_handle), "Session.Reset");
        }
    }

    /// <summary>
    /// Charges <paramref name="budget"/> one unit per prompt and generated
    /// token. Prompt tokens are charged up front: if they do not fit,
    /// generation raises <see cref="BudgetExhaustedException"/> and runs
    /// nothing; running out mid-way stops with <see cref="StopReason.Budget"/>.
    /// </summary>
    /// <param name="budget">The budget, or <see langword="null"/> to unbind.</param>
    public void BindBudget(Budget? budget)
    {
        lock (_gate)
        {
            Enter("Session.BindBudget");
            if (budget is not null)
            {
                budget.Owner.Enter("Session.BindBudget");
                _handle.AddDependency(budget.Owner.Handle);
            }

            IntPtr rmag = budget is null ? IntPtr.Zero : budget.Owner.Handle.DangerousGetHandle();
            int rc = NativeMethods.SessionBindBudget(_handle, rmag, budget?.Index ?? 0);
            if (rc != (int)ZxvStatus.Ok)
            {
                if (budget is not null)
                {
                    _handle.RemoveDependency(budget.Owner.Handle);
                }

                _model.Capability.Check(rc, "Session.BindBudget");
            }

            if (_budget is not null)
            {
                _handle.RemoveDependency(_budget.Owner.Handle);
            }

            _budget = budget;
        }
    }

    /// <summary>Generates a continuation of <paramref name="prompt"/>. Needs <see cref="CapabilityRights.AiInference"/>.</summary>
    /// <param name="prompt">The prompt (appended to the conversation so far).</param>
    /// <param name="options">Sampling settings; default greedy.</param>
    /// <returns>The text, the token ids and why it stopped.</returns>
    public GenerationResult Generate(string prompt, GenerateOptions? options = null) =>
        GenerateCore(prompt, options, null, CancellationToken.None);

    /// <summary>Generates, calling <paramref name="onToken"/> for each token as it is produced.</summary>
    /// <param name="prompt">The prompt.</param>
    /// <param name="options">Sampling settings; default greedy.</param>
    /// <param name="onToken">Return <see langword="false"/> to stop (<see cref="StopReason.Callback"/>).</param>
    /// <returns>The result.</returns>
    public GenerationResult Generate(string prompt, GenerateOptions? options, Func<GeneratedToken, bool> onToken)
    {
        ArgumentNullException.ThrowIfNull(onToken);
        return GenerateCore(prompt, options, onToken, CancellationToken.None);
    }

    /// <summary>Generates on the thread pool.</summary>
    /// <param name="prompt">The prompt.</param>
    /// <param name="options">Sampling settings; default greedy.</param>
    /// <param name="cancellationToken">Stops generation at the next token.</param>
    /// <returns>The result.</returns>
    /// <exception cref="OperationCanceledException">Cancelled; the tokens generated so far stay in the session.</exception>
    public Task<GenerationResult> GenerateAsync(string prompt, GenerateOptions? options = null, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        return Task.Run(() => GenerateCore(prompt, options, null, cancellationToken), cancellationToken);
    }

    /// <summary>
    /// Streams tokens as they are generated (generation runs on the thread
    /// pool). Stopping the enumeration early, or cancelling, stops generation.
    /// </summary>
    /// <param name="prompt">The prompt.</param>
    /// <param name="options">Sampling settings; default greedy.</param>
    /// <param name="cancellationToken">Stops generation.</param>
    /// <returns>The tokens.</returns>
    public async IAsyncEnumerable<GeneratedToken> StreamAsync(string prompt, GenerateOptions? options = null, [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        var channel = Channel.CreateUnbounded<GeneratedToken>(new UnboundedChannelOptions { SingleReader = true, SingleWriter = true });
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        Task<GenerationResult> run = Task.Run(
            () =>
            {
                try
                {
                    return GenerateCore(prompt, options, t => channel.Writer.TryWrite(t), stop.Token);
                }
                finally
                {
                    channel.Writer.TryComplete();
                }
            },
            CancellationToken.None);
        try
        {
            while (await channel.Reader.WaitToReadAsync(cancellationToken).ConfigureAwait(false))
            {
                while (channel.Reader.TryRead(out GeneratedToken token))
                {
                    yield return token;
                }
            }

            await run.ConfigureAwait(false); // surfaces a generation error
        }
        finally
        {
            await stop.CancelAsync().ConfigureAwait(false);
            await Task.WhenAny(run).ConfigureAwait(false);
        }
    }

    /// <summary>Releases the session (and its hold on the model and any bound budget).</summary>
    public void Dispose() => _handle.Dispose();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnNativeToken(void* user, int token, byte* piece, nuint len)
    {
        var state = (GenerationState)GCHandle.FromIntPtr((IntPtr)user).Target!;
        return state.OnToken(token, new ReadOnlySpan<byte>(piece, checked((int)len))) ? 1 : 0;
    }

    private unsafe GenerationResult GenerateCore(string prompt, GenerateOptions? options, Func<GeneratedToken, bool>? onToken, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        GenerateOptions o = options ?? GenerateOptions.Greedy;
        ArgumentOutOfRangeException.ThrowIfNegative(o.MaxTokens, nameof(options));
        ArgumentOutOfRangeException.ThrowIfNegative(o.TopK, nameof(options));
        ArgumentOutOfRangeException.ThrowIfNegative(o.RepeatLastN, nameof(options));
        int temperature = o.Temperature.ToQ16(), topP = o.TopP.ToQ16(), penalty = o.RepeatPenalty.ToQ16();
        cancellationToken.ThrowIfCancellationRequested();

        byte[] utf8 = Encoding.UTF8.GetBytes(prompt);
        var state = new GenerationState(onToken, cancellationToken);
        GCHandle gc = GCHandle.Alloc(state);
        try
        {
            lock (_gate)
            {
                Enter("Session.Generate");
                int rc;
                uint nPrompt, nGenerated;
                int stop;
                fixed (byte* p = utf8)
                {
                    rc = NativeMethods.Generate(
                        _model.Capability.Handle,
                        _handle,
                        p,
                        (nuint)utf8.Length,
                        (uint)o.MaxTokens,
                        temperature,
                        (uint)o.TopK,
                        topP,
                        penalty,
                        (uint)o.RepeatLastN,
                        o.Seed,
                        &OnNativeToken,
                        (void*)GCHandle.ToIntPtr(gc),
                        out nPrompt,
                        out nGenerated,
                        out stop);
                }

                if (state.Error is not null)
                {
                    ExceptionDispatchInfoThrow(state.Error);
                }

                _model.Capability.Check(rc, "Session.Generate");
                cancellationToken.ThrowIfCancellationRequested();
                return new GenerationResult(state.Text, state.Ids, checked((int)nPrompt), (StopReason)stop);
            }
        }
        finally
        {
            gc.Free();
        }
    }

    private static void ExceptionDispatchInfoThrow(Exception error) =>
        System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error).Throw();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _model.EnterHeld(operation);
    }

    /// <summary>Per-call state reached from the native token callback through a <see cref="GCHandle"/>.</summary>
    private sealed class GenerationState
    {
        private readonly Func<GeneratedToken, bool>? _onToken;
        private readonly CancellationToken _cancel;
        private readonly Decoder _decoder = new UTF8Encoding(false, false).GetDecoder();
        private readonly List<byte> _bytes = new();
        private readonly char[] _chars = new char[64];

        internal GenerationState(Func<GeneratedToken, bool>? onToken, CancellationToken cancel)
        {
            _onToken = onToken;
            _cancel = cancel;
        }

        internal List<int> Ids { get; } = new();

        internal Exception? Error { get; private set; }

        internal string Text => Encoding.UTF8.GetString(_bytes.ToArray());

#pragma warning disable CA1031 // A callback exception must never unwind into native code; it is rethrown after zxv_generate returns.
        internal bool OnToken(int id, ReadOnlySpan<byte> piece)
        {
            try
            {
                Ids.Add(id);
                foreach (byte b in piece)
                {
                    _bytes.Add(b);
                }

                if (_cancel.IsCancellationRequested)
                {
                    return false;
                }

                if (_onToken is null)
                {
                    return true;
                }

                char[] chars = piece.Length + 4 <= _chars.Length ? _chars : new char[piece.Length + 4];
                int n = _decoder.GetChars(piece, chars, flush: false);
                return _onToken(new GeneratedToken(id, new string(chars, 0, n), piece.ToArray()));
            }
            catch (Exception ex)
            {
                Error = ex;
                return false;
            }
        }
#pragma warning restore CA1031
    }
}
