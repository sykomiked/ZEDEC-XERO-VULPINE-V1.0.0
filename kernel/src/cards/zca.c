/* zca.c — the ZXV Card Activation language. See zca.h. */
#include "zca.h"

/* The 38 disciplines, in the order build_card_index.py assigns ids. This is
 * the card system's canonical vocabulary, so it lives with the language. */
static const char *DISCIPLINE[ZCA_DISCIPLINES] = {
    "Acoustomancy","Aeromancy","Alchemy","Astromancy","Biomancy","Botanomancy",
    "Cartomancy","Chronomancy","Cryomancy","Cryptomancy","Crystallomancy",
    "Electromancy","Enochian","Ferromancy","Gravitomancy","Hematomancy",
    "Hydromancy","Illusory Architecture","Invocation","Kinetomancy",
    "Linguamancy","Necromancy","Omni-Channeling","Oneiromancy","Osteomancy",
    "Photomancy","Pneumatomancy","Pyromancy","Sigilcraft","Somnomancy",
    "Sovereign Warding","Spatial Weaving","Symbology","Technomancy",
    "Terramancy","Topomancy","Tycomancy","Umbramancy"
};

const char *zca_discipline_name(uint8_t d) {
    return (d < ZCA_DISCIPLINES) ? DISCIPLINE[d] : "unknown";
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool is_sep(char c) { return c == ' ' || c == '_' || c == '-' || c == '\t'; }

/* Compare ignoring case and separators — OCR is unreliable about both. */
static bool name_eq(const char *a, const char *b, uint32_t blen) {
    uint32_t i = 0, j = 0;
    for (;;) {
        while (a[i] && is_sep(a[i])) i++;
        while (j < blen && is_sep(b[j])) j++;
        bool ae = (a[i] == 0), be = (j >= blen);
        if (ae || be) return ae && be;
        if (lower(a[i]) != lower(b[j])) return false;
        i++; j++;
    }
}

int32_t zca_discipline_id(const char *name, uint32_t len) {
    if (!name) return -1;
    for (uint32_t d = 0; d < ZCA_DISCIPLINES; d++)
        if (name_eq(DISCIPLINE[d], name, len)) return (int32_t)d;
    return -1;
}

/* ------------------------------------------------------------------ CRC16 */
/* CRC16-CCITT. Its job is to make an OCR misread fail loudly rather than
 * quietly activate a different card. */
static uint16_t crc16(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t zca_seal(const zca_program_t *p) {
    if (!p) return 0;
    uint16_t c = 0xFFFFu;
    /* the card index is sealed too, so a program cannot be lifted off one
     * card and replayed as another */
    for (int s = 24; s >= 0; s -= 8) c = crc16(c, (uint8_t)(p->card_index >> s));
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->ins[i].op == ZCA_OP_SEAL) break;
        c = crc16(c, p->ins[i].op);
        c = crc16(c, p->ins[i].a);
        c = crc16(c, (uint8_t)(p->ins[i].b >> 8));
        c = crc16(c, (uint8_t)(p->ins[i].b));
    }
    return c;
}

/* ------------------------------------------------------------- derivation */
/* Turn a card's printed attributes into its canonical program.
 *
 * The discipline picks the primary axis, so two cards of one school point
 * nearly the same way and largely duplicate each other. Gematria and root
 * perturb secondary axes, so a second card from the same school is not a
 * perfect clone but adds far less than a card from a new school. The
 * "collect broadly, not deeply" incentive is this geometry — not a rule
 * written down somewhere and enforced separately. */
