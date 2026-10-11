<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Getting started: your first ledger in five languages

**Who this is for:** a developer who has never used ZXV.
**What you will have at the end:** a program that opens two accounts, moves
150.00 NGN between them, prints the balance, and adds 0.1 and 0.2 exactly.
You will run it in one of Java, .NET, Python, TypeScript or C. All five
versions do the same thing, because every SDK wraps the same C library,
[`libzxv`](../../libzxv/).

Pick your language below and open its section. You only need one.

## Before you start: build the C library

Every SDK needs `libzxv`. Build it once, from the root of the repository:

```sh
make -C sdk/libzxv all fixtures
```

This makes `sdk/libzxv/build/libzxv.so` (`libzxv.dylib` on macOS) and a tiny
test model in `sdk/conformance/fixtures/`. You need a C compiler (gcc or
clang) and `make`.

Each SDK looks for the library in the same order: a path you pass in code,
then the `ZXV_LIBRARY_PATH` environment variable, then a copy bundled with
the package, then the system loader path. While you are learning, the
easiest choice is:

```sh
export ZXV_LIBRARY_PATH=$PWD/sdk/libzxv/build
```

## The example, in words

1. Create a **context**: one platform instance. It owns a ledger.
2. Make a **root capability**: a token that says what this code may do.
   Every call that reads or changes state must present one. (The
   [capabilities guide](capabilities.md) explains why.)
3. Enable the Nigerian naira (`NGN`) in this context.
4. Open two accounts: `bank`, an **issuer** account (it may go below zero,
   because it is where money enters the ledger), and `ada`, a **holder**
   account (it can never go below zero).
5. Post a transfer of 15,000 kobo (150.00 NGN) from `bank` to `ada`. Money
   is always an integer count of the currency's smallest unit (its **minor
   unit**). There are no floats.
6. Print `ada`'s balance, then print `0.1 + 0.2`, computed exactly.

Expected output, in every language:

```text
ada holds 15000 kobo
0.1 + 0.2 = 0.3
```

<details>
<summary><strong>Java</strong> (also Kotlin and Scala)</summary>

You need JDK 22 or later (the SDK uses the Foreign Function & Memory API, not
JNI) and Maven.

```sh
cd sdk/java
mvn package -DskipTests          # writes target/zxv-1.1.0.jar
```

Save this as `LedgerQuickstart.java`:

```java
import org.zedec.zxv.*;
import org.zedec.zxv.ledger.*;

public class LedgerQuickstart {
    public static void main(String[] args) {
        try (ZxvContext ctx = ZxvContext.create();
             Capability root = ctx.rootCapability(Permission.all())) {
            ctx.enableCurrency(root, "NGN");
            Ledger ledger = Ledger.of(root);
            Account bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER);
            Account ada = ledger.openAccount("ada", "NGN", AccountKind.HOLDER);
            ledger.post(bank, ada, 15_000, "NGN", "E2E-0001");     // 150.00 NGN, in kobo
            System.out.println("ada holds " + ledger.balance(ada).equity() + " kobo");
        }
        System.out.println("0.1 + 0.2 = " + Rational.parse("0.1").add(Rational.parse("0.2")));
    }
}
```

Run it as a single-file program. `--enable-native-access` allows the SDK to
call native code:

```sh
java --enable-native-access=ALL-UNNAMED \
     -cp sdk/java/target/zxv-1.1.0.jar \
     -Dzxv.library=$PWD/sdk/libzxv/build/libzxv.so \
     LedgerQuickstart.java
```

`try-with-resources` closes the capability and then the context. Every
native handle in the Java SDK is `AutoCloseable`. Next: the
[Java README](../../java/).

</details>

