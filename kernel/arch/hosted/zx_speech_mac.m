/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_speech_mac.m — macOS on-device speech recognition and synthesis behind
 * the kernel's C speech boundaries. See zx_speech_mac.h.
 *
 * UNTESTED ON A MAC. Written against Apple's documented Speech and
 * AVFoundation APIs (macOS 10.15+ for on-device recognition and buffer
 * synthesis) and syntax-checked only. Compile with -fobjc-arc. */
#if defined(__APPLE__)

#    import <Foundation/Foundation.h>
#    import <Speech/Speech.h>
#    import <AVFoundation/AVFoundation.h>

#    include <string.h>
#    include "zx_speech_mac.h"
#    include "speech_dsp.h"

static int g_require_on_device = 1;
static char g_locale[32] = "en-US";
static uint32_t g_timeout_ms = 30000;

void zx_speech_mac_require_on_device(int on)
{
    g_require_on_device = on ? 1 : 0;
}

void zx_speech_mac_configure(const char *locale, uint32_t timeout_ms)
{
    if (locale && locale[0]) {
        strncpy(g_locale, locale, sizeof g_locale - 1);
        g_locale[sizeof g_locale - 1] = '\0';
    }
    if (timeout_ms) g_timeout_ms = timeout_ms;
}

/* Wait for sem. On the main thread the run loop keeps turning (framework
 * callbacks may be delivered there); elsewhere it is a plain wait. */
static int zxm_wait(dispatch_semaphore_t sem, uint32_t timeout_ms)
{
    if ([NSThread isMainThread]) {
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout_ms / 1000.0];
        while (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) != 0) {
            if ([deadline timeIntervalSinceNow] <= 0) return -1;
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        return 0;
    }
    dispatch_time_t t =
        dispatch_time(DISPATCH_TIME_NOW, (int64_t) timeout_ms * (int64_t) NSEC_PER_MSEC);
    return dispatch_semaphore_wait(sem, t) == 0 ? 0 : -1;
}

static NSString *zxm_str(const char *s)
{
    if (!s) return @"";
    NSString *v = [NSString stringWithUTF8String:s];
    return v ? v : @"";
}

static int zxm_copy(NSString *s, char *out, uint32_t cap)
{
    if (!out || cap == 0) return ZXM_ERR_ARG;
    out[0] = '\0';
    if (!s) return ZXM_OK;
    const char *u = [s UTF8String];
    if (!u) return ZXM_ERR_ENGINE;
    size_t n = strlen(u);
    if (n + 1 > cap) {
        size_t k = cap - 1;
        while (k > 0 && ((unsigned char) u[k] & 0xC0) == 0x80) k--; /* whole code points */
        memcpy(out, u, k);
        out[k] = '\0';
        return ZXM_ERR_TRUNC;
    }
    memcpy(out, u, n + 1);
    return ZXM_OK;
}

int zx_speech_mac_authorize(uint32_t timeout_ms)
{
    @autoreleasepool {
        SFSpeechRecognizerAuthorizationStatus st = [SFSpeechRecognizer authorizationStatus];
        if (st == SFSpeechRecognizerAuthorizationStatusAuthorized) return ZXM_OK;
        if (st == SFSpeechRecognizerAuthorizationStatusDenied ||
            st == SFSpeechRecognizerAuthorizationStatusRestricted)
            return ZXM_ERR_DENIED;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block SFSpeechRecognizerAuthorizationStatus got =
            SFSpeechRecognizerAuthorizationStatusNotDetermined;
        [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus s) {
          got = s;
          dispatch_semaphore_signal(sem);
        }];
        if (zxm_wait(sem, timeout_ms ? timeout_ms : g_timeout_ms) != 0) return ZXM_ERR_TIMEOUT;
        return got == SFSpeechRecognizerAuthorizationStatusAuthorized ? ZXM_OK : ZXM_ERR_DENIED;
    }
}

