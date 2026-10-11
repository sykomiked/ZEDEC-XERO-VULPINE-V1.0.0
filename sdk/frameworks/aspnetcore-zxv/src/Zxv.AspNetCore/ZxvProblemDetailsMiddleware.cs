// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zxv.Interop;

namespace Zxv.AspNetCore;

/// <summary>
/// Maps ZXV exceptions thrown further down the pipeline to RFC 9457 problem
/// details, by status class, without leaking secrets.
/// </summary>
/// <remarks>
/// <para>
/// The response carries a stable <c>type</c> (<c>urn:zxv:problem:zxv_e_cap</c>),
/// a generic <c>title</c>, the HTTP <c>status</c>, and the extensions
/// <c>code</c> (the status name, e.g. <c>ZXV_E_CAP</c>), <c>class</c>
/// (<c>Usage</c>, <c>Refused</c>, <c>System</c>, <c>Fatal</c>) and
/// <c>traceId</c>. It never carries the exception message, the native
/// <c>Detail</c> text (which can name accounts, references or currencies),
/// the SDK operation, or a stack trace; those go to the server log only.
/// </para>
/// <para>
/// A <see cref="ZxvFatalException"/> is not swallowed into an ordinary
/// error: it trips <see cref="ZxvFatalState"/> (health goes Unhealthy; with
/// <see cref="ZxvAspNetCoreOptions.StopApplicationOnFatal"/> the host stops),
/// is logged at Critical, and is answered 503. A
/// <see cref="BadHttpRequestException"/> (for example a JSON float where a
/// <see cref="Rational"/> belongs) is answered with its own status (400) and
/// no message. Other exceptions pass through untouched.
/// </para>
/// </remarks>
public sealed partial class ZxvProblemDetailsMiddleware
{
    private readonly RequestDelegate _next;
    private readonly ILogger<ZxvProblemDetailsMiddleware> _logger;

    /// <summary>Initializes a new instance.</summary>
    /// <param name="next">The next middleware.</param>
    /// <param name="logger">The logger.</param>
    public ZxvProblemDetailsMiddleware(RequestDelegate next, ILogger<ZxvProblemDetailsMiddleware> logger)
    {
        _next = next;
        _logger = logger;
    }

    /// <summary>Runs the rest of the pipeline and maps ZXV exceptions.</summary>
    /// <param name="context">The HTTP context.</param>
    /// <returns>A task.</returns>
    public async Task InvokeAsync(HttpContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        try
        {
            await _next(context).ConfigureAwait(false);
        }
        catch (ZxvFatalException fatal) when (!context.Response.HasStarted)
        {
            string code = fatal.StatusName;
            Fatal(_logger, fatal, context.Request.Method, context.Request.Path, code);
            context.RequestServices.GetService<ZxvFatalState>()?.Trip(code);
            await WriteAsync(context, StatusCodes.Status503ServiceUnavailable, code, ZxvStatusClass.Fatal).ConfigureAwait(false);
        }
        catch (ZxvException zx) when (!context.Response.HasStarted)
        {
            ZxvStatusClass cls = ZxvProblemMapping.ClassOf(zx);
            int status = ZxvProblemMapping.HttpStatusFor(zx.Status, cls);
            if (cls == ZxvStatusClass.System || status >= 500)
            {
                Failed(_logger, zx, context.Request.Method, context.Request.Path, zx.StatusName);
            }
            else
            {
                Refused(_logger, context.Request.Method, context.Request.Path, status, zx.StatusName);
            }

            await WriteAsync(context, status, zx.StatusName, cls).ConfigureAwait(false);
        }
        catch (BadHttpRequestException bad) when (!context.Response.HasStarted)
        {
            // A body or parameter that does not bind, such as a JSON float
            // where a Rational belongs. With RouteHandlerOptions.ThrowOnBadRequest
            // (the Development default) it would otherwise reach
            // UseExceptionHandler, which answers 500 in .NET 8.
            BadRequest(_logger, context.Request.Method, context.Request.Path, bad.StatusCode);
            var problem = new ProblemDetails
            {
                Type = "urn:zxv:problem:bad-request",
                Title = "Bad request",
                Status = bad.StatusCode,
            };
            problem.Extensions["traceId"] = context.TraceIdentifier;
            await WriteProblemAsync(context, problem).ConfigureAwait(false);
        }
    }

    private static async Task WriteAsync(HttpContext context, int status, string code, ZxvStatusClass cls)
    {
        ProblemDetails problem = ZxvProblemMapping.Create(status, code, cls);
        problem.Extensions["traceId"] = context.TraceIdentifier;
        await WriteProblemAsync(context, problem).ConfigureAwait(false);
    }

    private static async Task WriteProblemAsync(HttpContext context, ProblemDetails problem)
    {
        context.Response.Clear();
        context.Response.StatusCode = problem.Status ?? StatusCodes.Status500InternalServerError;
        // The exception is deliberately not handed to the writer, so no
        // CustomizeProblemDetails hook can copy its message into the response.
        IProblemDetailsService? writer = context.RequestServices.GetService<IProblemDetailsService>();
        if (writer is null || !await writer.TryWriteAsync(new ProblemDetailsContext { HttpContext = context, ProblemDetails = problem }).ConfigureAwait(false))
        {
            await context.Response.WriteAsJsonAsync(problem, (System.Text.Json.JsonSerializerOptions?)null, "application/problem+json").ConfigureAwait(false);
        }
    }

