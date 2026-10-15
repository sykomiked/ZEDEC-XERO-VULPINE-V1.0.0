# App wave spec (for the server's coding model)

Read this whole file before any task. Each task builds one built-in app of the ZXV OS. Work on a branch of `sykomiked/ZEDEC-XERO-VULPINE-V1.0.0` cut from `claude/project-thread-cqt3z5-wave0` (its latest head), named `server/waveN-<slot>-<id>` for the task's wave N. Never push to `main` or to that branch. Never open a PR. Commit locally and hand the branch back for review.

## What an app is
1. **Model** `kernel/src/apps/<id>_model.{h,c}`: C11, the zxm.h pattern. Copy the shape of `almanac_model.{h,c}` (read-only facts) or `keepsake_model.{h,c}` (actions). No floats, no allocation, every string bounded, zxvd reached only through `zxm_link_t`.
2. **Manifest** `kernel/src/apps/<id>.zxapp`: copy `almanac.zxapp`. It must have `slot`, `layer L1`, `icon icons/l1/NNN-<id>.svg`, `budget`, one `needs` per capability (only names from `zxapp_caps`, kernel/src/apps/zxapp.c), `model`. **Every L1 app is a block too** (zxapp.h C1): give its manifest `version 1.0.0` and a `provides` line for every entry, so L2 apps can `use <id> 1` it. An L2 app is published the same way for L3. Icons: `icons/l1/`, `icons/l2/` or `icons/l3/` + `NNN-<id>.svg`. An app holds up to 128 logic lines (ZXAPP_LOGIC_MAX). L2 tasks name an L1 part's entries by meaning: read that part's merged manifest for the exact `provides` names. Budget is 233 unless the task says otherwise. An L2 app re-exports every entry it composes from its parts (`provides <part>.<entry> ...`, as murmuration.zxapp does), so L3 apps built from it can call them.
3. **Logic blocks** `kernel/src/apps/blocks/<id>/`: every number, rule or record the app decides goes in the language `kernel/src/apps/LANGUAGE_FIT.tsv` says owns that kind of work. Only languages whose runtime column is not `-`.
   - money, counts that must refuse on overflow: `sutra` (COBOL), see blocks/fee_check/fee.sutra
   - arrays, shares, sums over vectors: `kron` (APL/K), see blocks/banker/share.kron
   - limits, safety, policy: `eve` (Ada), see blocks/shepherd/limit.eve
   - exact numeric sums: `trebago` (FORTRAN), see blocks/shepherd/week.tb
   - records and indexes: `myra` (MUMPS), see blocks/banker/entry.myra
   - small pure functions: C++ or Rust compiled to wasm, see blocks/sounding/rate.rs; build with `tools/apps/build_wasm_blocks.sh`, commit the .wasm too
   Every decision that changes what the user sees or what is allowed is **critical**: write it in two languages (two `logic` lines for one `provides`), so the witness can hold it when they disagree. Never add a language just to add it. If no runnable language fits a feature, keep it in C and say so in the manifest comment.
   **Legacy languages and their AI twins.** Every app pairs legacy languages with their twins. The five pillars already do: COBOL runs as Sutra, APL/K as KRON, MUMPS as MYRA, Ada as Eve, FORTRAN as Trebago. The Lingua languages are written in the twin's canonical spelling: FORTRAN 77 as Reckonwyrm, BASIC as Hatchling, Forth as Cairndrake. Write the `logic` line with the twin's name (`logic reckonwyrm ...`, `logic hatchling ...`, `logic cairndrake ...`), and put the legacy name in the comment above it (`# FORTRAN 77, twin Reckonwyrm`). Run `zxlang twin <file>` and commit its output. test_zxapp.sh fails on any .f/.bas/.fs block that is not in twin form. Use more twins as Lingua adds them (kernel/src/lingua/catalogue.tsv, twin column).
   **No language left out.** Each task ends with a "Planned (no runtime yet)" line. For each language on it, find the entry it is the best fit for: the one whose kind of work matches that language's `owns` column in LANGUAGE_FIT.tsv. Write a runnable stand-in for that entry as usual, and add `pending <Language_Name> <entry>` to the manifest, with spaces written as _. `python3 tools/apps/lang_coverage.py --check` refuses a pending line for a language that already runs, or for an entry with no logic. If a planned language has no matching entry in the app, list it in your report and don't invent a feature for it. When Lingua ships a front-end, `lang_coverage.py --retrofit <language>` produces a task for each waiting app.
4. **Tests**: add a section to `kernel/src/apps/test_zxapp.c` following `test_wave0()` (`wave_app`, `wave_same`, `wave_lp`): 64 swept rows per witnessed entry checked against the C model's own function, every edge (0, max, max+1, negative), and one input that makes the languages disagree and must give BOTH. Model tests go in `kernel/src/apps/test_apps.c` against the real zxvd dispatcher.