zca_status_t zca_derive(const zca_attrs_t *a, zca_program_t *out) {
    if (!a || !out) return ZCA_ERR_ARG;
    out->card_index = a->index;
    out->n = 0;

    uint32_t axis  = (uint32_t)a->discipline % CHG_DIM;
    uint32_t axis2 = ((uint32_t)a->discipline / CHG_DIM + 1u) % CHG_DIM;
    uint32_t p1    = ((uint32_t)a->gematria * 7u + 3u) % CHG_DIM;
    uint32_t p2    = ((uint32_t)a->root * 13u + 5u) % CHG_DIM;

    #define EMIT(o,aa,bb) do { if (out->n >= ZCA_MAX_INS - 1u) return ZCA_ERR_TOO_LONG; \
        out->ins[out->n].op = (uint8_t)(o); out->ins[out->n].a = (uint8_t)(aa); \
        out->ins[out->n].b = (uint16_t)(bb); out->n++; } while (0)

    EMIT(ZCA_OP_AXIS, axis, 1000);
    if (axis2 != axis)                        EMIT(ZCA_OP_TILT, axis2, 550);
    if (p1 != axis && p1 != axis2)            EMIT(ZCA_OP_TILT, p1, 100);
    if (p2 != axis && p2 != axis2 && p2 != p1) EMIT(ZCA_OP_TILT, p2, 70);

    /* what the card permits the companion to do */
    uint32_t g = 1u;                                  /* INFER, always */
    if (a->root >= 5)            g |= 2u;             /* ADVISE  */
    if (a->set  >= 2)            g |= 4u;             /* RECALL  */
    if (a->discipline % 5 == 0)  g |= 8u;             /* COMPOSE */
    EMIT(ZCA_OP_GRANT, 0, g);

    EMIT(ZCA_OP_BIND, ZCA_FIELD_GEM,  a->gematria);
    EMIT(ZCA_OP_BIND, ZCA_FIELD_ROOT, a->root);
    EMIT(ZCA_OP_BIND, ZCA_FIELD_SET,  a->set);
    #undef EMIT

    out->ins[out->n].op = ZCA_OP_SEAL;
    out->ins[out->n].a  = 0;
    out->ins[out->n].b  = zca_seal(out);
    out->n++;
    return ZCA_OK;
}

/* --------------------------------------------------------------- execution */
zca_status_t zca_exec(const zca_program_t *p, card_effect_t *out) {
    if (!p || !out) return ZCA_ERR_ARG;
    for (uint32_t d = 0; d < CHG_DIM; d++) out->evidence[d] = SR_ZERO;
    out->grants = 0;
    for (uint32_t i = 0; i < ZCA_FIELD__MAX; i++) out->attr[i] = 0;

    /* Straight line, executed once each, no jumps: termination is structural. */
    uint32_t n = (p->n <= ZCA_MAX_INS) ? p->n : ZCA_MAX_INS;
    for (uint32_t i = 0; i < n; i++) {
        const zca_ins_t *in = &p->ins[i];
        /* Operands are masked and clamped HERE, once, on the way in. A card
         * cannot name an axis that does not exist or a magnitude above 1.0,
         * so there is nothing downstream left to validate. */
        uint32_t ax   = (uint32_t)in->a % CHG_DIM;
        uint32_t mag  = (in->b > ZCA_MAG_MAX) ? ZCA_MAG_MAX : in->b;
        /* Convert milli-units through the surplus API, NEVER by hand. The
         * two builds use different representations — Q32.32 in the kernel,
         * double on the host — so open-coding `mag * SR_ONE / 1000` yields
         * the right answer on target and silently ZERO on the host, where
         * SR_ONE is 1.0 and the integer divide floors. That bug shipped
         * here once and the tests did not catch it, because they checked
         * grants and attributes but never the evidence values. */
        surplus_real_t v = SR_DIV(SR_FROM_INT((int32_t)mag), SR_FROM_INT(1000));

        switch (in->op) {
        case ZCA_OP_AXIS:  out->evidence[ax] = v; break;
        case ZCA_OP_TILT:  out->evidence[ax] = SR_ADD(out->evidence[ax], v); break;
        case ZCA_OP_GRANT: out->grants |= (in->b & 0x0Fu); break;  /* 4 known bits */
        case ZCA_OP_BIND:
            if (in->a < ZCA_FIELD__MAX) out->attr[in->a] = in->b;
            break;
        case ZCA_OP_SEAL:  return ZCA_OK;          /* terminator */
        case ZCA_OP_NOP:   break;
        default:           break;                  /* unknown op is inert */
        }
    }
    return ZCA_ERR_NO_SEAL;
}

/* ----------------------------------------------------------------- emitter */
static uint32_t put(char *o, uint32_t cap, uint32_t at, const char *s) {
    while (*s && at < cap) o[at++] = *s++;
    return at;
}
static uint32_t put_u(char *o, uint32_t cap, uint32_t at, uint32_t v) {
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n-- && at < cap) o[at++] = t[n];
    return at;
}
static uint32_t put_hex4(char *o, uint32_t cap, uint32_t at, uint16_t v) {
    const char *H = "0123456789ABCDEF";
    for (int s = 12; s >= 0 && at < cap; s -= 4) o[at++] = H[(v >> s) & 0xF];
    return at;
}
static uint32_t put_milli(char *o, uint32_t cap, uint32_t at, uint16_t m) {
    at = put_u(o, cap, at, m / 1000);
    if (at < cap) o[at++] = '.';
    for (uint32_t div = 100; div; div /= 10)
        if (at < cap) o[at++] = (char)('0' + ((m % 1000) / div) % 10);
    return at;
}

