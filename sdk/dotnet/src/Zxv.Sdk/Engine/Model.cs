// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using System.Threading;
using Zxv.Interop;

namespace Zxv.Engine;

/// <summary>A model architecture the engine runs.</summary>
public enum ModelArchitecture
{
    /// <summary>Not reported.</summary>
    Unknown = 0,

    /// <summary>Qwen2.</summary>
    Qwen2 = 1,

    /// <summary>Qwen3.</summary>
    Qwen3 = 2,

    /// <summary>Llama.</summary>
    Llama = 3,
}

/// <summary>How a session stores its KV cache.</summary>
public enum KvCacheType
{
    /// <summary>Exact Q16 cache, 4 bytes per value.</summary>
    Q16 = 0,

    /// <summary>8-bit cache, about 1.16 bytes per value.</summary>
    Q8 = 1,
}

/// <summary>
/// A GGUF model loaded into the integer inference engine (qwen2, qwen3,
/// llama; F32/F16/BF16/Q8_0/Q4_0/Q5_x/Q4_K/Q6_K weights). Inference runs in
/// integer arithmetic only, so the same inputs give bit-identical tokens on
/// every machine. A model is read-only after loading and may be shared by
/// sessions on any threads.
/// </summary>
/// <example>
/// <code>
/// using Model model = Model.Open(cap, "qwen2.gguf");
/// using Session chat = model.NewSession();
/// GenerationResult r = chat.Generate("Hello", new GenerateOptions { MaxTokens = 32 });
/// Console.WriteLine(r.Text);
/// </code>
/// </example>
public sealed class Model : IDisposable
{
    private const int InfoArch = 1, InfoVocab = 2, InfoCtxTrain = 3, InfoLayers = 4, InfoEmbd = 5, InfoHeads = 6, InfoHeadsKv = 7, InfoEos = 8;

    private Model(Capability capability, ZxvModelHandle handle)
    {
        Capability = capability;
        Handle = handle;
    }

    /// <summary>Gets the architecture.</summary>
    public ModelArchitecture Architecture => (ModelArchitecture)Info(InfoArch);

    /// <summary>Gets the vocabulary size.</summary>
    public int VocabularySize => checked((int)Info(InfoVocab));

    /// <summary>Gets the context length the model was trained with.</summary>
    public int TrainingContextLength => checked((int)Info(InfoCtxTrain));

    /// <summary>Gets the number of transformer layers.</summary>
    public int LayerCount => checked((int)Info(InfoLayers));

    /// <summary>Gets the embedding width.</summary>
    public int EmbeddingLength => checked((int)Info(InfoEmbd));

    /// <summary>Gets the number of attention heads.</summary>
    public int HeadCount => checked((int)Info(InfoHeads));

    /// <summary>Gets the number of key/value heads.</summary>
    public int KeyValueHeadCount => checked((int)Info(InfoHeadsKv));

    /// <summary>Gets the end-of-sequence token id.</summary>
    public int EndOfSequenceToken => checked((int)Info(InfoEos));

    internal Capability Capability { get; }

    internal ZxvModelHandle Handle { get; }

    // SafeHandle.IsClosed stays false while sessions or adapters hold a
    // reference, so disposal is tracked here (observably disposed at once).
    private int _disposed;

    /// <summary>Opens a GGUF file. Needs <see cref="CapabilityRights.AiInference"/>.</summary>
    /// <param name="capability">The capability presented now and on every generation.</param>
    /// <param name="modelPath">The model file.</param>
    /// <param name="tokenizerPath">A separate tokenizer GGUF, or <see langword="null"/> for the model's own.</param>
    /// <returns>The model; dispose it after its sessions.</returns>
    /// <exception cref="ZxvSystemException">The file cannot be read (<see cref="ZxvStatus.IOError"/>).</exception>
    /// <exception cref="ZxvUsageException">The file is not a GGUF the engine can run (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static Model Open(Capability capability, string modelPath, string? tokenizerPath = null)
    {
        ArgumentNullException.ThrowIfNull(capability);
        Guard.Text(modelPath, nameof(modelPath));
        if (tokenizerPath is not null)
        {
            Guard.Text(tokenizerPath, nameof(tokenizerPath));
        }

        capability.Enter("Model.Open");
        int rc = NativeMethods.ModelOpen(capability.Handle, modelPath, tokenizerPath, out ZxvModelHandle handle);
        return Wrap(capability, rc, handle, "Model.Open");
    }

