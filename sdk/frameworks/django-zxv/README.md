<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# django-zxv

Django integration over the [ZXV Python SDK](../../python/) (`zxv`). Add
`"django_zxv"` to `INSTALLED_APPS` and your project gets:

- **One context per process, owned by the app config.** `ZxvConfig.ready()`
  validates `settings.ZXV`, loads `libzxv` and creates the context, so a bad
  setting or a missing library stops start-up instead of failing the first
  request. `django_zxv.shutdown()`, which is also registered with `atexit`,
  closes it in the destroy order of [ABI.md R6](../../docs/ABI.md#r6-threading):
  named capabilities (newest first), then the root, then the context.
- **Named capabilities; the root is never handed out.**
  `django_zxv.get_capability("teller")` returns a capability attenuated from
  the root as declared in `ZXV["CAPABILITIES"]`. The root lives in a
  name-mangled attribute and has no name. `"root"` is a reserved name.
- **`RationalField`**: an exact rational in one column (see
  [Storage](#storage-why-canonical-text)). It also provides `RationalFormField`.
  Both refuse floats.
- **Exact JSON without DRF**: `read_exact_json` / `loads_exact` refuse every
  JSON float (`0.1`, `1.0`, `1e3`, `NaN`) before it becomes a binary float.
  `rational_from_json` and `minor_units_from_json` read fields, and
  `ExactJsonResponse` writes `Rational`, `Fraction` and `Decimal` as exact text
  and refuses a float in a response.
- **Problem details without native text.** `ZxvProblemMiddleware` answers ZXV
  errors and exact-JSON errors as RFC 9457 `application/problem+json`. The
  body carries a generic title, the status code, the `ZXV_E_*` name and the
  class, but never the SDK's detail text, which can contain account
  references. The detail goes to the `django_zxv` logger instead. A halted
  context (`ZxvFatal`) is always answered 503 and never as a normal response.
- **A fail-closed health view.** `django_zxv.views.health` returns 200
  `{"status":"up"}` only when libzxv reports the context live. It returns 503
  `{"status":"down","state":"halted"|"closed"|"unknown"}` otherwise,
  including when anything goes wrong while asking.
- **A read-only accounting-equation admin page.** `LedgerEquationAdmin` adds
  `<changelist>/accounting-equation/`. Per currency, it reports whether
  Assets = Liabilities + Equity holds, as TRUE, FALSE or NEITHER (NEITHER
  when some lines are unclassified). The totals are computed exactly.
- **A system check** (`django_zxv.E001`) that reports an invalid `ZXV`
  setting.

Requirements: Python 3.12+, Django 5.2+, and `libzxv` plus the Python SDK
built from this repository. The tests run on Django 6.1.

## Quickstart

The kit uses its own virtual environment in `sdk/frameworks/django-zxv/.venv`.
That directory is git-ignored, as are `db.sqlite3`, `__pycache__` and
`.mypy_cache`.

```sh
make -C sdk/libzxv all                 # sdk/libzxv/build/libzxv.so
cd sdk/frameworks/django-zxv
python3 -m venv .venv
.venv/bin/pip install -e ../../python -e . -r requirements-dev.txt

.venv/bin/python runtests.py           # Django's test runner, 56 tests
.venv/bin/mypy                         # --strict, with the django-stubs plugin

# the sample project (SQLite, DEBUG, local only)
.venv/bin/python sample/manage.py migrate
.venv/bin/python sample/manage.py runserver 127.0.0.1:8000
```

```sh
curl -s 'localhost:8000/sum?a=0.1&b=0.2'                    # {"sum": "0.3"}
curl -s -H 'content-type: application/json' \
     -d '{"price":"12.75","quantity":"1/3"}' localhost:8000/quote      # {"total": "4.25"}
curl -s -H 'content-type: application/json' \
     -d '{"price":12.75,"quantity":"1/3"}' localhost:8000/quote        # 400 urn:zxv:problem:json_float
curl -s -H 'content-type: application/json' \
     -d '{"amountMinor":"150000","reference":"inv-1"}' localhost:8000/payments   # 201 {"entryId": "1"}
curl -s -H 'content-type: application/json' \
     -d '{"amountMinor":"100","reference":"r-1"}' localhost:8000/refunds         # 403 ZXV_E_CAP (auditor)
curl -s localhost:8000/balance                              # {"debit": "150000", ...}
curl -s localhost:8000/health/                              # 200 up, per-capability rights
```

`ZXV_SAMPLE_HALT=1` enables `POST /admin-halt`, which halts the context.
After that, `/health/` answers 503 and every ZXV call answers 503. The
sample's JSON endpoints are `csrf_exempt` because they are unauthenticated
local demo endpoints. A real API puts its own authentication in front of
them. Create a superuser (`manage.py createsuperuser`) to see the
accounting-equation page under *Ledger lines* in the admin.

## In your project

```python
# settings.py
INSTALLED_APPS = [..., "django_zxv"]
MIDDLEWARE = ["django_zxv.problems.ZxvProblemMiddleware", ...]   # first

ZXV = {
    "CURRENCIES": ["NGN"],                 # enabled at start-up
    "PRIVATE_UNITS": {"VFV": 2},           # code -> minor units (0..8)
    "ROOT_RIGHTS": ["LEDGER_READ", "LEDGER_POST"],    # default: all
    "CAPABILITIES": {
        "teller": ["LEDGER_READ", "LEDGER_POST"],
        "auditor": "LEDGER_READ",          # a list, or "A,B" / "A|B"
    },
    "FATAL_POLICY": "raise",               # or "abort"
    "LIBRARY_PATH": None,                  # else ZXV_LIBRARY_PATH, the bundled copy, the system loader
}

# urls.py
from django_zxv.views import health
urlpatterns = [path("health/", health), ...]
```

```python
import zxv, django_zxv
from django_zxv.exactjson import ExactJsonResponse, rational_from_json, read_exact_json

ledger = zxv.Ledger(django_zxv.get_capability("teller"))

def quote(request):
    body = read_exact_json(request)                   # a JSON float -> 400 problem
    total = rational_from_json(body.get("price"), "price") * rational_from_json(body.get("quantity"), "quantity")
    return ExactJsonResponse({"total": total})        # {"total": "4.25"}
```

```python
from django_zxv.fields import RationalField
from django_zxv.admin import LedgerEquationAdmin

class LedgerLine(models.Model):
    kind = models.CharField(max_length=16)           # "asset" | "liability" | "equity"
    currency = models.CharField(max_length=8)
    amount = RationalField()

@admin.register(LedgerLine)
class LedgerLineAdmin(LedgerEquationAdmin[LedgerLine]):
    equation_kind_field = "kind"
    equation_amount_field = "amount"
    equation_currency_field = "currency"             # or None for one total
```

Settings are validated as a whole. Unknown keys, unknown rights, a capability
asking for rights the root does not hold, bad currency codes and numbers
where rights are expected are all reported together, as
`ImproperlyConfigured` at start-up and as `django_zxv.E001` from `manage.py
check`.

### HTTP mapping

| ZXV status | HTTP |
|---|---|
| `ZXV_E_NOT_FOUND` | 404 |
| `ZXV_E_CAP` | 403 |
| `ZXV_E_DUPLICATE` | 409 |
| `ZXV_E_FUNDS`, `ZXV_E_CURRENCY`, `ZXV_E_DECLINED`, `ZXV_E_VERIFY` | 422 |
| `ZXV_E_BUDGET` | 429 |
| `ZXV_E_NOTIMPL` | 501 |
| `ZXV_E_CAPACITY`, `ZXV_E_NOMEM`, any fatal (halted context) | 503 |
| other usage / refused / system errors | 400 / 409 / 500 |
| exact-JSON errors (`json_float`, `json_syntax`, `json_type`, ...) | 400 (413 for `body_too_large`, 415 for `json_type`) |

## Storage: why canonical text

`RationalField` stores the reduced value as `"<numerator>/<denominator>"`.
The denominator is always positive and the slash is always present, so
`12.75` is stored as `"51/4"` and `3` as `"3/1"`. The column is a
`varchar(41)`, which is long enough for any pair of int64 parts.

- A **decimal** column cannot hold `1/3`, and some databases round it. A
  **float** column is out of the question.
- **Two integer columns** would make one model field own two database
  columns. Django has no public API for that: forms, `values()`,
  migrations, serialization and the admin all assume one column per field.
- Because the text is **canonical**, equal values always have equal text on
  every backend. That makes `exact`, `in`, `isnull`, `unique` and indexes
  exact.

The cost is that the database cannot order or add these values. Lookups such
as `lt`, `gt`, `range` and `contains` raise `FieldError` instead of silently
comparing strings. Sort and sum in Python with `zxv.Rational` or `Fraction`,
as the accounting-equation page does. Do not `order_by` a `RationalField`:
Django cannot stop it, and the order would be lexicographic. The field
accepts `Rational`, `int`, `Fraction`, `Decimal` and exact text (`"12.75"`,
`"-0.1"`, `"1/3"`). It refuses a `float` with `ValidationError` (code
`float`) and anything whose parts do not fit 64 bits.

## Accounting-equation page

The page needs the model's view permission and answers GET only. It runs one
`SELECT` over `get_queryset(request)`, so the admin's own row restrictions
apply. Per currency, it shows the asset, liability and equity totals, the
difference, and a verdict:

- TRUE when A = L + E exactly;
- FALSE when it does not hold;
- NEITHER when lines of another kind leave the answer unknown.

Totals are Python `Fraction`s (exact and unbounded), so a sum beyond int64
is still exact. It is **advisory**: it reads and reports, and it never
blocks a save or writes anything. The function behind it,
`django_zxv.ledger.accounting_equation(rows)`, can be used on its own.

## Limits

- **One context per process.** Under a pre-forking server, create the
  context after the fork (for example, do not use gunicorn `--preload`). A
  native context should not be inherited across `fork()`.
- **`RationalField` values are int64 parts.** Values whose parts do not fit
  are refused. Totals are not stored, so they are unbounded.
- **No async-specific API.** The SDK calls are synchronous and short. Call
  them from async views with `sync_to_async` if you need to.
- The accounting-equation page checks the **rows in your Django model**,
  not the libzxv ledger. The libzxv ledger enforces its own invariants
  natively (`zxv.Ledger.check()`).
- This kit has not been certified or audited by anyone. Use it as an
  integration example and test it in your own setting.

## Tests

`python runtests.py` uses Django's test runner against the real `libzxv`.
It runs 56 tests:

| Module | Tests |
|---|---|
| `test_conf` | 6 |
| `test_runtime` | 5 |
| `test_fields` | 11 |
| `test_exactjson` | 9 |
| `test_ledger` | 13 |
| `test_http` | 12 |

`mypy` (strict, django-stubs plugin) checks `src/`, `sample/` (except its
migrations) and `tests/`. CI runs both on Ubuntu 24.04 and macOS 14:
[`.github/workflows/sdk-frameworks.yml`](../../../.github/workflows/sdk-frameworks.yml).
