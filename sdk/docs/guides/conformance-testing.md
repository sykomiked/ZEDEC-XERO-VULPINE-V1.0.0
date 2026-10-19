<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Conformance testing: one answer key for every SDK

**Who this is for:** SDK maintainers, binding authors, and anyone who wants
evidence that two languages give the same answers.
**What you will be able to do:** run each SDK's tests, read the shared answer
key, and write your own conformance test.

## The answer key

[`sdk/conformance/vectors.json`](../../conformance/vectors.json) is written
by libzxv itself (the program `sdk/libzxv/tools/gen_vectors.c`). Every SDK
loads it in its tests and checks that it gives the same results. So when the
Python test and the Go test both pass, Python and Go agree, and both agree
with C.

| Key | What it checks |
|---|---|
| `abi` | the ABI version, `[1, 3, 0]` |
| `status` | every status code's name and class |
| `rational_ops`, `rational_parse`, `ieee754`, `q16` | exact arithmetic, parsing, float conversion, Q16.16 |
| `lpres` | the four-valued `and`, `or`, `not` tables |
| `hex` | all 64 hexagram tags: names, actions, steps, the quarantine-log hash |
| `ubh168`, `ubh168_idle` | wire frames, byte for byte |
| `hash` | Keccak-256, SHA3-256 and SHAKE256 |
| `ed25519`, `mldsa87`, `mlkem1024` | signatures and key agreement from fixed seeds |
| `swarm`, `evm_state` | the token economy and the EVM state encoding |
| `generate` | the inference engine's tokens on a tiny test model |
| `phase_cycle` | 27720 |

The engine vectors use a tiny model in `sdk/conformance/fixtures/`, written by
`make -C sdk/libzxv fixtures`. It is a test fixture only, not a useful model.

To regenerate the key after a deliberate change in libzxv:

```sh
make -C sdk/libzxv vectors
```

Then review the diff carefully. A changed vector means every SDK's behaviour
changed.

## Running each SDK's tests

Build the library and fixtures first: `make -C sdk/libzxv all fixtures`.

| SDK | From | Command |
|---|---|---|
| C | repository root | `make -C sdk/libzxv test` |
| Java | `sdk/java` | `mvn test` |
| .NET | `sdk/dotnet` | `dotnet build Zxv.sln -c Release -p:ZxvSkipNativeBuild=true && dotnet test Zxv.sln -c Release --no-build` |
| Python | `sdk/python` | `pip install -e '.[test]' && pytest` |
| TypeScript | `sdk/typescript` | `npm test` (both the native and the Wasm backend) |
| Rust | `sdk/rust` | `cargo test` |
| Go | `sdk/go` | `go test -race ./...` |
| C++ | repository root | `cmake -S sdk/cpp -B build && cmake --build build && ctest --test-dir build --output-on-failure` |
| Swift | `sdk/swift` | `swift test` |
| Ruby | `sdk/ruby` | `rake test` |
| PHP | `sdk/php` | `php tests/run.php` |
| Solidity | `sdk/solidity` | `npm test` |

Each language README has the details. CI runs all of them on Linux and macOS
([`sdk.yml`](../../../.github/workflows/sdk.yml)).

## Writing your own conformance test

Here is a small pytest file that checks the Python SDK against the key. It
passed 40 tests in our run. Use it as a template for any language.

```python
import json
import os
from pathlib import Path

import pytest
import zxv

VECTORS = json.loads(Path(os.environ["ZXV_VECTORS"]).read_text())


def test_abi_matches_the_answer_key():
    assert list(zxv.abi_version()) == VECTORS["abi"]


@pytest.mark.parametrize("row", VECTORS["status"])
def test_status_names_and_classes(row):
    assert zxv.status_name(row["code"]) == row["name"]
    assert int(zxv.status_class(row["code"])) == row["class"]


@pytest.mark.parametrize("row", VECTORS["rational_ops"])
def test_rational_ops(row):
    a, b = zxv.Rational.of(*row["a"]), zxv.Rational.of(*row["b"])
    assert a + b == zxv.Rational.of(*row["add"])
    assert a * b == zxv.Rational.of(*row["mul"])


def test_four_valued_tables():
    t = list(zxv.Truth)  # NEITHER, TRUE, FALSE, BOTH: the kernel's numbering
    for i, a in enumerate(t):
        assert (~a).value == VECTORS["lpres"]["not"][i]
        for j, b in enumerate(t):
            assert (a & b).value == VECTORS["lpres"]["and"][i][j]
            assert (a | b).value == VECTORS["lpres"]["or"][i][j]


@pytest.mark.parametrize("row", VECTORS["ubh168"])
def test_wire_frames_byte_for_byte(row):
    f = zxv.Ubh168Frame.build(row["trunk"], row["shell"], row["sync"],
                              tuple(row["s_plus"]), tuple(row["s_minus"]))
    assert f.to_bytes().hex() == row["frame"]
    assert int(f.resolve()) == row["truth"]
```

```sh
cd sdk/python && . .venv/bin/activate
ZXV_LIBRARY_PATH=$PWD/../libzxv/build ZXV_VECTORS=$PWD/../conformance/vectors.json \
  pytest -q test_my_conformance.py
# 40 passed
```

Tips:

- Read the vectors at test time. Never copy values into your test by hand;
  the point is that a change in libzxv shows up everywhere at once.
- Test pure host-language code (such as wire framing) against the vectors
  **and** against the C library, as the SDKs do.
- The `and` and `or` tables include two cells still waiting on an owner
  decision (`BOTH` with `NEITHER`; see
  [Four-valued logic](four-valued-logic.md#four-values-instead-of-two)).
  Reading them from the vectors keeps your test right if they change.

## Next

- [Writing a binding](writing-a-binding.md)
- [Troubleshooting](troubleshooting.md)