    /// <summary>Loads a model from memory; the bytes are copied, so the caller may release them.</summary>
    /// <param name="capability">Needs <see cref="CapabilityRights.AiInference"/>.</param>
    /// <param name="model">The GGUF bytes.</param>
    /// <param name="tokenizer">A separate tokenizer GGUF, or empty for the model's own.</param>
    /// <returns>The model.</returns>
    public static unsafe Model OpenMemory(Capability capability, ReadOnlySpan<byte> model, ReadOnlySpan<byte> tokenizer = default)
    {
        ArgumentNullException.ThrowIfNull(capability);
        if (model.IsEmpty)
        {
            throw new ArgumentException("The model bytes must not be empty.", nameof(model));
        }

        capability.Enter("Model.OpenMemory");
        int rc;
        ZxvModelHandle handle;
        fixed (byte* m = model)
        fixed (byte* t = tokenizer)
        {
            rc = NativeMethods.ModelOpenMemory(capability.Handle, m, (nuint)model.Length, tokenizer.IsEmpty ? null : t, (nuint)tokenizer.Length, out handle);
        }

        return Wrap(capability, rc, handle, "Model.OpenMemory");
    }

    /// <summary>Converts UTF-8 text to token ids.</summary>
    /// <param name="text">The text.</param>
    /// <param name="parseSpecial">Whether special tokens written as text (e.g. <c>&lt;|im_start|&gt;</c>) are recognised.</param>
    /// <returns>The token ids.</returns>
    public int[] Tokenize(string text, bool parseSpecial = true)
    {
        ArgumentNullException.ThrowIfNull(text);
        return Tokenize(Encoding.UTF8.GetBytes(text), parseSpecial);
    }

    /// <summary>Converts UTF-8 bytes to token ids.</summary>
    /// <param name="utf8">The text as UTF-8.</param>
    /// <param name="parseSpecial">Whether special tokens written as text are recognised.</param>
    /// <returns>The token ids.</returns>
    public unsafe int[] Tokenize(ReadOnlySpan<byte> utf8, bool parseSpecial = true)
    {
        Enter("Model.Tokenize");
        fixed (byte* t = utf8)
        {
            int rc = NativeMethods.Tokenize(Handle, t, (nuint)utf8.Length, parseSpecial ? 1 : 0, null, 0, out nuint count);
            if (rc == (int)ZxvStatus.Ok)
            {
                return Array.Empty<int>();
            }

            if (rc != (int)ZxvStatus.BufferTooSmall)
            {
                Capability.Check(rc, "Model.Tokenize");
            }

            if (count == 0)
            {
                return Array.Empty<int>();
            }

            // One spare slot: R4 counts a buffer of exactly the needed size as too small.
            var ids = new int[checked((int)count + 1)];
            fixed (int* p = ids)
            {
                Capability.Check(NativeMethods.Tokenize(Handle, t, (nuint)utf8.Length, parseSpecial ? 1 : 0, p, (nuint)ids.Length, out count), "Model.Tokenize");
            }

            return ids[..checked((int)count)];
        }
    }