static SFSpeechRecognizer *zxm_recognizer(const char *locale, int *on_device)
{
    NSString *ident = zxm_str(locale && locale[0] ? locale : g_locale);
    SFSpeechRecognizer *rec =
        [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:ident]];
    int od = 0;
    if (rec) {
        if (@available(macOS 10.15, *)) od = rec.supportsOnDeviceRecognition ? 1 : 0;
    }
    if (on_device) *on_device = od;
    return rec;
}

int zx_speech_mac_stt_available(const char *locale, int *on_device)
{
    @autoreleasepool {
        int od = 0;
        SFSpeechRecognizer *rec = zxm_recognizer(locale, &od);
        if (on_device) *on_device = od;
        if (!rec || !rec.isAvailable) return ZXM_ERR_UNAVAILABLE;
        if (g_require_on_device && !od) return ZXM_ERR_UNAVAILABLE;
        return ZXM_OK;
    }
}

int zx_speech_mac_transcribe(const int16_t *pcm, uint32_t n, uint32_t rate, const char *locale,
                             char *text, uint32_t cap, int *on_device, uint32_t timeout_ms)
{
    if (!pcm || n == 0 || !text || cap == 0 || rate < 8000 || rate > 192000) return ZXM_ERR_ARG;
    text[0] = '\0';
    if (on_device) *on_device = 0;
    if ([SFSpeechRecognizer authorizationStatus] != SFSpeechRecognizerAuthorizationStatusAuthorized)
        return ZXM_ERR_DENIED;
    @autoreleasepool {
        int od = 0;
        SFSpeechRecognizer *rec = zxm_recognizer(locale, &od);
        if (!rec || !rec.isAvailable) return ZXM_ERR_UNAVAILABLE;
        if (g_require_on_device && !od) return ZXM_ERR_UNAVAILABLE;
        /* Deliver results on our own serial queue, never the (maybe blocked)
         * main queue. */
        NSOperationQueue *q = [[NSOperationQueue alloc] init];
        q.maxConcurrentOperationCount = 1;
        rec.queue = q;

        SFSpeechAudioBufferRecognitionRequest *req =
            [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        req.shouldReportPartialResults = NO;
        if (@available(macOS 10.15, *)) {
            if (od) req.requiresOnDeviceRecognition = YES;
        }
        AVAudioFormat *fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                              sampleRate:(double) rate
                                                                channels:1
                                                             interleaved:NO];
        AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:n];
        if (!buf) return ZXM_ERR_ENGINE;
        buf.frameLength = n;
        float *ch = buf.floatChannelData[0];
        for (uint32_t i = 0; i < n; i++) ch[i] = (float) pcm[i] / 32768.0f;
        [req appendAudioPCMBuffer:buf];
        [req endAudio];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSString *best = nil;
        __block BOOL failed = NO;
        __block BOOL finished = NO;
        SFSpeechRecognitionTask *task =
            [rec recognitionTaskWithRequest:req
                              resultHandler:^(SFSpeechRecognitionResult *result, NSError *error) {
                                if (finished) return;
                                if (result) best = result.bestTranscription.formattedString;
                                if (error || (result && result.isFinal)) {
                                    failed = (error != nil) && best == nil;
                                    finished = YES;
                                    dispatch_semaphore_signal(sem);
                                }
                              }];
        if (zxm_wait(sem, timeout_ms ? timeout_ms : g_timeout_ms) != 0) {
            [task cancel];
            return ZXM_ERR_TIMEOUT;
        }
        if (failed) return ZXM_ERR_ENGINE;
        if (on_device) *on_device = od;
        return zxm_copy(best, text, cap);
    }
}

