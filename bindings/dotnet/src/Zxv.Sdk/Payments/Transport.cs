// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Messaging;

namespace Zxv.Payments;

/// <summary>
/// Hands a serialized ISO 20022 message to a network. The SDK ships no network
/// transport: PAPSS, SWIFT, CIPS or RTGS connectivity is provided through
/// scheme onboarding and plugs in here.
/// </summary>
public interface IMxTransport
{
    /// <summary>Gets a short name for logs, e.g. <c>none</c> or <c>directory</c>.</summary>
    string Name { get; }

    /// <summary>Sends one message.</summary>
    /// <param name="document">The message.</param>
    /// <param name="cancellationToken">Cancels the send.</param>
    /// <returns>A task that completes when the transport has accepted the message.</returns>
    Task SendAsync(MxDocument document, CancellationToken cancellationToken);
}

/// <summary>A transport that accepts and discards every message (the default).</summary>
public sealed class NullMxTransport : IMxTransport
{
    /// <summary>The shared instance.</summary>
    public static readonly NullMxTransport Instance = new();

    /// <inheritdoc/>
    public string Name => "none";

    /// <inheritdoc/>
    public Task SendAsync(MxDocument document, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(document);
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }
}

/// <summary>
/// Writes each message as <c>&lt;MsgId&gt;.xml</c> into a directory: a file drop
/// for an operator's gateway, or for inspection in tests. Never overwrites.
/// </summary>
public sealed class DirectoryMxTransport : IMxTransport
{
    private readonly string _directory;

    /// <summary>Initializes a new instance.</summary>
    /// <param name="directory">The drop directory; created if missing.</param>
    public DirectoryMxTransport(string directory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        _directory = Path.GetFullPath(directory);
        Directory.CreateDirectory(_directory);
    }

    /// <inheritdoc/>
    public string Name => "directory";

    /// <inheritdoc/>
    public async Task SendAsync(MxDocument document, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(document);
        foreach (char ch in document.MessageId)
        {
            if (!char.IsAsciiLetterOrDigit(ch) && ch != '-' && ch != '_' && ch != '.')
            {
                throw new ArgumentException("MessageId contains characters unsafe for a file name.", nameof(document));
            }
        }

        string path = Path.Combine(_directory, document.MessageId + ".xml");
        var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, useAsync: true);
        await using (stream.ConfigureAwait(false))
        {
            byte[] bytes = Encoding.UTF8.GetBytes(document.Xml);
            await stream.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
        }
    }
}
