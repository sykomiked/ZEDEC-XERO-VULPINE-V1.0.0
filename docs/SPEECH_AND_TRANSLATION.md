<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Speech and translation

People talk to the system, dictate, listen to text read aloud and read every post, message and caption in their own language. This document explains how each of those works, what runs on the device and what does not, and which engines and model licences are involved.

| Piece | Where | State |
|---|---|---|
| Audio front end: resampler, VAD, pre-emphasis, framing, FFT, Whisper log-mel | `kernel/src/speech/speech_dsp.[ch]` | Tested (`test_speech`) |
| Speech session: push-to-talk, hands-free, voice chat, dictation, captions, read-aloud | `kernel/src/speech/speech_session.[ch]` | Tested with fake engines |
| macOS engines: Speech framework and AVSpeechSynthesizer | `kernel/arch/hosted/zx_speech_mac.[hm]` | Syntax-checked only. **Not run on a Mac.** |
| Language identification, 82 languages | `kernel/src/xlate/xlate_langid.[ch]`, generated `xlate_langid_model.h` | Tested; accuracy given below |
| Translation pipeline, cache, reader settings, markers, caption filter | `kernel/src/xlate/xlate.[ch]` | Tested with a fake model |
| Prompt templates (Qwen2.5, MADLAD-400, Mistral) | `kernel/src/xlate/xlate_prompts.c` | Tested (formatting and injection checks) |

The kernel code is freestanding integer C11: no libc, no malloc, no floating point, no 64-bit division. Engines and models attach through function pointers. **When no engine is bound, every path fails closed.** The system returns a typed error and never makes up a transcript, a sound or a translation.

## 1. Voice chat, dictation and read-aloud

### The audio path

```
microphone (any rate 8-96 kHz, S16 mono)
  -> speech_resample        to 16 kHz (Kaiser-windowed sinc, exact rational clock)
  -> [hands-free] speech_vad_frame per 10 ms: energy + zero crossings,
       adaptive noise floor, 3-frame onset, 300 ms hangover,
       300 ms pre-roll so the first syllable is kept
  -> capture one utterance (caller buffer, length limit, default 30 s)
  -> STT engine (text-level speech_stt_ops_t; voice.h phonemes as fallback)
  -> transcript event
```

A recogniser that takes log-mel features, such as a Whisper port on `zt_model`, calls `speech_logmel()`. It is the Whisper front end (periodic Hann, n_fft 400, hop 160, 80 Slaney mel bands, log10 with a 1e-10 floor, clamp to max − 8, then (x + 4) / 4) in Q16 integers. On the test signals its largest error against a float64 numpy implementation of Whisper's `log_mel_spectrogram` is **0.000275** in Whisper's normalised units. The test asserts a limit of 0.0005 and a mean error of at most 0.00002. Full-scale input stays within 0.00003 of a double-precision reference. Pre-emphasis is a separate step because Whisper does not use it.

### Modes and uses

`speech_config_t` combines one mode with one use:

| | **Push to talk** (`ptt_down` / `ptt_up`) | **Hands-free** (`arm`, then the VAD) |
|---|---|---|
| **ASSISTANT**: talk to the system | Hold, speak, release. The transcript goes to the reply hook (the chat model) and the reply is spoken. | The person speaks whenever they like. Speaking over a reply is a *barge-in*: a `BARGE_IN` event tells the app to stop playback, then the session listens. With `barge_in` off, the session ignores the microphone while it is speaking, so the assistant does not hear itself. |
| **DICTATION**: voice typing | The transcript event carries text for the focused field. | Same, one transcript per utterance. |
| **CAPTIONS**: calls and video | Each utterance passes through `caption_filter` (normally `xlate_caption_filter`) and is shown as a caption. | Same, continuously. |