<details>
<summary><strong>.NET</strong> (C#, F#)</summary>

You need the .NET 8 SDK. Create a console project and reference the SDK
project:

```sh
dotnet new console -o LedgerQuickstart
cd LedgerQuickstart
dotnet add reference ../sdk/dotnet/src/Zxv.Sdk/Zxv.Sdk.csproj   # adjust the path
```

Replace `Program.cs` with:

```csharp
using Zxv;
using Zxv.Ledger;

using ZxvPlatform zxv = ZxvPlatform.Create();
using Capability root = zxv.CreateRootCapability();
var ngn = zxv.Currencies.Enable(root, "NGN");
CapabilityLedger ledger = zxv.Ledger.WithCapability(root);
Account bank = ledger.OpenAccount("bank", "NGN", AccountKind.Issuer);
Account ada = ledger.OpenAccount("ada", "NGN");
ledger.Post(new Transfer(bank.Id, ada.Id, Money.FromMinorUnits(15_000, ngn), "E2E-0001"));  // 150.00 NGN
Console.WriteLine($"ada holds {ledger.GetBalances(ada.Id).Equity.MinorUnits} kobo");
Console.WriteLine($"0.1 + 0.2 = {Rational.Parse("0.1") + Rational.Parse("0.2")}");
```

```sh
ZXV_LIBRARY_PATH=/path/to/sdk/libzxv/build/libzxv.so dotnet run
```

`ZxvPlatform` owns one context. `using` disposes the capability and then the
platform. Next: the [.NET README](../../dotnet/).

</details>

<details>
<summary><strong>Python</strong></summary>

You need Python 3.10 or later. The SDK uses cffi in ABI mode, so there is
nothing to compile.

```sh
cd sdk/python
python3 -m venv .venv && . .venv/bin/activate
pip install -e '.[test]'
export ZXV_LIBRARY_PATH=$PWD/../libzxv/build
```

Save this as `ledger.py`:

```python
import zxv

with zxv.Context() as ctx:
    root = ctx.root_capability()
    ctx.currencies.enable("NGN", cap=root)
    ledger = zxv.Ledger(root)
    bank = ledger.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    ada = ledger.open_account("ada", "NGN")
    ledger.post(bank, ada, 15_000, "NGN", "E2E-0001")     # 150.00 NGN, in kobo
    print("ada holds", ledger.balance(ada).equity, "kobo")

print("0.1 + 0.2 =", zxv.Rational.parse("0.1") + zxv.Rational.parse("0.2"))
```

```sh
python ledger.py
```

The `with` block closes the context, and everything made from it, at the
end. Next: the [Python README](../../python/).

</details>

<details>
<summary><strong>TypeScript / JavaScript</strong></summary>

You need Node.js 20.10 or later. In Node the SDK uses a native addon; in a
browser it uses a WebAssembly build of the same C code.

```sh
cd sdk/typescript
npm install            # also builds the native addon
npm run build:ts
```

In your own project, install the SDK from the folder
(`npm install /path/to/sdk/typescript`), and use a `tsconfig.json` with
`"module": "NodeNext"`, `"target": "ES2022"` and `"lib": ["ES2022",
"ESNext.Disposable"]` (for `using`). Set `"type": "module"` in
`package.json`. Then save this as `ledger.ts`:

```ts
import { Zxv, Ledger, AccountKind, Rational } from '@zedec/zxv';

const zxv = await Zxv.load();                        // the native addon in Node, Wasm in a browser
{
  using ctx = zxv.createContext();                   // closed at the end of this block
  const root = ctx.rootCapability();
  ctx.enableCurrency(root, 'NGN');
  const ledger = Ledger.of(root);
  const bank = ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
  const ada = ledger.openAccount('ada', 'NGN');
  ledger.post(bank, ada, 15_000n, 'NGN', 'E2E-0001');  // 150.00 NGN, in kobo (a bigint)
  console.log('ada holds', ledger.balance(ada).equity, 'kobo');
}
console.log('0.1 + 0.2 =', Rational.parse('0.1').add('0.2').toString());
```

```sh
npx tsc && node ledger.js
```

Amounts are `bigint`, so Node prints the balance as `15000n`. A fractional
`number` is refused. Next: the [TypeScript README](../../typescript/).

</details>

<details>
<summary><strong>C</strong></summary>

C talks to `libzxv` directly. Every function returns a status code:
`ZXV_OK` (0) or a negative `ZXV_E_*` value. Save this as `ledger.c`:

```c
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "zxv_api.h"

/* Stop on any status other than ZXV_OK, with the library's reason. */
static void check(int32_t rc, const char *what) {
    if (rc == ZXV_OK) return;
    char why[256] = "";
    size_t n;
    zxv_last_error(why, sizeof why, &n);
    fprintf(stderr, "%s: %s (%s)\n", what, zxv_status_name(rc), why);
    exit(1);
}

int main(void) {
    zxv_ctx *ctx;
    zxv_cap *root;
    uint32_t bank, ada;
    uint64_t entry;
    int64_t equity, num, den;
    char text[32];
    size_t len;

    check(zxv_ctx_create(&ctx), "create context");
    check(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root), "root capability");
    check(zxv_ccy_enable_cap(root, "NGN"), "enable NGN");
    check(zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank), "open bank");
    check(zxv_ledger_open_account_cap(root, "ada", "NGN", ZXV_ACCT_HOLDER, &ada), "open ada");
    check(zxv_ledger_post_cap(root, bank, ada, 15000, "NGN", "E2E-0001", &entry), "post");
    check(zxv_ledger_balance_cap(root, ada, NULL, NULL, &equity), "balance");
    printf("ada holds %" PRId64 " kobo\n", equity);

    check(zxv_rat_add(1, 10, 2, 10, &num, &den), "0.1 + 0.2");
    check(zxv_rat_format(num, den, text, sizeof text, &len), "format");
    printf("0.1 + 0.2 = %s\n", text);

    zxv_cap_destroy(root);                                      /* children first */
    zxv_ctx_destroy(ctx);
    return 0;
}
```

```sh
cc -std=c11 ledger.c -Isdk/libzxv/include -Lsdk/libzxv/build -lzxv \
   -Wl,-rpath,$PWD/sdk/libzxv/build -o ledger && ./ledger
```

In C you destroy handles yourself, children before parents. Next:
[the C ABI](../ABI.md).

</details>

## What just happened

- **Exact money.** 150.00 NGN went in as the integer 15000. Nothing was
  rounded, because nothing was ever a float.
- **A capability on every call.** The root capability allowed everything.
  In real code you hand each part of your program a narrower one: see
  [Capabilities](capabilities.md).
- **Three balances.** `equity` is one of three numbers every account keeps.
  See [Ledger and currencies](ledger-and-currencies.md).
- **0.1 + 0.2 = 0.3.** In most languages this prints
  `0.30000000000000004`. ZXV's `Rational` keeps exact fractions. See
  [Exact numbers](exact-numbers.md).

## Other languages

Rust, Go, C++, Swift, Ruby and PHP have the same quickstart in their
READMEs: [Rust](../../rust/), [Go](../../go/), [C++](../../cpp/),
[Swift](../../swift/), [Ruby](../../ruby/), [PHP](../../php/). Solidity is
different: it verifies the state encoding libzxv produces
([Solidity](../../solidity/)).

## Next steps

Go back to the [guide catalogue](README.md) and follow the "start here"
path, or jump to [Errors and fail-closed](errors-and-fail-closed.md), the
guide most people need second.