static const char *MNEM[ZCA_OP__MAX] = { "NOP", "AXIS", "TILT", "GRANT", "BIND", "SEAL" };
static const char *FIELD[ZCA_FIELD__MAX] = { "?", "GEM", "ROOT", "TAROT", "SET" };

uint32_t zca_emit(const zca_program_t *p, char *o, uint32_t cap) {
    if (!p || !o || !cap) return 0;
    uint32_t at = put(o, cap, 0, "ZCA1 ");
    at = put_u(o, cap, at, p->card_index);
    if (at < cap) o[at++] = '\n';
    for (uint32_t i = 0; i < p->n && i < ZCA_MAX_INS; i++) {
        const zca_ins_t *in = &p->ins[i];
        if (in->op >= ZCA_OP__MAX) return 0;
        at = put(o, cap, at, MNEM[in->op]);
        switch (in->op) {
        case ZCA_OP_AXIS: case ZCA_OP_TILT:
            if (at < cap) o[at++] = ' ';
            at = put_u(o, cap, at, in->a);
            if (at < cap) o[at++] = ' ';
            at = put_milli(o, cap, at, in->b);
            break;
        case ZCA_OP_GRANT:
            if (at < cap) o[at++] = ' ';
            at = put_u(o, cap, at, in->b);
            break;
        case ZCA_OP_BIND:
            if (at < cap) o[at++] = ' ';
            at = put(o, cap, at, (in->a < ZCA_FIELD__MAX) ? FIELD[in->a] : "?");
            if (at < cap) o[at++] = ' ';
            at = put_u(o, cap, at, in->b);
            break;
        case ZCA_OP_SEAL:
            if (at < cap) o[at++] = ' ';
            at = put_hex4(o, cap, at, in->b);
            break;
        default: break;
        }
        if (at < cap) o[at++] = '\n';
    }
    if (at >= cap) return 0;
    o[at] = 0;
    return at;
}

/* ------------------------------------------------------------------ parser */
typedef struct { const char *s; uint32_t len, at; } scan_t;

static void skip_blank(scan_t *z) { while (z->at < z->len && (z->s[z->at]==' '||z->s[z->at]=='\t')) z->at++; }
static bool at_eol(scan_t *z) { return z->at >= z->len || z->s[z->at]=='\n' || z->s[z->at]=='\r'; }
static void skip_line(scan_t *z) {
    while (z->at < z->len && z->s[z->at] != '\n') z->at++;
    if (z->at < z->len) z->at++;
}
/* one bare word */
static uint32_t word(scan_t *z, const char **out) {
    skip_blank(z);
    *out = z->s + z->at;
    uint32_t n = 0;
    while (z->at < z->len && z->s[z->at] > ' ') { z->at++; n++; }
    return n;
}
static bool w_eq(const char *w, uint32_t n, const char *lit) {
    uint32_t i = 0;
    for (; i < n; i++) { if (!lit[i] || lower(w[i]) != lower(lit[i])) return false; }
    return lit[i] == 0;
}
static bool parse_u(const char *w, uint32_t n, uint32_t *out) {
    if (!n) return false;
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return false;
        v = v * 10u + (uint32_t)(w[i] - '0');
        if (v > 0x00FFFFFFu) return false;
    }
    *out = v; return true;
}
/* "1.000" / "0.55" / "1" -> milli */
static bool parse_milli(const char *w, uint32_t n, uint32_t *out) {
    uint32_t whole = 0, frac = 0, fd = 0, i = 0;
    for (; i < n && w[i] != '.'; i++) {
        if (w[i] < '0' || w[i] > '9') return false;
        whole = whole * 10u + (uint32_t)(w[i] - '0');
        if (whole > 1000u) return false;
    }
    if (i < n) {
        i++;
        for (; i < n; i++) {
            if (w[i] < '0' || w[i] > '9') return false;
            if (fd < 3) { frac = frac * 10u + (uint32_t)(w[i] - '0'); fd++; }
        }
    }
    while (fd < 3) { frac *= 10u; fd++; }
    uint32_t m = whole * 1000u + frac;
    if (m > ZCA_MAG_MAX) return false;
    *out = m; return true;
}
static bool parse_hex4(const char *w, uint32_t n, uint32_t *out) {
    if (n == 0 || n > 4) return false;
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = lower(w[i]); uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a') + 10u;
        else return false;
        v = v * 16u + d;
    }
    *out = v; return true;
}