    [LoggerMessage(EventId = 3200, Level = LogLevel.Information, Message = "{Method} {Path} refused with {Status}: {Code}")]
    private static partial void Refused(ILogger logger, string method, string path, int status, string code);

    [LoggerMessage(EventId = 3203, Level = LogLevel.Information, Message = "{Method} {Path} bad request ({Status})")]
    private static partial void BadRequest(ILogger logger, string method, string path, int status);

    [LoggerMessage(EventId = 3201, Level = LogLevel.Error, Message = "{Method} {Path} failed: {Code}")]
    private static partial void Failed(ILogger logger, Exception exception, string method, string path, string code);

    [LoggerMessage(EventId = 3202, Level = LogLevel.Critical, Message = "{Method} {Path} met a halted ZXV context: {Code}")]
    private static partial void Fatal(ILogger logger, Exception exception, string method, string path, string code);
}

/// <summary>The one table from ZXV statuses to HTTP.</summary>
public static class ZxvProblemMapping
{
    /// <summary>
    /// The HTTP status for a ZXV status: specific statuses first, then by class
    /// (USAGE 400, REFUSED 409, SYSTEM 500, FATAL 503).
    /// </summary>
    /// <param name="status">The ZXV status.</param>
    /// <param name="statusClass">Its class.</param>
    /// <returns>The HTTP status code.</returns>
    public static int HttpStatusFor(ZxvStatus status, ZxvStatusClass statusClass) => status switch
    {
        ZxvStatus.NotFound => StatusCodes.Status404NotFound,
        ZxvStatus.Duplicate => StatusCodes.Status409Conflict,
        ZxvStatus.CapabilityDenied => StatusCodes.Status403Forbidden,
        ZxvStatus.InsufficientFunds or ZxvStatus.InvalidCurrency or ZxvStatus.Declined or ZxvStatus.VerificationFailed => StatusCodes.Status422UnprocessableEntity,
        ZxvStatus.BudgetExhausted => StatusCodes.Status429TooManyRequests,
        ZxvStatus.NotImplemented => StatusCodes.Status501NotImplemented,
        ZxvStatus.CapacityExceeded or ZxvStatus.OutOfMemory => StatusCodes.Status503ServiceUnavailable,
        _ => statusClass switch
        {
            ZxvStatusClass.Usage => StatusCodes.Status400BadRequest,
            ZxvStatusClass.Refused => StatusCodes.Status409Conflict,
            ZxvStatusClass.Fatal => StatusCodes.Status503ServiceUnavailable,
            _ => StatusCodes.Status500InternalServerError,
        },
    };

    /// <summary>A problem with no secret-bearing text: type, generic title, status, code and class.</summary>
    /// <param name="status">The HTTP status.</param>
    /// <param name="code">The ZXV status name.</param>
    /// <param name="statusClass">The ZXV status class.</param>
    /// <returns>The problem details.</returns>
    public static ProblemDetails Create(int status, string code, ZxvStatusClass statusClass)
    {
        ArgumentNullException.ThrowIfNull(code);
        var problem = new ProblemDetails
        {
            Type = "urn:zxv:problem:" + code.ToLowerInvariant(),
            Title = statusClass switch
            {
                ZxvStatusClass.Usage => "Invalid request",
                ZxvStatusClass.Refused => "Operation refused",
                ZxvStatusClass.Fatal => "Service halted",
                _ => "Internal error",
            },
            Status = status,
        };
        problem.Extensions["code"] = code;
        problem.Extensions["class"] = statusClass.ToString();
        return problem;
    }

    /// <summary>The class of an exception's status. Exceptions made in managed code (status Internal) count as SYSTEM unless their type says otherwise.</summary>
    /// <param name="exception">The exception.</param>
    /// <returns>The class.</returns>
    public static ZxvStatusClass ClassOf(ZxvException exception) => exception switch
    {
        null => throw new ArgumentNullException(nameof(exception)),
        ZxvUsageException => ZxvStatusClass.Usage,
        ZxvRefusedException => ZxvStatusClass.Refused,
        ZxvSystemException => ZxvStatusClass.System,
        _ => exception.StatusClass,
    };
}

/// <summary>Adds <see cref="ZxvProblemDetailsMiddleware"/> to the pipeline.</summary>
public static class ZxvApplicationBuilderExtensions
{
    /// <summary>
    /// Maps ZXV exceptions from everything after this call to problem details.
    /// Put it early, after <c>UseExceptionHandler</c> if you use one.
    /// </summary>
    /// <param name="app">The application builder.</param>
    /// <returns>The application builder.</returns>
    public static IApplicationBuilder UseZxvProblemDetails(this IApplicationBuilder app) =>
        app.UseMiddleware<ZxvProblemDetailsMiddleware>();
}
