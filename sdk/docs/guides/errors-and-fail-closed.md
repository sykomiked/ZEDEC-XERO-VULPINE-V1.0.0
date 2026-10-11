<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Errors and fail-closed

**Who this is for:** every ZXV developer. Read it second, right after
[Getting started](getting-started.md).
**What you will be able to do:** tell a bug from a refusal from a halt, catch
the right thing in your language, and make sure a halted system stays
halted.

## Four classes of error

Every libzxv call returns a status code. `0` is `ZXV_OK`. Every negative
code belongs to one of four **classes**, and the class tells you what to do:

| Class | What it means | What to do | Examples |
|---|---|---|---|
| USAGE (1) | the caller made a mistake | fix the code or the input | `ZXV_E_ARG`, `ZXV_E_RANGE`, `ZXV_E_FORMAT`, `ZXV_E_FLOAT`, `ZXV_E_CURRENCY`, `ZXV_E_DUPLICATE` |
| REFUSED (2) | a rule said no | handle it: tell the user, try something else | `ZXV_E_FUNDS`, `ZXV_E_CAP`, `ZXV_E_BUDGET`, `ZXV_E_STATE`, `ZXV_E_CAPACITY`, `ZXV_E_VERIFY`, `ZXV_E_NOTIMPL` |
| FATAL (3) | the context is halted | stop; never retry | `ZXV_E_POISONED`, `ZXV_E_INVARIANT` |
| SYSTEM (4) | the environment failed | log it; maybe retry later | out of memory, I/O |

Branch on the **code** or the **class**, never on the message text. The
text (from `zxv_last_error`) is for people and may change between releases.

## Catching refusals

Each SDK maps the classes to its own exception types, with the same names.
Python:

```python
try:
    ledger.post(ada, bank, 100, "NGN", "E2E-9")
except zxv.InsufficientFundsError as e:          # a REFUSED subclass
    print(e.status_name, "|", e.detail)          # ZXV_E_FUNDS | account 1 has 0 available, needs 100
```

Posting the same reference twice gives `ZxvUsageError` with
`ZXV_E_DUPLICATE`, an amount of `0` gives `ZXV_E_RANGE`, and an amount of
`1.5` gives `FloatRejectedError`.

## Fail-closed: when the context halts

A context **halts** when something has gone wrong that must not be ignored:
an operator calls `halt`, the quarantine log fills up, or an integrity check
fails. Halting is also called **poisoning**. After it:

1. Every later call on that context, from any thread, returns
   `ZXV_E_POISONED`. This check runs first, before the arguments are even
   looked at. A retry cannot succeed.
2. The SDK raises a **terminal** error type that ordinary error handling
   does not catch, where the language allows it.

Only a new context gets you going again. This is called **fail-closed**: when
in doubt, the system stops rather than carrying on.

You can test it yourself with `halt`. Each example below was run.

**Python.** `ZxvFatal` derives from `BaseException`, so `except Exception`
does not catch it:

```python
ctx.halt("operator stop")
try:
    ledger.balance(ada)
except Exception:
    print("never printed")
except BaseException as e:
    print(type(e).__name__, e.status_name)     # ZxvFatal ZXV_E_POISONED
print(ctx.is_poisoned)                         # True
```

**Java.** `ZxvFatalError` extends `Error`, so `catch (Exception e)` misses
it:

```java
ctx.halt("operator stop");
try {
    ledger.balance(ada);
} catch (Exception e) {
    System.out.println("never printed: Exception does not catch ZxvFatalError");
} catch (ZxvFatalError e) {
    System.out.println("fatal: " + e.statusName() + ", poisoned=" + ctx.isPoisoned());
}
```

**C#.** `ZxvFatalException` does not derive from `ZxvException`:

```csharp
zxv.Halt("operator stop");
try
{
    ledger.GetBalances(ada.Id);
}
catch (ZxvException)
{
    Console.WriteLine("never printed: ZxvFatalException is not a ZxvException");
}
catch (ZxvFatalException e)
{
    Console.WriteLine($"fatal: {e.StatusName}, halted={zxv.IsHalted}");
}
```

C# cannot make an exception uncatchable, so a bare `catch` or
`catch (Exception)` still sees it. The context stays poisoned in C either
way.

**TypeScript.** `ZxvFatalError` is not a `ZxvError`:

```ts
ctx.halt('operator stop');
try {
  ledger.balance(ada);
} catch (e) {
  console.log(e instanceof ZxvFatalError, e instanceof ZxvError);   // true false
}
```

**C.** Check the class:

```c
zxv_ctx_halt(ctx, "operator stop");
int32_t rc = zxv_cap_check(root, ZXV_CAP_LEDGER_READ);
/* rc == ZXV_E_POISONED, zxv_status_class(rc) == 3 (FATAL) */
```

## Making a halt truly final

A program can always catch the terminal type if it tries hard enough (Java
`catch (Throwable t)`, Python `except BaseException`). If your process must
stop at once, choose the **abort** policy. The SDK then ends the process on
the first fatal status, and nothing can catch that:

| SDK | How |
|---|---|
| Python | `zxv.Context(fatal_policy=zxv.FatalPolicy.ABORT)` or `zxv.set_default_fatal_policy(...)` |
| Java | `ZxvContext.create(FatalPolicy.ABORT)` |
| .NET | `ZxvLibrary.FatalPolicy = FatalPolicy.FailFast` |
| TypeScript | `zxv.createContext({ fatalPolicy: 'abort' })` |

Run the process under a supervisor (systemd, Kubernetes, and so on) that
restarts it into a fresh context, after a person has looked at why it halted.

## Common codes and their fixes

See [Troubleshooting](troubleshooting.md#error-codes-and-what-to-do) for a
table of the codes you are most likely to meet.

## Next

- [SDK_DESIGN §5](../SDK_DESIGN.md#5-errors): the exact rules every SDK follows.
- [Budgets and time](budgets-and-time.md)