    /// <summary>Converts token ids back to text.</summary>
    /// <param name="tokens">The ids.</param>
    /// <returns>The text.</returns>
    public unsafe string Detokenize(ReadOnlySpan<int> tokens)
    {
        Enter("Model.Detokenize");
        fixed (int* ids = tokens)
        {
            int* p = ids;
            nuint n = (nuint)tokens.Length;
            return NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.Detokenize(Handle, p, n, b, c, out l), "Model.Detokenize");
        }
    }

    /// <summary>Creates a session (a conversation with its own KV cache).</summary>
    /// <param name="contextLength">Context window in tokens; 0 for min(training context, 4096).</param>
    /// <param name="kvCache">KV cache precision.</param>
    /// <returns>The session; dispose it before the model.</returns>
    public Session NewSession(int contextLength = 0, KvCacheType kvCache = KvCacheType.Q16)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(contextLength);
        Enter("Model.NewSession");
        int rc = NativeMethods.SessionCreate(Handle, (uint)contextLength, (int)kvCache, out ZxvSessionHandle session);
        if (rc != (int)ZxvStatus.Ok)
        {
            session.Dispose();
            Capability.Check(rc, "Model.NewSession");
        }

        session.AddDependency(Handle);
        return new Session(this, session);
    }

    /// <summary>
    /// Loads a LoRA adapter. NOT BUILT YET: the engine declares LoRA in the
    /// ABI but does not implement it (<see cref="ZxvFeatures.Lora"/> is never set),
    /// so this always raises <see cref="ZxvFeatureNotAvailableException"/> today.
    /// </summary>
    /// <param name="path">The adapter file.</param>
    /// <param name="scale">The adapter scale (Q16.16 on the wire).</param>
    /// <returns>Never returns today.</returns>
    /// <exception cref="ZxvFeatureNotAvailableException">Always, until the engine implements LoRA.</exception>
    public LoraAdapter LoadLora(string path, Rational scale)
    {
        Guard.Text(path, nameof(path));
        Enter("Model.LoadLora");
        int rc = NativeMethods.LoraLoad(Capability.Handle, Handle, path, scale.ToQ16(), out ZxvLoraHandle lora);
        if (rc != (int)ZxvStatus.Ok)
        {
            lora.Dispose();
            Capability.Check(rc, "Model.LoadLora");
        }

        lora.AddDependency(Handle);
        return new LoraAdapter(this, lora);
    }

    /// <summary>Releases the model (the native model lives until its last session is released).</summary>
    /// <remarks>This model throws <see cref="ObjectDisposedException"/> at once, even while sessions still hold it.</remarks>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
        {
            Handle.Dispose();
        }
    }

    internal void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        EnterHeld(operation);
    }

    /// <summary>
    /// The check for a session or adapter that holds this model: it keeps
    /// working after <see cref="Dispose"/>, since its reference keeps the
    /// native model alive, but not once the capability is gone or the context halts.
    /// </summary>
    internal void EnterHeld(string operation)
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        Capability.Enter(operation);
    }

    private static Model Wrap(Capability capability, int rc, ZxvModelHandle handle, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            handle.Dispose();
            capability.Check(rc, operation);
        }

        handle.AddDependency(capability.Handle);
        return new Model(capability, handle);
    }

    private long Info(int key)
    {
        Enter("Model.Info");
        Capability.Check(NativeMethods.ModelInfo(Handle, key, out long value), "Model.Info");
        return value;
    }
}

/// <summary>A LoRA adapter (declared by the ABI; the engine does not build it yet).</summary>
public sealed class LoraAdapter : IDisposable
{
    private readonly Model _model;
    private readonly ZxvLoraHandle _handle;

    internal LoraAdapter(Model model, ZxvLoraHandle handle)
    {
        _model = model;
        _handle = handle;
    }

    /// <summary>Applies the adapter to a session.</summary>
    /// <param name="session">A session of the same model.</param>
    public void ApplyTo(Session session)
    {
        ArgumentNullException.ThrowIfNull(session);
        _model.EnterHeld("LoraAdapter.ApplyTo");
        _model.Capability.Check(NativeMethods.LoraApply(_model.Capability.Handle, session.Handle, _handle), "LoraAdapter.ApplyTo");
    }

    /// <summary>Releases the adapter.</summary>
    public void Dispose() => _handle.Dispose();
}
