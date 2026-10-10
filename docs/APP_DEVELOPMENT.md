<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Building apps for the platform

This guide covers how to write an app today, which pieces are real and tested, and what is still missing from the SDK. It is based on what the repository contains now, not on what the SDK README promises.

## Pick a path

| You want to… | Use | State |
|---|---|---|
| Write a native app inside the OS (editor, tool, game, assistant skill) | C11 in `kernel/src/apps`, with `kernel/src/appkit`, `speech`, `xlate`, `i18n` and the module headers | Works. This is how the bundled apps are built. |
| Write against the documented SDK facade `m5_api.h` | `kernel/src/sdk` | **Declarations only.** See the gaps below. |
| Write a .NET service or desktop app | `bindings/dotnet` (`Zxv.Sdk` over the C ABI `native/zxv_api.h`) | In progress. Covers payments, ledger and currencies. |
| Call the core from COBOL, Fortran, Pascal, Ada, PL/I or RPG | `bindings/` (`zx_legacy_api.h`, `build_lib.sh`) | In progress. The C, COBOL, Fortran, Pascal and Ada examples build and run. |

## 1. A native app, step by step

### 1.1 The rules every module follows

These rules come from `SDK_README.md` "Best Practices" and the kernel's own conventions:
- Freestanding C11: no libc in module code, no `malloc`, no floating point (use `m5_rat_t` / Q-format integers), and no 64-bit division (use `zt_udiv64`).
- Bounded buffers that the caller owns.
- **Fail closed.** An unbound hook is a typed error, never a guess.
- Every file starts with the two-line copyright and SPDX header. Today that is `SPDX-License-Identifier: Apache-2.0`.
- Formatting: `clang-format` 18 with the repo's `.clang-format`. CI checks the files you touch.

### 1.2 Start from the template

```sh
cp kernel/src/sdk/app_template.c kernel/src/apps/myapp.c
```

The template shows the shape every app uses:
- **State** in one static struct, with `m5_app_base_t` first.
- **`app_init`**, which draws the window with `m5_gui_*`.
- **A key handler, a tick and a render**, wired to the launcher.
- **A main loop** that yields with `m5_syscall(M5_SYS_YIELD, …)`.

Real examples of the same shape are in `kernel/src/apps/`: `notes_app.c` (it has a test, `test_notes.c`), `wallet_app.c`, `clock_app.c` and `treaty_app.c`. Register a new app in `kernel/src/apps/apps.c` and `apps.h`.

### 1.3 Documents and editing: AppKit

`kernel/src/appkit/doc.h` gives every document-style app (Writer, Sheet, Deck, …) the same core:
- a gap buffer
- coalesced undo and redo (64 steps)
- search and line indexing
- a ZXVFS-backed load and save path

```c
static doc_t d;
doc_init(&d, "letter.txt");
doc_insert(&d, 0, "Dear Ana,", 9);
doc_undo(&d);
```

Run its tests from `kernel/` the way the Makefile does:

```sh
gcc -std=c11 -Wall -Werror -Wextra -Isrc/appkit src/appkit/test_doc.c src/appkit/doc.c
```

### 1.4 Voice, conversation and dictation in your app

Use `kernel/src/speech/speech_session.h` (details in `docs/SPEECH_AND_TRANSLATION.md`). An app can add a microphone button in about 20 lines:

```c
static int16_t cap[16000 * 30], tts[16000 * 20];
static speech_session_t s;

static int my_reply(const char *said, const char *lang, char *out, uint32_t cap_, void *ctx)
{
    /* Ask the model (zt_model_generate) or run your app's command. */
    return my_assistant_answer(said, lang, out, cap_);
}

speech_config_t cfg;
speech_config_defaults(&cfg);
cfg.use = SPEECH_USE_ASSISTANT;   /* or SPEECH_USE_DICTATION into a doc_t */
cfg.input_rate = 48000;
cfg.reply = my_reply;
cfg.on_event = my_events;         /* TRANSCRIPT, REPLY, SPOKEN, ERROR ... */
cfg.audio_out = my_speaker;       /* wraps audio_write() */
speech_session_init(&s, &cfg, cap, sizeof cap / 2, tts, sizeof tts / 2);
speech_session_bind_stt(&s, &platform_stt);   /* e.g. zx_speech_mac_stt_ops() */
speech_session_bind_tts(&s, &platform_tts);
/* button down / up: */
speech_session_ptt_down(&s);  /* feed mic samples with speech_session_feed() */
speech_session_ptt_up(&s);
```

- **Dictation into a document:** on a `SPEECH_EV_TRANSCRIPT` event, call `doc_insert(&d, caret, ev->text, len)`.
- **Read-aloud:** `speech_session_say(&s, text, lang)`.
- **Typed chat** in the same conversation: `speech_session_submit_text`.

### 1.5 Every language

There are two layers:
- **Interface text** comes from `kernel/src/i18n` (778 CLDR locales). Keep message ids in your state and render them at the edge with `i18n_msg()`. This is the Enochian-core rule: the core stores keys, the edge shows text.
- **User content** (posts, messages, documents others wrote) goes through `xlate_view()` with the reader's `xlate_prefs_t`. Show `xlate_marker_label()` whenever `XLATE_F_MACHINE` is set, and offer "Show original" with `xlate_prefs_toggle_original()`.

