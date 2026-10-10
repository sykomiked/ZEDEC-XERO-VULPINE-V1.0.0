/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* voice.c — the deterministic text<->phoneme bridge. See voice.h.
 *
 * Everything here is integer, bounded, and clock-free. The two neural models
 * (TTS, STT) are never touched directly — they arrive as bound ops or they do
 * not arrive at all, and "do not arrive" is a typed error, not a guess. */
#include "voice.h"

/* ---- tiny local char helpers (freestanding: no <ctype.h>) ---- */
static char v_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}
static bool v_is_az(char c) { return c >= 'a' && c <= 'z'; }
static bool v_is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* Per-letter default phoneme, indexed by ('a'..'z' - 'a'). The digraph rules in
 * voice_text_to_phonemes take precedence over this table. */
static const uint8_t k_letter_ph[26] = {
    PH_AE, /* a */ PH_B,  /* b */ PH_K,  /* c */ PH_D,  /* d */
    PH_EH, /* e */ PH_F,  /* f */ PH_G,  /* g */ PH_HH, /* h */
    PH_IH, /* i */ PH_JH, /* j */ PH_K,  /* k */ PH_L,  /* l */
    PH_M,  /* m */ PH_N,  /* n */ PH_OW, /* o */ PH_P,  /* p */
    PH_KW, /* q */ PH_R,  /* r */ PH_S,  /* s */ PH_T,  /* t */
    PH_AH, /* u */ PH_V,  /* v */ PH_W,  /* w */ PH_KS, /* x */
    PH_Y,  /* y */ PH_Z   /* z */
};

/* Reverse: canonical spelling a phoneme prints back to. Lossy on purpose. */
static const char *const k_ph_str[PH__COUNT] = {
    " ",  /* PH_SIL */ "a",  "b",  "k",  "d",  "e",  "f",  "g",  "h",
    "i",  "j",  "l",  "m",  "n",  "o",  "p",  "kw", "r",  "s",  "t",
    "u",  "v",  "w",  "ks", "y",  "z",  "sh", "ch", "th", "ng"
};

const char *voice_phoneme_str(uint8_t phoneme) {
    if (phoneme >= PH__COUNT) return "";
    return k_ph_str[phoneme];
}

void voice_init(voice_t *v) {
    if (!v) return;
    v->tts.synth = 0;   v->tts.ctx = 0;
    v->stt.recognize = 0; v->stt.ctx = 0;
    v->spoken = 0;
    v->heard = 0;
}

void voice_set_tts(voice_t *v, const voice_tts_ops_t *ops) {
    if (!v) return;
    if (ops) v->tts = *ops;
    else { v->tts.synth = 0; v->tts.ctx = 0; }
}

void voice_set_stt(voice_t *v, const voice_stt_ops_t *ops) {
    if (!v) return;
    if (ops) v->stt = *ops;
    else { v->stt.recognize = 0; v->stt.ctx = 0; }
}

bool voice_has_tts(const voice_t *v) { return v && v->tts.synth != 0; }
bool voice_has_stt(const voice_t *v) { return v && v->stt.recognize != 0; }

/* ===== THE REAL ON-DEVICE CORE ===== */
uint32_t voice_text_to_phonemes(const char *text, uint8_t *out, uint32_t cap) {
    if (!text || !out || cap == 0) return 0;

    uint32_t n = 0;
    uint32_t i = 0;
    uint8_t last = PH__COUNT;   /* sentinel: "nothing emitted yet" */

    while (text[i] != '\0') {
        char c0 = v_lower(text[i]);

        if (v_is_az(c0)) {
            char c1 = (text[i + 1] != '\0') ? v_lower(text[i + 1]) : '\0';
            uint8_t tok;
            uint32_t adv;

            /* Digraph rules first — they consume two graphemes. */
            if      (c0 == 's' && c1 == 'h') { tok = PH_SH; adv = 2; }
            else if (c0 == 'c' && c1 == 'h') { tok = PH_CH; adv = 2; }
            else if (c0 == 't' && c1 == 'h') { tok = PH_TH; adv = 2; }
            else if (c0 == 'n' && c1 == 'g') { tok = PH_NG; adv = 2; }
            else if (c0 == 'p' && c1 == 'h') { tok = PH_F;  adv = 2; }
            else if (c0 == 'c' && c1 == 'k') { tok = PH_K;  adv = 2; }
            else                             { tok = k_letter_ph[c0 - 'a']; adv = 1; }

            if (n >= cap) break;            /* bounded: never overrun `out` */
            out[n++] = tok;
            last = tok;
            i += adv;
        } else if (v_is_space(c0)) {
            /* Whitespace -> a single word boundary, collapsed, never leading. */
            if (n > 0 && last != PH_SIL) {
                if (n >= cap) break;
                out[n++] = PH_SIL;
                last = PH_SIL;
            }
            i++;
        } else {
            /* Punctuation and everything else: silently skipped. */
            i++;
        }
    }
    /* Trim a trailing word boundary: "a b " is two words, not two-and-a-gap. */
    if (n > 0 && out[n - 1] == PH_SIL) n--;
    return n;
}