## One owner for every shared idea
Before writing an app, read `docs/APP_BLOCKS.md`. It is generated from `kernel/src/apps/CONCEPTS.tsv` and every merged manifest, and lists each shared concept with its one owner, plus every app and block with what it provides. If your app needs an idea listed there (a share of a capacity, days from seconds, a gcd, a swarm share...), `use` its owner and call `<owner>.<entry>`. Never write it again under any name. If your app defines a new idea another app will plausibly need, add a row to CONCEPTS.tsv naming your app as the owner, with the names that mean it and an exact meaning (formula, rounding, refusals). Then run `python3 tools/apps/concepts.py`. `concepts.py --check` (run by test_zxapp.sh) fails when a manifest provides a name a concept owns, or when two manifests provide the same name that is neither a concept nor a listed local word. Fix it by using the owner or renaming your entry, never by adding the name to the local list to get past the check. Your report lists every concept you used and every one you added.

## Build in matrix order
The L1 apps are the matrix's layers 1 to 144 (layer = slot; layer 0 is the core). `MATRIX_ORDER.tsv` lists the unbuilt ones in build order, measured from which layer's code already uses which (the matrix glue, `alexandria/LAYER_ORDER.tsv` in the project files), and waves 1 to 11 follow it: an app comes after every layer its code leans on, and the most leaned-on come first. Work the waves in number order, and inside a wave in the file's order. Each task's `Matrix:` line names the layers this app leans on and says which are built. Take every idea those layers own from them: `use <id> 1` and call `<id>.<entry>`. When the layer that should own an idea is not built yet, do not write the idea here; list it in your report as missing from that layer. A new idea that more than one app will need goes in the lowest layer that needs it (the earliest in `MATRIX_ORDER.tsv`), registered in CONCEPTS.tsv with that app as owner. An idea every layer needs (a share, a day count, a checksum) is a shared block under `blocks/` (a `.zxblock`), owned there. `python3 tools/apps/concepts.py` prints each app's reuse line (`reuse <id>: used U, new N, P%`: blocks used against new entries written); put yours in your report. A later app with a low figure has to say why nothing earlier fitted.

## Language gotchas (learned in wave 0)
- KRON evaluates right to left; `_` floor, `%` divide, `|` max, `&` min. A 1-element argument does not stretch to a vector: write `+/x` to make it a scalar. `floor` is a reserved name. Truths cannot be summed: count with `+/0|1&1+a-b`.
- Eve: no `integer` type; declare a range type (`cap = -1 .. 1440`) and use it.
- MYRA: integers at most 18 digits (`i18`).
- Sutra: binary minus needs spaces (`a - 1`); `answer` ends the row; a value that does not fit its picture is refused, which is the point.
- Wasm: export only `run`, integers only; build_wasm_blocks.sh sets the memory maximum Z-Wasm requires; test that it loads.

## Rules
- **INTEGERS ONLY.** Never write `float`, `double`, `f32`, `f64`, `sqrt` or `fabs` in a model, block or test helper. A fraction is two integers, or `kernel/src/rational`. A percentage is hundredths of a percent (x10000), rounded down by integer division. A square root, a mean or a ratio is computed in integers, or left out. test_zxapp.sh fails on any floating point in an app's model or blocks.
- Format with the repo's .clang-format. If your clang-format is older than 18, format only the lines you wrote; the reviewer re-formats with 18.1.8.
- No floats anywhere in kernel code. No other companies' app names in user-visible text. "the Banker" (files), "the Hoard" (storage root).
- Every new file starts with the two-line Apache-2.0 header (see any file).
- `clang-format` (repo's .clang-format) on every changed C file.
- Never copy anything from build_system/keys/.

## The gate (tools/apps/app_gate.py, run by test_zxapp.sh)
An app passes only when it is really built and tested, not only present:
- every `logic` file exists, and every file in `blocks/<id>/` is used by a `logic` line. A .wasm counts only when its .cpp or .rs source is beside it and `tools/apps/build_wasm_blocks.sh` built it.
- `test_zxapp.c` loads the manifest: add `{"apps/<id>.zxapp", "<id>", <entries>, <slot>}` to the apps table in `test_wave0()`. Then add the 64-row sweeps that check every entry against the model's own C function, plus the edge cases and the input that must give BOTH.
- `test_apps.sh` compiles the model, and `test_apps.c` includes its header and tests it against the real zxvd dispatcher.
- the model calls only daemon methods that `api/zxvd.openrpc.json` lists. Never invent one (`sys.settings`, `term.history`, `<app>.<anything>`). A missing method goes in your report.
Run `bash kernel/src/apps/test_zxapp.sh` after every change, read its errors, and fix them until it ends with `[PASS]`. Never hand back a branch that has not passed it.

## Done means all of these pass, in this order
python3 tools/apps/lang_coverage.py && python3 tools/apps/concepts.py
                                        (regenerate first: every new app changes docs/APP_LANGUAGE_COVERAGE.md
                                         and docs/APP_BLOCKS.md; commit both with the app)
bash kernel/src/apps/test_zxapp.sh      (ends [PASS]; its --check steps fail on a stale doc)
bash kernel/src/apps/test_apps.sh
tools/apps/build_wasm_blocks.sh --check
python3 build_system/relicense_apache.py --check
Report back: the branch name, the commit, each test's last line, the app's reuse line, every idea you listed as missing from an earlier layer, and anything you could not do.