### 1.6 Test on the host, then cross-compile

Host test, from `kernel/` with the Makefile's `CPATH`:

```sh
gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/apps src/apps/test_myapp.c src/apps/myapp.c ... -o /tmp/t
```

Check that the module stays freestanding:

```sh
clang --target=aarch64-none-elf -ffreestanding -std=c11 -O2 -c src/apps/myapp.c -o /tmp/myapp.o
nm -u /tmp/myapp.o   # only other kernel symbols (and memcpy/memset, from freestanding.c)
```

Then add your test line to `verify-all` in `kernel/Makefile`, and build the image:

```sh
make -C kernel -f build_system/Makefile.arm64 all
```

### 1.7 The hosted desktop build

`build_system/build_desktop.sh` builds the hosted swarm (`kernel/arch/hosted/zxv_host.c`) for macOS, Windows and Linux with zig. That build carries no Apple frameworks. To ship `zx_speech_mac.m`, compile it on a Mac with clang and link `-framework Speech -framework AVFoundation -framework Foundation`. The app bundle also needs a usage string in `Info.plist`.

## 2. The .NET SDK (`bindings/dotnet`, in progress)

- `native/zxv_api.h` is a stable C ABI with opaque handles, scalars only, status codes, caller-owned buffers with a two-call length pattern, UTF-8, and versioning (rules R1-R8 in the header).
- `src/Zxv.Native` holds the P/Invoke layer and `src/Zxv.Sdk` the idiomatic API: Money, Currencies, Ledger, Messaging, Cards, Conformance and DI extensions.
- Samples are in `samples/Zxv.Samples.Console` and `samples/Zxv.Gateway`; tests are in `tests/`.
- The ABI covers currencies (ISO 4217), ledgers and messaging today. Speech, translation, i18n and the assistant are not exposed yet. See the gaps below.

## 3. The legacy bindings (`bindings/`, in progress)

- One append-only ABI, `zx_legacy_api.h` with `zx_legacy_abi_version() == 1`, wraps `kernel/src/legacy` (COMP-3 packed decimal, IBM HFP floats, record layouts).
- `./build_lib.sh` builds `libzxlegacy`, runs the C self-test, and builds and runs every language example whose compiler is installed: GnuCOBOL, gfortran, Free Pascal and GNAT were used to build these.
- The PL/I and RPG files are illustrative sketches only.

## 4. Gaps found in the SDK

1. **`m5_api.h` is declarations without definitions.** Of its 85 functions (`m5_rat_*`, `m5_ledger_*`, `m5_vfs_*`, `m5_ipc_*`, `m5_vena_*`, `m5_pc_*`, …), none is defined anywhere in `kernel/src` outside the SDK. A small number exist only as local helpers inside individual `kernel/src/apps` files. An app written purely against the SDK compiles but does not link. A shim from `m5_*` to the real modules (`rational`, `oseq`, `lpres`, `phase_coord`, `finance`, `zxvfs`, `syscall`) is the most important missing piece.
2. **`m5_api.h` breaks its own rule.** `m5_phase_t` and `m5_iphase_*` use `double`, against "No Floating Point".
3. **`app_template.c` does not build with the SDK's own flags.** Under `-Wall -Wextra -Werror` it fails: `app_handle_key`, `app_handle_special` and `app_render` are defined but never called. Nothing reads input or calls them from the main loop, and there is no input syscall in the template.
4. **`Makefile.app` paths.** It hard-codes `KERNEL_ROOT := ../../../..`, which is wrong when run as `make -f kernel/src/sdk/Makefile.app` from the repository root. `install` and `run` only print instructions, and `test` only compiles; it does not run a test.
5. **There is no app manifest or packaging step** (name, version, capabilities, signature). Apps are linked into the kernel by editing `apps.c`. `kernel/src/zxpkg` exists, but the SDK does not use it.
6. **There is no capability or permission model in the SDK** for microphone, network, ledger or files. The syscall layer has per-call capabilities, but `m5_api.h` does not expose them.
7. **Speech, translation, i18n and the assistant are not in `m5_api.h` or `zxv_api.h`.** Apps reach them only by including kernel headers directly.
8. **`SDK_README.md` is stale.**
   - Its licence section lists OPL, CC BY-SA, the Royal Writ and SEL, but the code is now Apache-2.0.
   - It claims "32 languages", but i18n now covers 778 CLDR locales.
   - Its "Support" section points at raw IP model endpoints, which an SDK should not ship.
   - Its Best Practices list numbers two items as 3.
9. **There is no event or input API beyond key characters:** no pointer, touch, audio-device or timer subscription in the SDK.

## 5. Checklist for a new app

- [ ] The header has the two copyright and SPDX lines; the code is freestanding C11 and clang-format clean.
- [ ] Its state is static and bounded; every hook fails closed.
- [ ] It has a host test, and its line in `verify-all`.
- [ ] It passes the `aarch64-none-elf` cross-compile and the `nm -u` check.
- [ ] Interface text comes from i18n message ids, and user content goes through `xlate_view` with a visible machine-translation marker.
- [ ] Voice uses `speech_session`, with nothing fabricated when no engine is bound.
