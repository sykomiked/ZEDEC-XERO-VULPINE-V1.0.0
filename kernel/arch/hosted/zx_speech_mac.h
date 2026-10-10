/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_speech_mac.h — macOS speech engines for the hosted build, callable from C.
 *
 *   speech recognition   Apple Speech framework (SFSpeechRecognizer), with
 *                        requiresOnDeviceRecognition = YES whenever the
 *                        recognizer supports it; by default it REFUSES to
 *                        fall back to Apple's servers (see
 *                        zx_speech_mac_require_on_device)
 *   speech synthesis     AVSpeechSynthesizer: rendered to 16 kHz S16 PCM
 *                        (writeUtterance:toBufferCallback:, macOS 10.15+) or
 *                        played straight to the speakers
 *
 * They plug into kernel/src/speech (speech_stt_ops_t / speech_tts_ops_t, the
 * text-level boundary) and, lossily, into kernel/src/voice (voice_set_stt /
 * voice_set_tts, the phoneme boundary).
 *
 * macOS only: everything here is inside #if defined(__APPLE__), and the Linux
 * and Windows builds never compile zx_speech_mac.m. Build on a Mac with
 *   clang -fobjc-arc -c kernel/arch/hosted/zx_speech_mac.m -Ikernel/src/speech \
 *         -Ikernel/src/voice -Ikernel/src/chiglet -Ikernel/src/surplus ...
 *   and link with -framework Foundation -framework Speech -framework AVFoundation
 * The app bundle's Info.plist must carry NSSpeechRecognitionUsageDescription
 * (and NSMicrophoneUsageDescription if the app records), or macOS terminates
 * the process on first use.
 *
 * UNTESTED ON A MAC: written against the documented APIs and syntax-checked
 * only; run it on macOS 13+ before relying on it.
 */
#ifndef ZX_SPEECH_MAC_H
#define ZX_SPEECH_MAC_H

#if defined(__APPLE__)

#    include <stdint.h>
#    include "speech_session.h"
#    include "voice.h"

typedef enum {
    ZXM_OK = 0,
    ZXM_ERR_ARG = -1,
    ZXM_ERR_UNAVAILABLE = -2, /* no recognizer / voice for the locale, or no on-device model */
    ZXM_ERR_DENIED = -3,      /* the person has not allowed speech recognition         */
    ZXM_ERR_TIMEOUT = -4,
    ZXM_ERR_ENGINE = -5,
    ZXM_ERR_TRUNC = -6
} zxm_result_t;

/* Ask for (or confirm) speech-recognition permission. Blocks up to
 * timeout_ms. ZXM_OK when authorised. */
int zx_speech_mac_authorize(uint32_t timeout_ms);

/* 1 (default): refuse recognition unless it runs on this Mac. 0: allow
 * Apple's server recognition when no on-device model exists. */
void zx_speech_mac_require_on_device(int on);

/* Default locale ("en-US") and per-call timeout (ms) for the ops adapters. */
void zx_speech_mac_configure(const char *locale, uint32_t timeout_ms);

/* Can `locale` be recognised now, and on device? */
int zx_speech_mac_stt_available(const char *locale, int *on_device);

/* Recognise n samples of mono S16 PCM at `rate` Hz. Writes a NUL-terminated
 * transcript. *on_device (optional) reports whether it ran locally. */
int zx_speech_mac_transcribe(const int16_t *pcm, uint32_t n, uint32_t rate, const char *locale,
                             char *text, uint32_t cap, int *on_device, uint32_t timeout_ms);

/* Synthesise text to 16 kHz mono S16 PCM. lang: BCP 47 ("" = system voice). */
int zx_speech_mac_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, uint32_t timeout_ms);

/* Speak through the speakers (returns at once) / stop speaking. */
int zx_speech_mac_speak(const char *text, const char *lang);
void zx_speech_mac_stop(void);

/* Adapters for kernel/src/speech (preferred: full words, any language). */
void zx_speech_mac_stt_ops(speech_stt_ops_t *ops);
void zx_speech_mac_tts_ops(speech_tts_ops_t *ops);

/* Adapters for kernel/src/voice's phoneme boundary. LOSSY: phonemes are
 * spelled back to letters before synthesis, and transcripts are reduced to
 * voice.h's phoneme alphabet. Prefer the speech_session adapters. */
void zx_speech_mac_bind_voice(voice_t *v);

#endif /* __APPLE__ */
#endif /* ZX_SPEECH_MAC_H */