- **Typed prompts join the same conversation.** `speech_session_submit_text()` sends typed text through the same reply hook. It speaks the reply only if `speak_replies` is set, so a person can switch between typing and talking.
- **Read-aloud.** `speech_session_say(text, lang)` covers accessibility, article reading and notifications.
- **Events.** The app receives `STATE`, `UTTERANCE_START/END`, `TRANSCRIPT`, `CAPTION`, `REPLY`, `SPOKEN`, `BARGE_IN` and `ERROR`, with these flags:
  - `SPEECH_EVF_LOSSY`: the text went through voice.h's phoneme alphabet rather than a word-level engine.
  - `SPEECH_EVF_MACHINE_TRANSLATED`
  - `SPEECH_EVF_TRUNCATED`

### The reply hook and the assistant

`speech_reply_fn` is where the conversational model plugs in. The integer forward pass in `kernel/src/tensor/zt_model.h` (`zt_model_generate`) fits here: format the user's turn with the model's chat template, generate with `stop_strs = "<|im_end|>"` and copy the bytes out. The Chiglet (`kernel/src/chiglet`) is not a language model. It is an evidence-weighted decider: use it to decide whether to act, and use `zt_model` to word the reply. The reply hook can also call apps, for productivity tools or anything else a developer exposes (see `docs/APP_DEVELOPMENT.md`).

### Engines

Text-level engines are preferred. `voice.h`'s phoneme boundary (`voice_set_stt` / `voice_set_tts`) is supported as a fallback. It is lossy because voice.h spells phonemes back as letters, so text that arrives this way is flagged. The macOS adapter also binds into voice.h, with the same caveat.

On macOS (`zx_speech_mac.m`, macOS 10.15+):
- **Recognition** uses `SFSpeechRecognizer` with `requiresOnDeviceRecognition = YES` when `supportsOnDeviceRecognition`. By default it refuses to send audio to Apple's servers; `zx_speech_mac_require_on_device(0)` allows that.
- **Synthesis** uses `AVSpeechSynthesizer writeUtterance:toBufferCallback:`. The output is resampled to 16 kHz by the kernel resampler, or played directly with `zx_speech_mac_speak`.
- **Build:**

  ```
  clang -fobjc-arc -c kernel/arch/hosted/zx_speech_mac.m -Ikernel/src/speech -Ikernel/src/voice \
        -Ikernel/src/chiglet -Ikernel/src/surplus
  # link: -framework Foundation -framework Speech -framework AVFoundation, plus speech_dsp.c, voice.c
  ```

- **Info.plist:** the app bundle needs `NSSpeechRecognitionUsageDescription`, and `NSMicrophoneUsageDescription` if it records.
- **Testing:** the file is guarded by `__APPLE__`, and the Linux and Windows builds never compile it. It was written against Apple's documented API and syntax-checked against stub headers only. **It has not been run on a Mac**, so test it on macOS 13+ before shipping.

## 2. Auto-translation for the feed, chat and calls

### Pipeline (`xlate_view`)

```
item (post / message / caption)
  -> source language: given by the author, else xlate_langid() on the device
  -> does this reader want it?  (xlate_prefs_t)
       auto-translate off               -> original
       same language as the reader*     -> original (no model call, no cache lookup)
       a language the reader also reads -> original
       "show original" toggled for item -> original, flagged XLATE_F_CAN_TRANSLATE
  -> cache lookup: SHA-256(content id or text), source tag, target tag
  -> template (xlate_prompts.c) -> backend (function pointer) -> clean output
  -> cache, return with XLATE_F_MACHINE and the model id
```

\* "Same language" compares primary subtags, so `en-GB` text is not translated for an `en` reader. Chinese also compares scripts: `zh-Hant` is not the same as `zh-Hans`, and `zh-TW` / `zh-HK` / `zh-MO` count as Hant.

**Translated once per language.** The cache key is the post's content id (the IPFS CID digest, when the caller has one) plus the source and target tags. The cache is domain-separated so a text can never collide with a CID, and it is evicted least-recently-used first. Ten readers of the same Spanish post in English cost one model call (tested). Binding a different backend empties the cache, because a different model gives different text. A translation longer than a cache slot is still returned but is not cached.

