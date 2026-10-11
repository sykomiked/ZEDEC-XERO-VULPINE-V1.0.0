<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# sdk/release

Helpers for the [`sdk-release`](../../.github/workflows/sdk-release.yml)
workflow, which packages every SDK into a **draft** GitHub release. The
human checklist (version bumps, ABI baseline, review, publishing, registry
uploads) is [`sdk/RELEASING.md`](../RELEASING.md).

| File | What it does |
|---|---|
| [`check_versions.py`](check_versions.py) | Reads every SDK manifest and checks it declares the release version; also checks that the ABI baseline `sdk/libzxv/abi/libzxv-X.Y.Z.json` exists. Standard library only. |
| [`render_notes.py`](render_notes.py) | Fills [`NOTES_TEMPLATE.md`](NOTES_TEMPLATE.md) with the version, commit and the list of attached files. |
| [`NOTES_TEMPLATE.md`](NOTES_TEMPLATE.md) | The release notes: what is attached, honest limits, untested platforms. Edit it here, not in the workflow. |

## check_versions.py

```sh
python3 sdk/release/check_versions.py                  # version from zxv_api.h
python3 sdk/release/check_versions.py --version 1.3.0
python3 sdk/release/check_versions.py --version 1.3.0 --strict
```

Exit status 0 means every check passed, 1 means something disagrees (each
problem is listed with its file), 2 is a usage error. The policy table at
the top of the script sorts each manifest into one of four groups:

| Group | Rule |
|---|---|
| lockstep | Must equal the release version: zxv_api.h, Python, Java, .NET, TypeScript, Rust, Ruby, C++, Kotlin, Dart. |
| pin | A dependency on a lockstep package must accept the release version: Rust `zxv` → `zxv-sys`, Spring `zxv.version`, NestJS peer range, django-zxv requirement. |
| independent | Framework, engine and plugin kits on their own version line. Reported; enforced only with `--strict`. |
| tag | No version field (Go, SwiftPM, Composer, the Android AAR). Reported for information. |

Pre-release versions are compared in each ecosystem's own spelling:
`1.3.0-rc.1` matches `1.3.0rc1` in Python and `1.3.0.pre.rc.1` in Ruby, and
the C header and CMake compare only `1.3.0`.

To move a package between groups, edit its row in `POLICY`.

## render_notes.py

```sh
python3 sdk/release/render_notes.py --version 1.3.0 --tag sdk-v1.3.0 \
    --commit "$(git rev-parse HEAD)" --dist dist --prerelease false --out notes.md
```

It fails if the template contains a `{{PLACEHOLDER}}` it does not know.