/* Append one synthesizer buffer, converted to S16 and resampled to 16 kHz. */
static void zxm_take(AVAudioPCMBuffer *pb, speech_resampler_t *rs, int16_t *pcm, uint32_t cap,
                     uint32_t *len)
{
    AVAudioFrameCount frames = pb.frameLength;
    int16_t tmp[1024];
    AVAudioFrameCount off = 0;
    while (off < frames && *len < cap) {
        uint32_t k = frames - off < 1024 ? frames - off : 1024;
        if (pb.format.commonFormat == AVAudioPCMFormatInt16 && pb.int16ChannelData) {
            memcpy(tmp, pb.int16ChannelData[0] + off, k * sizeof(int16_t));
        } else if (pb.format.commonFormat == AVAudioPCMFormatFloat32 && pb.floatChannelData) {
            const float *f = pb.floatChannelData[0] + off;
            for (uint32_t i = 0; i < k; i++) {
                float v = f[i] * 32768.0f;
                tmp[i] = (int16_t) (v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : (int) v));
            }
        } else {
            return; /* unexpected format: stop, never write garbage */
        }
        uint32_t used = 0, at = 0;
        while (at < k && *len < cap) {
            *len += speech_resample(rs, tmp + at, k - at, &used, pcm + *len, cap - *len);
            if (used == 0) break;
            at += used;
        }
        off += k;
    }
}