**Failure shows the original.** If the model is missing, fails, returns nothing, or the language cannot be identified, the reader sees the original, and `status` says why.

**Markers.** Every machine translation carries `XLATE_F_MACHINE`, and there are three ways to show it:
- `xlate_marker_label()` renders the English fallback "Translated from Spanish by machine (model)". The i18n catalog key is `xlate.marker.machine_translated`. Per the i18n Enochian-core rule, the core stores the key and the edge renders the text.
- `xlate_marker_tag()` gives the compact `[MT es>en]` for plain-text channels and exports.
- Text that already starts with a marker is never translated again.

**Prompt safety.** Content is data, never instructions. The templates say so. `xlate_build_prompt()` defuses `<|...`, `[INST]`, `[/INST]`, `</s>` and `<2xx>` inside content by inserting a space, so a post cannot close the chat turn or retarget MADLAD (tested). Model output is bounded, cut at the stop sequence and at any chat control token, and trimmed.

### Where it is used

| Surface | Kind | Notes |
|---|---|---|
| Feed | `XLATE_KIND_POST` | Key the item by its CID. Show the marker and a one-tap "Show original" (`xlate_prefs_toggle_original`). |
| Chat | `XLATE_KIND_CHAT` | Key by message id. The sender's language comes from their settings when it is known; otherwise langid identifies it. |
| Calls: live captions | `XLATE_KIND_CAPTION` | **STT, then translate, then show.** The speech session in CAPTIONS use calls `xlate_caption_filter` with the speaker's detected language. The caption is marked machine-translated. If translation fails, the original caption is shown without the mark (tested end to end in `test_xlate`). `translate_captions` turns this off separately. |
| UI strings | `XLATE_KIND_UI` | Only for text the i18n catalogs lack. The 778 CLDR locales in `kernel/src/i18n` come first. |

### Templates and models (data, `xlate_prompts.c`)

| Template id | Format | Written for |
|---|---|---|
| `qwen2.5-instruct` | ChatML | Qwen2.5 0.5B / 1.5B / 7B / 14B / 32B Instruct |
| `madlad400` | `<2xx> text` | google/madlad400-3b-mt, -7b-mt, -10b-mt |
| `mistral-instruct` | `[INST]` | Mistral-7B-Instruct-v0.3 |

Plugging in `zt_model`: write an `xlate_backend_fn` that tokenises `req->prompt` with `parse_special = true`, calls `zt_model_generate` with `req->stop` as the stop string and `max_tokens` taken from `req->max_out`, then copies the bytes to `out`. Set `backend.tmpl = xlate_template_find("qwen2.5-instruct")`.

## 3. Language identification

The identifier works in four steps:
1. It normalises the text: decodes UTF-8, drops combining marks, folds case, and collapses punctuation and digits to word breaks.
2. It routes by Unicode script. A language that is alone in its script is decided by the script.
3. Han-only text is scored against the Chinese varieties and Japanese.
4. Otherwise it takes a naive-Bayes score over hashed character 1-3-grams.

**Size.** 82 languages, 17,831 hashed n-grams, 44,400 entries: about 200 KB of read-only data.

**Training data.** All of it is public domain:
- Mozilla Common Voice sentence collection, CC0-1.0, `github.com/common-voice/common-voice` at commit `a3acd695856c`.
- Public-domain Bibles from the eBible corpus (`github.com/BibleNLP/ebible` at commit `c531ff2da028`, listed as Public Domain in its `metadata/licences.tsv`). These only top up Croatian, Hebrew and Swahili (swh1850).
- CC BY-SA and NC/ND sources were deliberately left out.
- `gen_langid_model.py fetch DIR` downloads exactly these files, and `build DIR OUT` retrains the model and prints the report.

**Held-out accuracy.** The test set is every sentence whose hash ≡ 0 mod 10, which is never trained on, capped at 1,000 per language: 55,282 sentences. Overall accuracy is **95.28%**:

