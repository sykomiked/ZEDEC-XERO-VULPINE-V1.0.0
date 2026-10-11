// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Diagnostics;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using Zxv.Interop;

namespace Zxv.Gateway.Infrastructure;

/// <summary>
/// Maps SDK and argument exceptions to RFC 9457 problem details. Refusals are
/// logged at Information without a stack trace; anything unexpected is logged
/// at Error and answered with a generic 500 that leaks no internals. (The
/// framework's own ExceptionHandlerMiddleware log is silenced in appsettings so
/// a refused payment does not appear as an error with a stack trace.)
/// </summary>
internal sealed partial class ZxvExceptionHandler : IExceptionHandler
{
    private readonly IProblemDetailsService _problems;
    private readonly ILogger<ZxvExceptionHandler> _logger;

    public ZxvExceptionHandler(IProblemDetailsService problems, ILogger<ZxvExceptionHandler> logger)
    {
        _problems = problems;
        _logger = logger;
    }

    [LoggerMessage(EventId = 2000, Level = LogLevel.Information, Message = "{Method} {Path} refused with {Status}: {Type}")]
    private static partial void Refused(ILogger logger, string method, string path, int status, string? type);

    [LoggerMessage(EventId = 2001, Level = LogLevel.Error, Message = "{Method} {Path} failed unexpectedly")]
    private static partial void Unexpected(ILogger logger, Exception exception, string method, string path);

    /// <summary>The HTTP status for a native status. Kept in one table so the OpenAPI document can cite it.</summary>
    internal static int HttpStatusFor(ZxvStatus status) => status switch
    {
        ZxvStatus.NotFound => StatusCodes.Status404NotFound,
        ZxvStatus.Duplicate => StatusCodes.Status409Conflict,
        ZxvStatus.InsufficientFunds or ZxvStatus.InvalidCurrency or ZxvStatus.InvariantViolated or ZxvStatus.InvalidState => StatusCodes.Status422UnprocessableEntity,
        ZxvStatus.InvalidArgument or ZxvStatus.AmountOutOfRange or ZxvStatus.InvalidUtf8 or ZxvStatus.TooLong or ZxvStatus.InvalidField => StatusCodes.Status400BadRequest,
        ZxvStatus.NotImplemented => StatusCodes.Status501NotImplemented,
        ZxvStatus.CapacityExceeded or ZxvStatus.OutOfMemory => StatusCodes.Status503ServiceUnavailable,
        _ => StatusCodes.Status500InternalServerError,
    };

    public async ValueTask<bool> TryHandleAsync(HttpContext httpContext, Exception exception, CancellationToken cancellationToken)
    {
        ProblemDetails problem;
        switch (exception)
        {
            case ZxvTransportException tx:
                problem = new ProblemDetails
                {
                    Status = StatusCodes.Status502BadGateway,
                    Title = "Payment booked; message transport failed",
                    Detail = "The ledger posting stands. Retransmit with POST /v1/payments/{endToEndId}/retransmit.",
                    Type = "urn:zxv:problem:transport",
                };
                problem.Extensions["endToEndId"] = tx.Receipt?.EndToEndId;
                break;
            case ZxvException zx:
                problem = new ProblemDetails
                {
                    Status = HttpStatusFor(zx.Status),
                    Title = zx.Status == ZxvStatus.NotImplemented ? "Feature not available in this build" : "Operation refused",
                    Detail = zx.Detail,
                    Type = "urn:zxv:problem:" + zx.Code.ToLowerInvariant(),
                };
                problem.Extensions["code"] = zx.Code;
                problem.Extensions["operation"] = zx.Operation;
                break;
            case ArgumentException or FormatException or InvalidOperationException when exception is not ObjectDisposedException:
                problem = new ProblemDetails
                {
                    Status = StatusCodes.Status400BadRequest,
                    Title = "Invalid request",
                    Detail = exception.Message,
                    Type = "urn:zxv:problem:invalid-request",
                };
                break;
            case BadHttpRequestException bad:
                problem = new ProblemDetails { Status = bad.StatusCode, Title = "Bad request", Detail = bad.Message };
                break;
            default:
                Unexpected(_logger, exception, httpContext.Request.Method, httpContext.Request.Path);
                return false; // 500 via the default problem-details path, without leaking internals
        }

        Refused(_logger, httpContext.Request.Method, httpContext.Request.Path, problem.Status ?? 500, problem.Type);

        httpContext.Response.StatusCode = problem.Status ?? StatusCodes.Status500InternalServerError;
        return await _problems.TryWriteAsync(new ProblemDetailsContext { HttpContext = httpContext, ProblemDetails = problem, Exception = exception });
    }
}