zca_status_t zca_assemble(const char *text, uint32_t len, zca_program_t *out) {
    if (!text || !out) return ZCA_ERR_ARG;
    if (len > ZCA_MAX_TEXT) return ZCA_ERR_TOO_LONG;
    scan_t z = { text, len, 0 };
    out->n = 0; out->card_index = 0;

    /* header */
    for (;;) {
        skip_blank(&z);
        if (z.at >= z.len) return ZCA_ERR_MAGIC;
        if (z.s[z.at] == ';' || at_eol(&z)) { skip_line(&z); continue; }
        break;
    }
    const char *w; uint32_t n = word(&z, &w);
    if (!w_eq(w, n, "ZCA1")) return ZCA_ERR_MAGIC;
    n = word(&z, &w);
    uint32_t idx;
    if (!parse_u(w, n, &idx)) return ZCA_ERR_OPERAND;
    out->card_index = idx;
    skip_line(&z);

    bool sealed = false;
    while (z.at < z.len && !sealed) {
        skip_blank(&z);
        /* ';' lines are the operating instructions printed for the human */
        if (z.at >= z.len) break;
        if (z.s[z.at] == ';' || at_eol(&z)) { skip_line(&z); continue; }

        n = word(&z, &w);
        if (!n) { skip_line(&z); continue; }
        if (out->n >= ZCA_MAX_INS) return ZCA_ERR_TOO_LONG;
        zca_ins_t *in = &out->ins[out->n];
        in->op = ZCA_OP_NOP; in->a = 0; in->b = 0;

        if (w_eq(w, n, "AXIS") || w_eq(w, n, "TILT")) {
            in->op = w_eq(w, n, "AXIS") ? ZCA_OP_AXIS : ZCA_OP_TILT;
            uint32_t ax, mg;
            n = word(&z, &w); if (!parse_u(w, n, &ax)) return ZCA_ERR_OPERAND;
            n = word(&z, &w); if (!parse_milli(w, n, &mg)) return ZCA_ERR_OPERAND;
            in->a = (uint8_t)(ax & 0xFFu); in->b = (uint16_t)mg;
        } else if (w_eq(w, n, "GRANT")) {
            in->op = ZCA_OP_GRANT;
            uint32_t g; n = word(&z, &w);
            if (!parse_u(w, n, &g)) return ZCA_ERR_OPERAND;
            in->b = (uint16_t)g;
        } else if (w_eq(w, n, "BIND")) {
            in->op = ZCA_OP_BIND;
            n = word(&z, &w);
            uint32_t f = 0;
            for (uint32_t k = 1; k < ZCA_FIELD__MAX; k++) if (w_eq(w, n, FIELD[k])) f = k;
            if (!f) return ZCA_ERR_OPERAND;
            uint32_t v; n = word(&z, &w);
            if (!parse_u(w, n, &v)) return ZCA_ERR_OPERAND;
            in->a = (uint8_t)f; in->b = (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
        } else if (w_eq(w, n, "SEAL")) {
            in->op = ZCA_OP_SEAL;
            uint32_t c; n = word(&z, &w);
            if (!parse_hex4(w, n, &c)) return ZCA_ERR_OPERAND;
            in->b = (uint16_t)c;
            sealed = true;
        } else if (w_eq(w, n, "NOP")) {
            in->op = ZCA_OP_NOP;
        } else {
            /* An unrecognised mnemonic means the scan is wrong or the card
             * is from a newer deck. Refuse it — do not guess at intent. */
            return ZCA_ERR_OPCODE;
        }
        out->n++;
        skip_line(&z);
    }

    if (!sealed) return ZCA_ERR_NO_SEAL;
    /* The seal is the whole defence against a garbled scan silently becoming
     * a different, still-valid program. */
    if (out->ins[out->n - 1].b != zca_seal(out)) return ZCA_ERR_SEAL;
    return ZCA_OK;
}

const char *zca_status_name(zca_status_t s) {
    switch (s) {
    case ZCA_OK:           return "OK";
    case ZCA_ERR_ARG:      return "ERR_ARG";
    case ZCA_ERR_MAGIC:    return "ERR_MAGIC";
    case ZCA_ERR_OPCODE:   return "ERR_OPCODE";
    case ZCA_ERR_OPERAND:  return "ERR_OPERAND";
    case ZCA_ERR_TOO_LONG: return "ERR_TOO_LONG";
    case ZCA_ERR_NO_SEAL:  return "ERR_NO_SEAL";
    case ZCA_ERR_SEAL:     return "ERR_SEAL";
    default:               return "?";
    }
}