voice_result_t voice_speak(voice_t *v, const char *text,
                           int16_t *pcm, uint32_t cap, uint32_t *out_len) {
    if (out_len) *out_len = 0;
    if (!v || !text || !pcm) return VOICE_ERR_ARG;

    /* On-device decomposition happens regardless — it touches only our own
     * stack buffer, never the caller's PCM. */
    uint8_t ph[VOICE_MAX_PHONEMES];
    uint32_t n = voice_text_to_phonemes(text, ph, VOICE_MAX_PHONEMES);

    /* FAIL CLOSED: no model, no sound. Not one sample is written. */
    if (!v->tts.synth) return VOICE_ERR_NO_ENGINE;

    uint32_t got = 0;
    int rc = v->tts.synth(ph, n, pcm, cap, &got, v->tts.ctx);
    if (rc < 0) return VOICE_ERR_ENGINE;

    /* The synth is an UNTRUSTED ops boundary: a hostile/buggy engine can report
     * got > cap, which would make the caller read pcm[cap..got) out of bounds.
     * Clamp it (voice_listen already does the same on its side). */
    if (got > cap) got = cap;
    if (out_len) *out_len = got;
    v->spoken++;
    return VOICE_OK;
}

voice_result_t voice_listen(voice_t *v, const int16_t *pcm, uint32_t n,
                            char *out_text, uint32_t cap) {
    if (!v || !pcm || !out_text || cap == 0) return VOICE_ERR_ARG;
    out_text[0] = '\0';

    /* FAIL CLOSED: no model, no words. */
    if (!v->stt.recognize) return VOICE_ERR_NO_ENGINE;

    uint8_t ph[VOICE_MAX_PHONEMES];
    uint32_t got = 0;
    int rc = v->stt.recognize(pcm, n, ph, VOICE_MAX_PHONEMES, &got, v->stt.ctx);
    if (rc < 0) return VOICE_ERR_ENGINE;
    if (got > VOICE_MAX_PHONEMES) got = VOICE_MAX_PHONEMES; /* trust nothing */

    /* Render phonemes -> text on-device, bounded, always NUL-terminated. */
    uint32_t w = 0;
    for (uint32_t i = 0; i < got; i++) {
        const char *s = voice_phoneme_str(ph[i]);
        for (uint32_t j = 0; s[j] != '\0'; j++) {
            if (w + 1 >= cap) {          /* leave room for the NUL */
                out_text[w] = '\0';
                return VOICE_ERR_TRUNC;
            }
            out_text[w++] = s[j];
        }
    }
    out_text[w] = '\0';
    v->heard++;
    return VOICE_OK;
}

voice_result_t voice_chiglet_say(voice_t *v, chiglet_t *c,
                                 const surplus_real_t ev[][CHG_DIM], uint32_t k,
                                 int16_t *pcm, uint32_t cap, uint32_t *out_len) {
    if (out_len) *out_len = 0;
    if (!v || !c) return VOICE_ERR_ARG;

    chg_result_t r;
    chg_status_t s = chg_infer(c, ev, k, &r);
    /* Anything short of a clean DECIDED verdict is honest silence. The Chiglet
     * that says "I don't know" must not be given a fabricated sentence. */
    if (s != CHG_OK) return VOICE_ERR_NO_REPLY;
    if (r.state != CHG_DECIDED) return VOICE_ERR_NO_REPLY;
    if (r.label >= c->model.L) return VOICE_ERR_NO_REPLY;

    const char *reply = c->model.label_name[r.label];
    return voice_speak(v, reply, pcm, cap, out_len);
}

const char *voice_result_name(voice_result_t r) {
    switch (r) {
        case VOICE_OK:            return "VOICE_OK";
        case VOICE_ERR_ARG:       return "VOICE_ERR_ARG";
        case VOICE_ERR_NO_ENGINE: return "VOICE_ERR_NO_ENGINE";
        case VOICE_ERR_ENGINE:    return "VOICE_ERR_ENGINE";
        case VOICE_ERR_TRUNC:     return "VOICE_ERR_TRUNC";
        case VOICE_ERR_NO_REPLY:  return "VOICE_ERR_NO_REPLY";
    }
    return "VOICE_ERR_?";
}