| Sentence length | Accuracy |
|---|---|
| under 20 characters | 84.95% |
| 20-49 characters | 96.08% |
| 50 characters or more | 98.61% |

The C classifier gave the same answer as the Python trainer on all 55,282 sentences. `test_xlate` re-checks a 321-sentence fixture exactly (96.6% correct).

**Strong:** every single-script language (100%), ja 99.6%, ar, he, fa, ur, bn, mr, ro, fi, is, pl, cy.

**Weak:** close pairs.

| Language | Accuracy | Most often mistaken for |
|---|---|---|
| nb | 66% | da |
| pt | 79% | gl |
| gl | 81% | pt, es |
| sk | 77% | cs |
| zu / xh | 83-88% | each other |
| zh-Hant | 86% | yue |
| es | 88% | gl |
| sr | 88% | mk |

Some languages have few held-out sentences, so their figures are rough: lt (15), te (23), kn (5), tt (29), bg (64), zh-Hans (67).

Use `reliable` (confidence ≥ 30 and ≥ 12 letters) before acting on a guess. Prefer the author's declared language, and let the reader correct it.

## 4. Privacy: on-device first

- **Language identification, VAD, resampling and log-mel** always run on the device. They are integers and tables, with no network.
- **Recognition.** On macOS it is on-device only by default. On other platforms, bind an on-device engine (whisper.cpp / Whisper weights, or a Whisper port on `zt_model`).
- **Translation and the assistant** run on whatever backend is bound. The intended default is a local model through `zt_model`. A remote backend is a deliberate choice by the integrator and should be shown to the person.
- **Retention.** Audio is held only in the caller's capture buffer for one utterance. The session keeps no recordings. The translation cache holds translated text only, keyed by a hash.
- **Opt-outs.** Auto-translate and caption translation are per-reader switches. "Show original" is always one tap away.

## 5. Engines and model licences

Each name below was checked for commercial use. Re-check the exact checkpoint you ship.

| Engine / model | Use | Licence |
|---|---|---|
| OpenAI Whisper weights (tiny … large-v3, large-v3-turbo) | STT | MIT |
| whisper.cpp | STT runtime | MIT |
| Apple Speech framework, AVSpeechSynthesizer | STT / TTS on macOS | Apple SDK licence (system frameworks, usable by Mac apps) |
| Piper (rhasspy/piper, archived MIT release) | TTS | Code MIT. **Each voice has its own licence**, so check the model card. The newer OHF-Voice `piper1-gpl` is GPL-3.0, and phonemisation through espeak-ng is GPL-3.0. |
| Kokoro-82M | TTS | Apache-2.0 (its G2P may fall back to espeak-ng, GPL-3.0) |
| Qwen2.5 0.5B / 1.5B / 7B / 14B / 32B Instruct | assistant, translation | Apache-2.0. **Qwen2.5-3B and -72B are not Apache** (Qwen licences). |
| MADLAD-400 MT (3B / 7B / 10B) | translation | Apache-2.0 |
| Mistral-7B-Instruct-v0.3 | assistant, translation | Apache-2.0 |
| Language-ID tables (this repo) | langid | Trained only on CC0 and public-domain text |
| **Not allowed** | | NLLB-200, SeamlessM4T, Meta MMS (CC-BY-NC) |

## 6. Known gaps

- `zx_speech_mac.m` has never run on a Mac.
- There is no Linux or Windows STT/TTS engine adapter yet. Whisper on `zt_model` needs the encoder-decoder graph; `speech_logmel` is ready for it.
- There is no streaming (partial) transcription. Utterances are transcribed when they end.
- Long posts over `XLATE_TEXT_MAX` (4 KB) are not split into chunks, and translations over 1 KB are not cached.
- The marker label is English until the i18n catalog carries `xlate.marker.machine_translated`.
- MADLAD's Traditional Chinese token (`<2zh_Hant>`) needs checking against the shipped tokenizer.
- voice.h's boundary is phoneme-level. A text-level `voice_set_*` variant in voice.h would remove the lossy fallback (voice.h was not changed here).