int zx_speech_mac_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, uint32_t timeout_ms)
{
    if (out_len) *out_len = 0;
    if (!text || !pcm || cap == 0 || !out_len) return ZXM_ERR_ARG;
    if (@available(macOS 10.15, *)) {
        @autoreleasepool {
            AVSpeechUtterance *u = [AVSpeechUtterance speechUtteranceWithString:zxm_str(text)];
            AVSpeechSynthesisVoice *voice =
                (lang && lang[0]) ? [AVSpeechSynthesisVoice voiceWithLanguage:zxm_str(lang)] : nil;
            if (lang && lang[0] && !voice) return ZXM_ERR_UNAVAILABLE;
            if (voice) u.voice = voice;
            AVSpeechSynthesizer *synth = [[AVSpeechSynthesizer alloc] init];
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block speech_resampler_t rs;
            __block BOOL rs_ready = NO;
            __block BOOL bad = NO;
            __block BOOL finished = NO;
            __block uint32_t len = 0;
            [synth writeUtterance:u
                 toBufferCallback:^(AVAudioBuffer *buffer) {
                   if (finished) return;
                   AVAudioPCMBuffer *pb = [buffer isKindOfClass:[AVAudioPCMBuffer class]]
                                              ? (AVAudioPCMBuffer *) buffer
                                              : nil;
                   if (!pb || pb.frameLength == 0) { /* a zero-length buffer ends it */
                       finished = YES;
                       dispatch_semaphore_signal(sem);
                       return;
                   }
                   if (!rs_ready) {
                       if (speech_resampler_init(&rs, (uint32_t) pb.format.sampleRate) != 0 ||
                           pb.format.channelCount != 1) {
                           bad = YES;
                           finished = YES;
                           dispatch_semaphore_signal(sem);
                           return;
                       }
                       rs_ready = YES;
                   }
                   zxm_take(pb, &rs, pcm, cap, &len);
                 }];
            if (zxm_wait(sem, timeout_ms ? timeout_ms : g_timeout_ms) != 0) {
                [synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
                return ZXM_ERR_TIMEOUT;
            }
            if (bad) return ZXM_ERR_ENGINE;
            /* Flush the resampler's delay line with silence. */
            if (rs_ready && len < cap) {
                int16_t z[256] = {0};
                uint32_t left = speech_resampler_latency(&rs), used = 0;
                while (left > 0 && len < cap) {
                    uint32_t k = left < 256 ? left : 256;
                    len += speech_resample(&rs, z, k, &used, pcm + len, cap - len);
                    if (used == 0) break;
                    left -= used;
                }
            }
            *out_len = len;
            return len ? ZXM_OK : ZXM_ERR_ENGINE;
        }
    }
    return ZXM_ERR_UNAVAILABLE;
}

static AVSpeechSynthesizer *g_speaker;

int zx_speech_mac_speak(const char *text, const char *lang)
{
    if (!text) return ZXM_ERR_ARG;
    @autoreleasepool {
        if (!g_speaker) g_speaker = [[AVSpeechSynthesizer alloc] init];
        AVSpeechUtterance *u = [AVSpeechUtterance speechUtteranceWithString:zxm_str(text)];
        if (lang && lang[0]) {
            AVSpeechSynthesisVoice *v = [AVSpeechSynthesisVoice voiceWithLanguage:zxm_str(lang)];
            if (!v) return ZXM_ERR_UNAVAILABLE;
            u.voice = v;
        }
        [g_speaker speakUtterance:u];
        return ZXM_OK;
    }
}

void zx_speech_mac_stop(void)
{
    if (g_speaker) [g_speaker stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

/* ---- adapters: kernel/src/speech (text level) ---- */
static int zxm_stt_cb(const int16_t *pcm, uint32_t n, const char *lang_hint, char *text,
                      uint32_t cap, char *lang_out, uint32_t lang_cap, void *ctx)
{
    (void) ctx;
    const char *loc = (lang_hint && lang_hint[0]) ? lang_hint : g_locale;
    int rc = zx_speech_mac_transcribe(pcm, n, SPEECH_SAMPLE_RATE, loc, text, cap, 0, g_timeout_ms);
    if (rc != ZXM_OK && rc != ZXM_ERR_TRUNC) return rc;
    if (lang_out && lang_cap) {
        strncpy(lang_out, loc, lang_cap - 1);
        lang_out[lang_cap - 1] = '\0';
    }
    return 0;
}

static int zxm_tts_cb(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                      uint32_t *out_len, void *ctx)
{
    (void) ctx;
    int rc = zx_speech_mac_synthesize(text, lang, pcm, cap, out_len, g_timeout_ms);
    return rc == ZXM_OK ? 0 : rc;
}

void zx_speech_mac_stt_ops(speech_stt_ops_t *ops)
{
    if (!ops) return;
    ops->transcribe = zxm_stt_cb;
    ops->ctx = 0;
}

void zx_speech_mac_tts_ops(speech_tts_ops_t *ops)
{
    if (!ops) return;
    ops->synthesize = zxm_tts_cb;
    ops->ctx = 0;
}

/* ---- adapters: kernel/src/voice (phonemes; lossy) ---- */
static int zxm_voice_synth(const uint8_t *ph, uint32_t n, int16_t *pcm, uint32_t cap,
                           uint32_t *out_len, void *ctx)
{
    (void) ctx;
    char spelled[4 * VOICE_MAX_PHONEMES + 1];
    uint32_t w = 0;
    for (uint32_t i = 0; i < n && i < VOICE_MAX_PHONEMES; i++) {
        const char *s = voice_phoneme_str(ph[i]);
        for (uint32_t k = 0; s[k] && w + 1 < sizeof spelled; k++) spelled[w++] = s[k];
    }
    spelled[w] = '\0';
    int rc = zx_speech_mac_synthesize(spelled, g_locale, pcm, cap, out_len, g_timeout_ms);
    return rc == ZXM_OK ? 0 : -1;
}

static int zxm_voice_recognize(const int16_t *pcm, uint32_t n, uint8_t *ph, uint32_t cap,
                               uint32_t *out_len, void *ctx)
{
    (void) ctx;
    char text[2048];
    int rc = zx_speech_mac_transcribe(pcm, n, SPEECH_SAMPLE_RATE, g_locale, text, sizeof text, 0,
                                      g_timeout_ms);
    if (rc != ZXM_OK && rc != ZXM_ERR_TRUNC) return -1;
    *out_len = voice_text_to_phonemes(text, ph, cap);
    return 0;
}

void zx_speech_mac_bind_voice(voice_t *v)
{
    if (!v) return;
    voice_tts_ops_t t = {zxm_voice_synth, 0};
    voice_stt_ops_t s = {zxm_voice_recognize, 0};
    voice_set_tts(v, &t);
    voice_set_stt(v, &s);
}

#endif /* __APPLE__ */
