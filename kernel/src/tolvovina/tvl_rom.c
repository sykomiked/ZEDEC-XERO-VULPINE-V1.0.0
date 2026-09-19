/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* tvl_rom.c — TOL VOVINA UPAAH LOT container: parse, validate, derive. See tvl_rom.h.
 *
 * Freestanding: no libc, no float, no 64-bit variable division. Every width
 * here is uint32_t and every add/multiply that could wrap goes through
 * __builtin_*_overflow (the same discipline zphi.c uses), so an overflow is a
 * REFUSAL and never a wrapped value that passes a later bounds check. */
#include "tvl_rom.h"

#include "../emu/megarom.h"  /* the existing console cartridge registry      */
#include "../emu/dimfold.h"  /* the section codec: identity, bound, container*/
#include "zorder.h"          /* Morton hold addressing (ZO_MAX_LEVEL/encode2)*/
#include "e8.h"              /* E8_ROOTS — the orientation frame's real size */
#include "sha256.h"          /* streaming SHA-256, for the piecewise seal    */

/* --gc-sections KEEP. Same reasoning (and same spelling) as tvl_geom.c:
 * build_system/verify_banners.sh records the measured fact that adding a file
 * to KERNEL_SRCS is NECESSARY BUT NOT SUFFICIENT — the arm64 link runs
 * -Wl,--gc-sections, so a module nothing references is DISCARDED from the ELF.
 * mrschema.o is in all five arch Makefiles and has zero text symbols in
 * kernel_arm64.elf today for exactly this reason. The container has no
 * in-kernel caller until the boot path adopts it, so its entry points are
 * marked retained. This is a RETENTION statement, not a claim that anything
 * calls it at boot — it does not. Drop it the moment kernel_main calls
 * tvl_rom_selfcheck() directly. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define TVL_KEEP __attribute__((used, retain))
#else
#  define TVL_KEEP __attribute__((used))
#endif

/* ======================================================================== *
 * Small freestanding helpers.
 * ======================================================================== */

static void tvl_bzero(void *p, uint32_t n){
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* Little-endian scalar reads. Written as shift/mask rather than a cast so the
 * container has the SAME byte order on every target and never depends on a
 * __builtin_bswap that does not exist on the 32-bit builds. */
static uint16_t rd16(const uint8_t *p){
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wr32(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

/* Checked 32-bit arithmetic. False means "this did not fit", which is always a
 * refusal — the standing rule is that a security predicate never defaults true,
 * and a wrapped sum is precisely a predicate that defaulted true. */
static bool add32(uint32_t a, uint32_t b, uint32_t *o){
    return !__builtin_add_overflow(a, b, o);
}
static bool mul32(uint32_t a, uint32_t b, uint32_t *o){
    return !__builtin_mul_overflow(a, b, o);
}

static bool digest_eq(const uint8_t *a, const uint8_t *b){
    for (uint32_t i = 0; i < TVL_DIGEST_LEN; i++) if (a[i] != b[i]) return false;
    return true;
}

/* SHA-256d (double SHA-256) over TWO non-contiguous spans. Same construction
 * dimfold_identity uses, taken streaming so the header seal covers the header
 * AND the whole section table with no staging buffer to size wrong — the exact
 * failure sha256.h's own comment documents for the two truncating HMACs. */
static void sha256d_pair(const uint8_t *a, uint32_t alen,
                         const uint8_t *b, uint32_t blen, uint8_t out[32]){
    sha256_ctx_t c; uint8_t t[32];
    sha256_init(&c);
    if (alen) sha256_update(&c, a, (size_t)alen);
    if (blen) sha256_update(&c, b, (size_t)blen);
    sha256_final(&c, t);
    sha256(t, 32, out);
}

/* LEB128 varint reader, bounds-checked against the end of the span. dimfold
 * writes its container lengths this way; this reads them back. It is a READER
 * for an existing encoding, not a second encoding. */
static bool tvl_varint(const uint8_t *in, uint32_t n, uint32_t *pi, uint32_t *out){
    uint32_t v = 0, i = *pi; int shift = 0;
    while (i < n){
        uint8_t b = in[i++];
        v |= (uint32_t)(b & 0x7Fu) << shift;
        if (!(b & 0x80u)){ *pi = i; *out = v; return true; }
        shift += 7;
        if (shift > 28) return false;          /* would not fit in 32 bits */
    }
    return false;                               /* truncated               */
}

/* ======================================================================== *
 * RESIDENT SIZE RECOVERY — the heart of "derived, never declared".
 *
 * Nothing here asks how big a section expands to. Each codec container states
 * its own expanded length as part of the bytes that will be fed to the codec,
 * so the number the loader plans with and the number the decoder will actually
 * produce are THE SAME number. A hostile author cannot inflate one without
 * inflating the other, and dimfold_descend then re-verifies the reconstruction
 * against its own SHA-256d identity before it returns.
 * ======================================================================== */
static bool recover_resident(uint8_t form, const uint8_t *p, uint32_t n,
                             uint32_t *out){
    uint32_t pi, total, nch, sum = 0;

    switch (form){
    case TVL_FORM_RAW:
        *out = n;                       /* verbatim: resident IS stored      */
        return true;

    case TVL_FORM_FOLD:
        /* dimfold_compress emits put_varint(original length) first. */
        pi = 0;
        if (!tvl_varint(p, n, &pi, &total)) return false;
        /* dimfold_compress refuses next_pow2(n) > 1024, so a claimed original
         * length above 1024 names a block that codec would never produce. */
        if (total == 0u || total > 1024u) return false;
        *out = total;
        return true;

    case TVL_FORM_ELEV:
        /* 'Z''X''V''E' | identity[32] | varint(total) | chunks */
        if (n < 4u + 32u + 1u) return false;
        if (!(p[0]=='Z' && p[1]=='X' && p[2]=='V' && p[3]=='E')) return false;
        pi = 4u + 32u;
        if (!tvl_varint(p, n, &pi, &total)) return false;
        if (total == 0u) return false;
        *out = total;
        return true;

    case TVL_FORM_PACK:
        /* 'Z''X''V''M' | varint(nch) | { type | u32 elen | ELEV container }* */
        if (n < 5u) return false;
        if (!(p[0]=='Z' && p[1]=='X' && p[2]=='V' && p[3]=='M')) return false;
        pi = 4u;
        if (!tvl_varint(p, n, &pi, &nch)) return false;
        if (nch == 0u || nch > TVL_MAX_SECTIONS) return false;
        for (uint32_t c = 0; c < nch; c++){
            uint32_t elen, chan_res, next;
            if (!add32(pi, 5u, &next) || next > n) return false;
            pi += 1u;                                   /* channel polarity  */
            elen = rd32(p + pi); pi += 4u;
            if (!add32(pi, elen, &next) || next > n) return false;
            if (!recover_resident(TVL_FORM_ELEV, p + pi, elen, &chan_res)) return false;
            if (!add32(sum, chan_res, &sum)) return false;
            pi = next;
        }
        if (sum == 0u) return false;
        *out = sum;
        return true;

    default:
        return false;                   /* fail closed on an unknown form    */
    }
}

/* ======================================================================== *
 * PARSE + VALIDATE. Assume the image is hostile.
 * ======================================================================== */

TVL_KEEP tvl_error_t tvl_rom_parse(const uint8_t *img, uint32_t len,
                                   uint16_t expect_role, tvl_rom_t *out){
    uint32_t table, dir_end, i, worlds = 0, need, w_off, w_len;
    uint8_t  seal[32];

    if (!out) return TVL_E_NULL;
    /* Zero first: a caller that ignores the return code must not find stale
     * fields from an earlier, valid parse sitting in this struct. */
    tvl_bzero(out, (uint32_t)sizeof *out);

    if (!img || len == 0u)      return TVL_E_NULL;
    if (len < TVL_HDR_SIZE)     return TVL_E_SHORT;

    if (!(img[0]==TVL_MAGIC0 && img[1]==TVL_MAGIC1 &&
          img[2]==TVL_MAGIC2 && img[3]==TVL_MAGIC3)) return TVL_E_MAGIC;

    out->version = rd16(img + 4);
    if (out->version == 0u || out->version > TVL_VERSION) return TVL_E_VERSION;

    out->role = rd16(img + 6);
    if (out->role > (uint16_t)TRI_NEUTRAL) return TVL_E_ROLE;
    /* Cross-check against the triad file this payload came out of. A payload
     * that says S+ inside a .cedec is a mislabelled world, not a small slip. */
    if (expect_role <= (uint16_t)TRI_NEUTRAL && out->role != expect_role)
        return TVL_E_ROLE;

    out->section_count = rd32(img + 8);
    if (out->section_count == 0u || out->section_count > TVL_MAX_SECTIONS)
        return TVL_E_COUNT;

    if (!mul32(out->section_count, TVL_SEC_SIZE, &table)) return TVL_E_OVERFLOW;
    if (!add32(TVL_HDR_SIZE, table, &dir_end))            return TVL_E_OVERFLOW;
    if (dir_end > len)                                    return TVL_E_SHORT;

    /* HEADER SEAL, checked BEFORE any header field is believed. It covers the
     * header (minus the seal itself) and the entire section table, so an edited
     * offset cannot be laundered by editing the field next to it.
     * Honest scope: four bytes is a CHEAP EARLY REJECT, not this container's
     * integrity boundary. That boundary is the zxvfs_tri descriptor's full
     * 32-byte per-role digests, written last and quarantined across reboot. */
    sha256d_pair(img, 56u, img + TVL_HDR_SIZE, dir_end - TVL_HDR_SIZE, seal);
    for (i = 0; i < 4u; i++) if (img[56u + i] != seal[i]) return TVL_E_SEAL;

    /* Header fields, each bounded by something REAL rather than a chosen cap:
     * the E8 root system's actual size, the Morton addressing depth, and the
     * block codec's own 1024-coefficient limit. */
    out->orient_root = rd32(img + 12);
    if (out->orient_root >= E8_ROOTS) return TVL_E_FIELD;
    out->hoard_level = rd32(img + 16);
    if (out->hoard_level > (uint32_t)ZO_MAX_LEVEL) return TVL_E_FIELD;
    out->chunk_log2 = rd32(img + 20);
    if (out->chunk_log2 < TVL_CHUNK_LOG2_MIN || out->chunk_log2 > TVL_CHUNK_LOG2_MAX)
        return TVL_E_FIELD;
    for (i = 0; i < TVL_WITNESS_LEN; i++) out->iws_witness[i] = img[24u + i];

    out->image     = img;
    out->image_len = len;

    /* ---- pass 1: every directory entry, field by field and byte by byte --- */
    for (i = 0; i < out->section_count; i++){
        const uint8_t *e = img + TVL_HDR_SIZE + i * TVL_SEC_SIZE;
        tvl_section_t *s = &out->sec[i];
        uint32_t end, j;
        uint8_t  calc[32];

        s->kind      = rd16(e + 0);
        s->form      = e[2];
        s->residency = e[3];
        s->channel   = rd16(e + 4);
        s->offset    = rd32(e + 8);
        s->stored    = rd32(e + 12);
        for (j = 0; j < TVL_DIGEST_LEN; j++) s->digest[j] = e[16 + j];

        if (s->kind >= (uint16_t)TVL_SEC_KIND_COUNT) return TVL_E_SEC_FIELD;
        if (s->form >= (uint8_t)TVL_FORM_COUNT)      return TVL_E_SEC_FIELD;
        if (s->residency >= (uint8_t)TVL_RES_COUNT)  return TVL_E_SEC_FIELD;
        if (rd16(e + 6) != 0u)                       return TVL_E_SEC_FIELD;
        if (s->stored == 0u)                         return TVL_E_SEC_FIELD;
        if (s->channel >= TVL_MAX_SECTIONS)          return TVL_E_SEC_FIELD;
        /* One section per dimfold_pack channel. Two entries naming the same
         * channel would make reassembly ambiguous, and an ambiguous manifest is
         * how a loader ends up reading one section's bytes as another's. */
        for (j = 0; j < i; j++)
            if (out->sec[j].channel == s->channel) return TVL_E_SEC_FIELD;

        /* offset+size overflow FIRST: a wrapped end looks small and would then
         * sail through the "inside the image" test below. */
        if (!add32(s->offset, s->stored, &end))      return TVL_E_OVERFLOW;
        if (s->stored > 0x7FFFFFFFu)                 return TVL_E_BOUNDS; /* int APIs */
        if (s->offset < dir_end)                     return TVL_E_OVERLAP;
        if (end > len)                               return TVL_E_BOUNDS;

        for (j = 0; j < i; j++){
            uint32_t jend = out->sec[j].offset + out->sec[j].stored; /* checked above */
            if (!(end <= out->sec[j].offset || s->offset >= jend))
                return TVL_E_OVERLAP;
        }

        dimfold_identity(img + s->offset, (int)s->stored, calc);
        if (!digest_eq(calc, s->digest)) return TVL_E_DIGEST;

        if (s->kind == (uint16_t)TVL_SEC_WORLD){ worlds++; out->world_index = i; }
    }

    /* ---- pass 2: the WORLD section, structurally -------------------------- */
    /* Exactly one, stored verbatim, and resident for the session.
     * RAW is not a convenience: the working set cannot be sized without the
     * hoard adjacency, and the adjacency cannot be decompressed without a
     * working buffer whose size is part of that working set. Storing the map
     * verbatim is what breaks that circle. */
    if (worlds != 1u) return TVL_E_WORLD;
    if (out->sec[out->world_index].form != (uint8_t)TVL_FORM_RAW)        return TVL_E_WORLD;
    if (out->sec[out->world_index].residency != (uint8_t)TVL_RES_PERSISTENT)
        return TVL_E_WORLD;

    /* ---- pass 3: recover every section's resident size from its own bytes - */
    for (i = 0; i < out->section_count; i++){
        tvl_section_t *s = &out->sec[i];
        if (!recover_resident(s->form, img + s->offset, s->stored, &s->resident))
            return TVL_E_RESIDENT;
    }

    /* ---- pass 4: the hoard ------------------------------------------------ */
    w_off = out->sec[out->world_index].offset;
    w_len = out->sec[out->world_index].stored;
    if (w_len < TVL_HOARD_HDR) return TVL_E_HOARD;
    {
        const uint8_t *w = img + w_off;
        uint32_t holds_bytes, gates_bytes, g;

        if (!(w[0]==TVL_HOARD_MAGIC0 && w[1]==TVL_HOARD_MAGIC1 &&
              w[2]==TVL_HOARD_MAGIC2 && w[3]==TVL_HOARD_MAGIC3)) return TVL_E_HOARD;

        out->hold_count = rd32(w + 4);
        out->gate_count = rd32(w + 8);
        if (out->hold_count == 0u || out->hold_count > TVL_MAX_HOLDS) return TVL_E_HOARD;
        if (out->gate_count > TVL_MAX_GATES)                          return TVL_E_HOARD;

        if (!mul32(out->hold_count, TVL_HOLD_SIZE, &holds_bytes)) return TVL_E_OVERFLOW;
        if (!mul32(out->gate_count, TVL_GATE_SIZE, &gates_bytes)) return TVL_E_OVERFLOW;
        if (!add32(TVL_HOARD_HDR, holds_bytes, &need))            return TVL_E_OVERFLOW;
        if (!add32(need, gates_bytes, &need))                     return TVL_E_OVERFLOW;
        /* EXACT, not "at least". Slack inside the map is unaccounted bytes the
         * loader would carry and never read; refusing it keeps the derived
         * W_core equal to what the world actually costs. */
        if (need != w_len) return TVL_E_HOARD;

        out->hold_off = w_off + TVL_HOARD_HDR;
        out->gate_off = out->hold_off + holds_bytes;

        for (i = 0; i < out->hold_count; i++){
            const uint8_t *h = img + out->hold_off + i * TVL_HOLD_SIZE;
            uint32_t morton = rd32(h + 0);
            uint8_t  level  = h[4];
            uint8_t  role   = h[5];
            uint16_t sec    = rd16(h + 6);
            uint16_t hx = 0, hy = 0;

            /* One address space per hoard: a hold at a different Morton level
             * would break zo_contains as the frontier test. */
            if ((uint32_t)level != out->hoard_level) return TVL_E_HOARD;
            if (role > (uint8_t)TRI_NEUTRAL)         return TVL_E_HOARD;

            /* The address must actually live at that level, and must survive a
             * Morton round-trip — junk in the high bit-pairs would silently
             * relocate a hold in the hierarchy. */
            zo_decode2(morton, &hx, &hy);
            if (level < ZO_MAX_LEVEL){
                if ((uint32_t)hx >= (1u << level) || (uint32_t)hy >= (1u << level))
                    return TVL_E_HOARD;
            }
            if (zo_encode2(hx, hy) != morton) return TVL_E_HOARD;

            for (g = 0; g < i; g++){
                const uint8_t *o = img + out->hold_off + g * TVL_HOLD_SIZE;
                if (rd32(o + 0) == morton) return TVL_E_HOARD;   /* two holds, one place */
            }

            if (sec != TVL_NO_SECTION){
                if ((uint32_t)sec >= out->section_count) return TVL_E_HOARD;
                /* A hold's body is what the frontier PAGES. A persistent
                 * section is already in W_core, so letting a hold name one
                 * would double-count it in W_stream. */
                if (out->sec[sec].residency != (uint8_t)TVL_RES_STREAMED)
                    return TVL_E_HOARD;
            }
        }

        for (i = 0; i < out->gate_count; i++){
            const uint8_t *e = img + out->gate_off + i * TVL_GATE_SIZE;
            uint16_t from = rd16(e + 0), to = rd16(e + 2);
            if ((uint32_t)from >= out->hold_count) return TVL_E_HOARD;
            if ((uint32_t)to   >= out->hold_count) return TVL_E_HOARD;
            if (from == to)                        return TVL_E_HOARD; /* self-gate */
            if (e[6] > 1u)                         return TVL_E_HOARD; /* visible_only */
            if (e[7] != 0u)                        return TVL_E_HOARD; /* reserved     */
            for (g = 0; g < i; g++){
                const uint8_t *o = img + out->gate_off + g * TVL_GATE_SIZE;
                /* A duplicate directed gate would count its target twice in the
                 * frontier and overstate the working set. */
                if (rd16(o + 0) == from && rd16(o + 2) == to) return TVL_E_HOARD;
            }
        }
    }

    out->valid = true;
    return TVL_OK;
}

TVL_KEEP bool tvl_rom_hold(const tvl_rom_t *r, uint32_t i, tvl_hold_t *out){
    if (!r || !r->valid || !out || i >= r->hold_count) return false;
    {
        const uint8_t *h = r->image + r->hold_off + i * TVL_HOLD_SIZE;
        out->morton  = rd32(h + 0);
        out->level   = h[4];
        out->role    = h[5];
        out->section = rd16(h + 6);
    }
    return true;
}

TVL_KEEP bool tvl_rom_gate(const tvl_rom_t *r, uint32_t i, tvl_gate_t *out){
    if (!r || !r->valid || !out || i >= r->gate_count) return false;
    {
        const uint8_t *e = r->image + r->gate_off + i * TVL_GATE_SIZE;
        out->from         = rd16(e + 0);
        out->to           = rd16(e + 2);
        out->cost         = rd16(e + 4);
        out->visible_only = e[6];
    }
    return true;
}

/* ======================================================================== *
 * THE DERIVED WORKING SET.
 * ======================================================================== */

/* r(v): what a hold's body costs resident. Zero for a hold with no body — a
 * place you can walk into that streams nothing is free, and saying so is more
 * honest than charging it a nominal fee. */
static uint32_t hold_resident(const tvl_rom_t *r, uint32_t v){
    const uint8_t *h = r->image + r->hold_off + v * TVL_HOLD_SIZE;
    uint16_t sec = rd16(h + 6);
    if (sec == TVL_NO_SECTION) return 0u;
    return r->sec[sec].resident;
}

/* W_core: everything that must be resident for the whole session. */
static bool derive_core(const tvl_rom_t *r, uint32_t *out){
    uint32_t sum = 0;
    for (uint32_t i = 0; i < r->section_count; i++)
        if (r->sec[i].residency == (uint8_t)TVL_RES_PERSISTENT)
            if (!add32(sum, r->sec[i].resident, &sum)) return false;
    *out = sum;
    return true;
}

/* W_stream: the MAXIMUM LIVE SET, which is a graph property, not a total.
 *
 * Standing at hold v the loader must hold v's own body plus the bodies of
 * everywhere reachable in one step, so the player never waits at a gate. The
 * requirement is therefore the WORST such neighbourhood over the whole hoard —
 * a MAX, never a sum over all holds. Summing every hold would demand the entire
 * world be resident at once, which is exactly the cartridge model this project
 * is not building. The gate list is scanned per hold (V*E, both bounded, no
 * scratch array), and duplicate directed gates were already refused at parse so
 * nothing is counted twice. */
static bool derive_stream(const tvl_rom_t *r, uint32_t *out){
    uint32_t best = 0;
    for (uint32_t v = 0; v < r->hold_count; v++){
        uint32_t frontier = hold_resident(r, v);
        for (uint32_t g = 0; g < r->gate_count; g++){
            const uint8_t *e = r->image + r->gate_off + g * TVL_GATE_SIZE;
            if ((uint32_t)rd16(e + 0) != v) continue;
            if (!add32(frontier, hold_resident(r, (uint32_t)rd16(e + 2)), &frontier))
                return false;
        }
        if (frontier > best) best = frontier;
    }
    *out = best;
    return true;
}

/* W_work: decompression scratch. One chunk in, one worst-case coded chunk out.
 * This is a small CONSTANT rather than O(section size) precisely because the
 * codec chunks — dimfold_elevate walks the payload a block at a time — and
 * dimfold_bound() is the codec's own statement of its worst-case expansion, so
 * the margin is read off the codec rather than guessed at here. */
static bool derive_work(const tvl_rom_t *r, uint32_t *out){
    uint32_t chunk = 1u << r->chunk_log2;      /* chunk_log2 <= 10 at parse  */
    int bound = dimfold_bound((int)chunk);
    if (bound <= 0) return false;
    return add32(chunk, (uint32_t)bound, out);
}

TVL_KEEP uint32_t tvl_rom_ram_required(const tvl_rom_t *r){
    uint32_t core, stream, work, sum;
    /* Fail closed: an image we refused gets no working-set claim at all. A
     * valid ROM always has work >= 2 + dimfold_bound(2) > 0, so 0 is an
     * unambiguous refusal value and never a legitimate answer. */
    if (!r || !r->valid) return 0u;
    if (!derive_core(r, &core))     return 0u;
    if (!derive_stream(r, &stream)) return 0u;
    if (!derive_work(r, &work))     return 0u;
    if (!add32(core, stream, &sum)) return 0u;
    if (!add32(sum, work, &sum))    return 0u;
    return sum;
}

TVL_KEEP bool tvl_rom_iws(const tvl_rom_t *r, const tvl_iws_env_t *env,
                          tvl_iws_t *out){
    uint32_t px, bytes, total;
    if (!out) return false;
    tvl_bzero(out, (uint32_t)sizeof *out);
    if (!r || !r->valid || !env) { out->overflow = true; return false; }

    if (!derive_core(r, &out->core))     { out->overflow = true; return false; }
    if (!derive_stream(r, &out->stream)) { out->overflow = true; return false; }
    if (!derive_work(r, &out->work))     { out->overflow = true; return false; }

    /* W_frame is ASKED OF THE DISPLAY, never assumed. There is no default size
     * here on purpose: a default would be the fixed value this project forbids,
     * and it is the same defect as a projection routine carrying 1920x1080. */
    if (!mul32(env->display_w, env->display_h, &px))       { out->overflow = true; return false; }
    if (!mul32(px, env->bytes_per_px, &bytes))             { out->overflow = true; return false; }
    if (!mul32(bytes, env->buffers, &out->frame))          { out->overflow = true; return false; }
    out->ai = env->ai_bytes;

    if (!add32(out->core, out->stream, &total)) { out->overflow = true; return false; }
    if (!add32(total, out->work,  &total))      { out->overflow = true; return false; }
    if (!add32(total, out->frame, &total))      { out->overflow = true; return false; }
    if (!add32(total, out->ai,    &total))      { out->overflow = true; return false; }
    out->total = total;
    return true;
}

TVL_KEEP void tvl_rom_witness_of(const tvl_rom_t *r, const tvl_iws_env_t *env,
                                 uint8_t out[TVL_WITNESS_LEN]){
    uint8_t enc[24];
    tvl_iws_t w;
    if (!out) return;
    tvl_bzero(out, TVL_WITNESS_LEN);
    if (!tvl_rom_iws(r, env, &w)) return;      /* zeros == "no witness"      */
    wr32(enc +  0, w.core);   wr32(enc +  4, w.stream);
    wr32(enc +  8, w.work);   wr32(enc + 12, w.frame);
    wr32(enc + 16, w.ai);     wr32(enc + 20, w.total);
    dimfold_identity(enc, (int)sizeof enc, out);
}

TVL_KEEP bool tvl_rom_witness_agrees(const tvl_rom_t *r, const tvl_iws_env_t *env){
    uint8_t mine[TVL_WITNESS_LEN];
    if (!r || !r->valid) return false;
    tvl_rom_witness_of(r, env, mine);
    return digest_eq(mine, r->iws_witness);
}

/* ======================================================================== *
 * REGISTRATION through the EXISTING console registry.
 * ======================================================================== */

static const tvl_rom_t *g_active;
static int      g_slot  = -1;
static uint32_t g_boots;

/* The activation hook. It records that the cartridge was made the active layer
 * and nothing else. Say plainly what that means: this does NOT put a world on
 * screen. Wiring the hoard to a renderer is a separate piece of work and is not
 * done in this file — an activation hook that quietly did nothing while a
 * banner claimed a playable world would be the exact overstatement this tree
 * has a gate against. */
static void tvl_boot(void){ g_boots++; }

TVL_KEEP int tvl_rom_register(const tvl_rom_t *r, const char *name,
                              const char *tagline){
    megarom_t m;
    if (!r || !r->valid || !name) return -1;
    if (g_slot >= 0) return -1;        /* ONE cartridge for the whole world */
    m.name = name; m.tagline = tagline; m.kind = MR_KIND_GAME; m.boot = tvl_boot;
    {
        int slot = megarom_register(&m);
        if (slot < 0) return -1;
        g_active = r;
        g_slot   = slot;
        return slot;
    }
}

TVL_KEEP const tvl_rom_t *tvl_rom_active(void){ return g_active; }

/* ======================================================================== *
 * SELF-CHECK.
 *
 * One valid ROM, built in memory, whose derived RAM figure is compared against
 * a number worked out by hand below — then a battery of malformed ROMs, each of
 * which must be REFUSED with the right reason. The rejection half is the half
 * that matters: a parser that accepts good input is ordinary, a parser that
 * refuses hostile input is the point.
 * ======================================================================== */

#define TVL_PROBE_CAP 2048u

static uint8_t s_probe[TVL_PROBE_CAP];   /* the pristine reference image     */
static uint8_t s_work [TVL_PROBE_CAP];   /* the copy each mutation runs on   */
static uint8_t s_tmp  [1024];            /* codec staging                    */

static uint32_t s_probe_len;

/* Recompute only the header seal (bytes 56..59). Deliberately does NOT touch
 * section digests, so a mutation test can corrupt an offset into the weeds
 * without this helper following the pointer. */
static void seal_header(uint8_t *b, uint32_t count){
    uint8_t d[32];
    uint32_t dir_end = TVL_HDR_SIZE + count * TVL_SEC_SIZE;
    sha256d_pair(b, 56u, b + TVL_HDR_SIZE, dir_end - TVL_HDR_SIZE, d);
    for (uint32_t i = 0; i < 4u; i++) b[56u + i] = d[i];
}

/* Recompute every section digest and then the header seal. Only safe while the
 * section extents are still inside the image — used for tests that mutate
 * PAYLOAD content and want the failure to land on the semantic check rather
 * than on the digest. */
static void reseal_all(uint8_t *b){
    uint32_t count = rd32(b + 8);
    for (uint32_t i = 0; i < count; i++){
        uint8_t *e = b + TVL_HDR_SIZE + i * TVL_SEC_SIZE;
        dimfold_identity(b + rd32(e + 8), (int)rd32(e + 12), e + 16);
    }
    seal_header(b, count);
}

static uint8_t *sec_entry(uint8_t *b, uint32_t i){
    return b + TVL_HDR_SIZE + i * TVL_SEC_SIZE;
}

/* Build the reference probe. Five sections in three different STORAGE FORMS,
 * because the whole claim of this module is that the derived RAM figure depends
 * on CONTENT and not on how that content happens to be packed: sections 2 and 4
 * are compressed, their stored lengths are whatever the codec produced, and the
 * expected working set below does not mention those lengths at all. */
static uint32_t build_probe(uint8_t *b, uint32_t cap){
    uint32_t cur, i, wl;
    uint8_t  raw[128];
    int      n;

    if (cap < 1024u) return 0;
    for (i = 0; i < cap; i++) b[i] = 0;

    b[0]=TVL_MAGIC0; b[1]=TVL_MAGIC1; b[2]=TVL_MAGIC2; b[3]=TVL_MAGIC3;
    wr16(b + 4, (uint16_t)TVL_VERSION);
    wr16(b + 6, (uint16_t)TRI_POSITIVE);      /* this is the .zxvc face      */
    wr32(b + 8, 5u);                          /* section_count               */
    wr32(b + 12, 17u);                        /* orient_root, < E8_ROOTS     */
    wr32(b + 16, 4u);                         /* hoard_level                 */
    wr32(b + 20, 9u);                         /* chunk_log2 -> 512-byte chunk*/
    /* iws_witness left zero: the reference build carries no witness, which the
     * checks below rely on to prove a zero witness never "agrees". */

    cur = TVL_HDR_SIZE + 5u * TVL_SEC_SIZE;   /* = 300, first payload byte   */

    /* ---- section 0: WORLD, RAW, PERSISTENT -------------------------------- */
    {
        uint8_t *w = b + cur;
        uint32_t p;
        w[0]=TVL_HOARD_MAGIC0; w[1]=TVL_HOARD_MAGIC1;
        w[2]=TVL_HOARD_MAGIC2; w[3]=TVL_HOARD_MAGIC3;
        wr32(w + 4, 3u);                      /* three holds                 */
        wr32(w + 8, 3u);                      /* three gates                 */
        p = TVL_HOARD_HDR;
        /* hold 0 — S+, protrudes, body = section 3 (64 B resident)          */
        wr32(w + p, zo_encode2(0, 0)); w[p+4]=4; w[p+5]=(uint8_t)TRI_POSITIVE; wr16(w+p+6, 3u); p += 8;
        /* hold 1 — S-, recedes,   body = section 4 (96 B resident)          */
        wr32(w + p, zo_encode2(1, 0)); w[p+4]=4; w[p+5]=(uint8_t)TRI_NEGATIVE; wr16(w+p+6, 4u); p += 8;
        /* hold 2 — S0, screen plane, held: visible, no body, not enterable  */
        wr32(w + p, zo_encode2(0, 1)); w[p+4]=4; w[p+5]=(uint8_t)TRI_NEUTRAL;  wr16(w+p+6, TVL_NO_SECTION); p += 8;
        /* gates: 0->1, 0->2 (visible only), 1->2                            */
        wr16(w+p,0); wr16(w+p+2,1); wr16(w+p+4,1); w[p+6]=0; w[p+7]=0; p += 8;
        wr16(w+p,0); wr16(w+p+2,2); wr16(w+p+4,1); w[p+6]=1; w[p+7]=0; p += 8;
        wr16(w+p,1); wr16(w+p+2,2); wr16(w+p+4,1); w[p+6]=0; w[p+7]=0; p += 8;
        wl = p;                                /* 12 + 24 + 24 = 60          */
        {
            uint8_t *e = sec_entry(b, 0);
            wr16(e+0, (uint16_t)TVL_SEC_WORLD); e[2]=(uint8_t)TVL_FORM_RAW;
            e[3]=(uint8_t)TVL_RES_PERSISTENT;   wr16(e+4, 0u); wr16(e+6, 0u);
            wr32(e+8, cur); wr32(e+12, wl);
        }
        cur += wl;
    }

    /* ---- section 1: MECH, RAW, PERSISTENT, 32 B --------------------------- */
    {
        uint8_t *e = sec_entry(b, 1);
        for (i = 0; i < 32u; i++) b[cur + i] = (uint8_t)(0x40u + (i & 7u));
        wr16(e+0,(uint16_t)TVL_SEC_MECH); e[2]=(uint8_t)TVL_FORM_RAW;
        e[3]=(uint8_t)TVL_RES_PERSISTENT; wr16(e+4,1u); wr16(e+6,0u);
        wr32(e+8, cur); wr32(e+12, 32u);
        cur += 32u;
    }

    /* ---- section 2: SUTRA, ELEV, PERSISTENT, 48 B of program -------------- */
    {
        uint8_t *e = sec_entry(b, 2);
        for (i = 0; i < 48u; i++) raw[i] = (uint8_t)((i < 24u) ? 'S' : 'P');
        n = dimfold_elevate(raw, 48, s_tmp, (int)sizeof s_tmp);
        if (n <= 0) return 0;
        if (cur + (uint32_t)n > cap) return 0;
        for (i = 0; i < (uint32_t)n; i++) b[cur + i] = s_tmp[i];
        wr16(e+0,(uint16_t)TVL_SEC_SUTRA); e[2]=(uint8_t)TVL_FORM_ELEV;
        e[3]=(uint8_t)TVL_RES_PERSISTENT;  wr16(e+4,2u); wr16(e+6,0u);
        wr32(e+8, cur); wr32(e+12, (uint32_t)n);
        cur += (uint32_t)n;
    }

    /* ---- section 3: ASSET, RAW, STREAMED, 64 B (hold 0's body) ------------ */
    {
        uint8_t *e = sec_entry(b, 3);
        if (cur + 64u > cap) return 0;
        for (i = 0; i < 64u; i++) b[cur + i] = (uint8_t)(i * 3u + 1u);
        wr16(e+0,(uint16_t)TVL_SEC_ASSET); e[2]=(uint8_t)TVL_FORM_RAW;
        e[3]=(uint8_t)TVL_RES_STREAMED;    wr16(e+4,3u); wr16(e+6,0u);
        wr32(e+8, cur); wr32(e+12, 64u);
        cur += 64u;
    }

    /* ---- section 4: ASSET, FOLD, STREAMED, 96 B (hold 1's body) ----------- */
    {
        uint8_t *e = sec_entry(b, 4);
        for (i = 0; i < 96u; i++) raw[i] = (uint8_t)((i < 48u) ? 0x11u : 0x22u);
        n = dimfold_compress(raw, 96, s_tmp, (int)sizeof s_tmp);
        if (n <= 0) return 0;
        if (cur + (uint32_t)n > cap) return 0;
        for (i = 0; i < (uint32_t)n; i++) b[cur + i] = s_tmp[i];
        wr16(e+0,(uint16_t)TVL_SEC_ASSET); e[2]=(uint8_t)TVL_FORM_FOLD;
        e[3]=(uint8_t)TVL_RES_STREAMED;    wr16(e+4,4u); wr16(e+6,0u);
        wr32(e+8, cur); wr32(e+12, (uint32_t)n);
        cur += (uint32_t)n;
    }

    reseal_all(b);
    return cur;
}

static void probe_copy(void){
    for (uint32_t i = 0; i < s_probe_len; i++) s_work[i] = s_probe[i];
}

/* Every rejection test: copy the pristine image, break one thing, and demand
 * BOTH that the parse refused and that it refused for the stated reason. */
static int reject(void (*mutate)(uint8_t *), uint32_t len, tvl_error_t want){
    tvl_rom_t r;
    tvl_error_t got;
    probe_copy();
    if (mutate) mutate(s_work);
    got = tvl_rom_parse(s_work, len, (uint16_t)TRI_POSITIVE, &r);
    if (got != want) return 0;
    if (r.valid)     return 0;                 /* never valid on a refusal   */
    if (tvl_rom_ram_required(&r) != 0u) return 0;  /* and no working set claimed */
    return 1;
}

static void m_magic  (uint8_t *b){ b[0] = 'X'; }
static void m_version(uint8_t *b){ wr16(b + 4, (uint16_t)(TVL_VERSION + 1u)); seal_header(b, rd32(b+8)); }
static void m_role   (uint8_t *b){ wr16(b + 6, 3u); seal_header(b, rd32(b+8)); }
static void m_cnt0   (uint8_t *b){ wr32(b + 8, 0u); }
static void m_cntbig (uint8_t *b){ wr32(b + 8, TVL_MAX_SECTIONS + 1u); }
static void m_seal   (uint8_t *b){ b[24] ^= 0xFFu; }        /* witness byte, seal only */
static void m_root   (uint8_t *b){ wr32(b + 12, E8_ROOTS); seal_header(b, rd32(b+8)); }
static void m_level  (uint8_t *b){ wr32(b + 16, (uint32_t)ZO_MAX_LEVEL + 1u); seal_header(b, rd32(b+8)); }
static void m_chunk  (uint8_t *b){ wr32(b + 20, TVL_CHUNK_LOG2_MAX + 1u); seal_header(b, rd32(b+8)); }

/* offset = UINT32_MAX, so offset+stored wraps for ANY non-zero stored length —
 * the test does not depend on how large the codec happened to make the section. */
static void m_ovf    (uint8_t *b){ wr32(sec_entry(b,4) + 8, 0xFFFFFFFFu); seal_header(b, rd32(b+8)); }
static void m_outside(uint8_t *b){ wr32(sec_entry(b,3) + 8, 0xFFFF0000u); seal_header(b, rd32(b+8)); }
static void m_inhdr  (uint8_t *b){ wr32(sec_entry(b,1) + 8, 100u);        seal_header(b, rd32(b+8)); }
static void m_overlap(uint8_t *b){
    /* slide section 4 back so it lands inside section 3's extent */
    uint8_t *e3 = sec_entry(b,3), *e4 = sec_entry(b,4);
    wr32(e4 + 8, rd32(e3 + 8) + 8u);
    seal_header(b, rd32(b+8));
}
static void m_chandup(uint8_t *b){ wr16(sec_entry(b,4) + 4, 3u); seal_header(b, rd32(b+8)); }
static void m_resv   (uint8_t *b){ wr16(sec_entry(b,2) + 6, 1u); seal_header(b, rd32(b+8)); }
static void m_zerolen(uint8_t *b){ wr32(sec_entry(b,1) + 12, 0u); seal_header(b, rd32(b+8)); }
static void m_tamper (uint8_t *b){ b[rd32(sec_entry(b,3) + 8)] ^= 0x01u; }  /* payload, no reseal */
static void m_badform(uint8_t *b){ sec_entry(b,2)[2] = (uint8_t)TVL_FORM_COUNT; seal_header(b, rd32(b+8)); }

static void m_noworld(uint8_t *b){ wr16(sec_entry(b,0) + 0, (uint16_t)TVL_SEC_ASSET); seal_header(b, rd32(b+8)); }
static void m_2world (uint8_t *b){ wr16(sec_entry(b,1) + 0, (uint16_t)TVL_SEC_WORLD); seal_header(b, rd32(b+8)); }
static void m_wfold  (uint8_t *b){ sec_entry(b,0)[2] = (uint8_t)TVL_FORM_ELEV; seal_header(b, rd32(b+8)); }
static void m_wstream(uint8_t *b){ sec_entry(b,0)[3] = (uint8_t)TVL_RES_STREAMED; seal_header(b, rd32(b+8)); }

/* codec container corruption: the ELEV magic goes, so the expanded length can
 * no longer be recovered from the payload -- and nothing else may supply it. */
static void m_badelev(uint8_t *b){ b[rd32(sec_entry(b,2) + 8)] = 'X'; reseal_all(b); }

static uint8_t *world_ptr(uint8_t *b){ return b + rd32(sec_entry(b,0) + 8); }
static void m_hmagic (uint8_t *b){ world_ptr(b)[0] = 'X';                 reseal_all(b); }
static void m_hholds (uint8_t *b){ wr32(world_ptr(b) + 4, TVL_MAX_HOLDS + 1u); reseal_all(b); }
static void m_hsize  (uint8_t *b){ wr32(world_ptr(b) + 8, 2u);            reseal_all(b); } /* gate_count now disagrees with the section length */
static void m_hlevel (uint8_t *b){ world_ptr(b)[TVL_HOARD_HDR + 4] = 5u;  reseal_all(b); }
static void m_hdup   (uint8_t *b){ wr32(world_ptr(b) + TVL_HOARD_HDR + TVL_HOLD_SIZE, zo_encode2(0,0)); reseal_all(b); }
static void m_hsec   (uint8_t *b){ wr16(world_ptr(b) + TVL_HOARD_HDR + 6, 9u); reseal_all(b); }
static void m_hpersist(uint8_t *b){ wr16(world_ptr(b) + TVL_HOARD_HDR + 6, 1u); reseal_all(b); } /* names a PERSISTENT section */
static void m_gedge  (uint8_t *b){ wr16(world_ptr(b) + TVL_HOARD_HDR + 3u*TVL_HOLD_SIZE + 2, 9u); reseal_all(b); }
static void m_gself  (uint8_t *b){ wr16(world_ptr(b) + TVL_HOARD_HDR + 3u*TVL_HOLD_SIZE + 2, 0u); reseal_all(b); }
static void m_gresv  (uint8_t *b){ world_ptr(b)[TVL_HOARD_HDR + 3u*TVL_HOLD_SIZE + 7] = 1u; reseal_all(b); }
static void m_gdup   (uint8_t *b){
    uint8_t *g = world_ptr(b) + TVL_HOARD_HDR + 3u*TVL_HOLD_SIZE;
    wr16(g + 2u*TVL_GATE_SIZE + 0, rd16(g + 0));      /* gate 2 := gate 0 */
    wr16(g + 2u*TVL_GATE_SIZE + 2, rd16(g + 2));
    reseal_all(b);
}

TVL_KEEP int tvl_rom_selfcheck(void){
    /* STATIC, not a local: tvl_rom_register stores this pointer in g_active,
     * and a registry pointing into a returned stack frame would be a dangling
     * read the moment anything booted it. GCC's -Wdangling-pointer caught
     * exactly that; the fix is the lifetime, not the warning. The whole probe
     * is single-threaded bring-up code and already keeps its images in statics. */
    static tvl_rom_t r;
    tvl_iws_t iws;
    tvl_iws_env_t env;
    tvl_hold_t hd;
    tvl_gate_t gt;
    uint32_t ram, expect_core, expect_stream, expect_work, expect;
    int ok = 1;

    s_probe_len = build_probe(s_probe, TVL_PROBE_CAP);
    if (s_probe_len == 0u) return 0;

    /* ---- 1. the valid ROM parses ----------------------------------------- */
    if (tvl_rom_parse(s_probe, s_probe_len, (uint16_t)TRI_POSITIVE, &r) != TVL_OK) return 0;
    if (!r.valid) return 0;
    if (r.section_count != 5u || r.hold_count != 3u || r.gate_count != 3u) ok = 0;
    if (r.world_index != 0u) ok = 0;

    /* resident sizes were RECOVERED from each section's own container, not
     * read from any declared field: 48 out of the ELEV header, 96 out of the
     * FOLD stream's leading varint, the rest verbatim. */
    if (r.sec[0].resident != 60u) ok = 0;
    if (r.sec[1].resident != 32u) ok = 0;
    if (r.sec[2].resident != 48u) ok = 0;   /* stored length is smaller       */
    if (r.sec[3].resident != 64u) ok = 0;
    if (r.sec[4].resident != 96u) ok = 0;   /* recovered, not the extent      */

    /* The strongest form of the claim: the DERIVED resident size equals the
     * byte count the codec will actually emit. Run the real decoders over the
     * real stored bytes and compare. If these ever disagree, the loader would
     * have planned for one size and been handed another — which is precisely
     * the failure a declared ram_bytes field makes undetectable. */
    { static uint8_t back[256];
      int got = dimfold_descend(s_probe + r.sec[2].offset, (int)r.sec[2].stored,
                                back, (int)sizeof back);
      if (got != (int)r.sec[2].resident) ok = 0;
      got = dimfold_expand(s_probe + r.sec[4].offset, (int)r.sec[4].stored,
                           back, (int)sizeof back);
      if (got != (int)r.sec[4].resident) ok = 0; }

    /* the hoard reads back */
    if (!tvl_rom_hold(&r, 2u, &hd) || hd.role != (uint8_t)TRI_NEUTRAL ||
        hd.section != TVL_NO_SECTION) ok = 0;
    if (!tvl_rom_gate(&r, 1u, &gt) || gt.from != 0u || gt.to != 2u ||
        gt.visible_only != 1u) ok = 0;
    if (tvl_rom_hold(&r, 3u, &hd)) ok = 0;         /* out of range refused    */

    /* ---- 2. the derived RAM figure matches a HAND-COMPUTED expectation ---- *
     * W_core   = WORLD 60 + MECH 32 + SUTRA 48                        = 140
     * W_stream = max( h0: 64 + r(h1) 96 + r(h2) 0 = 160,
     *                 h1: 96 + r(h2) 0            =  96,
     *                 h2: 0                       =   0 )             = 160
     * W_work   = chunk 512 + dimfold_bound(512) = 512 + (512*5+16)    = 3088
     * IWS(ROM) = 140 + 160 + 3088                                     = 3388
     * Note what is ABSENT: the stored lengths of sections 2 and 4. The figure
     * is a function of content, not of packing. */
    expect_core   = 60u + 32u + 48u;
    expect_stream = 64u + 96u + 0u;
    expect_work   = 512u + (512u * 5u + 16u);
    expect        = expect_core + expect_stream + expect_work;
    if (expect != 3388u) ok = 0;                    /* the arithmetic above   */
    ram = tvl_rom_ram_required(&r);
    if (ram != expect) ok = 0;

    /* the full IWS adds the two terms the ROM cannot know, supplied by the
     * caller from the hardware actually present. */
    env.display_w = 640u; env.display_h = 480u;
    env.bytes_per_px = 4u; env.buffers = 2u;        /* present + parallax     */
    env.ai_bytes = 512u;
    if (!tvl_rom_iws(&r, &env, &iws)) ok = 0;
    if (iws.core != expect_core || iws.stream != expect_stream ||
        iws.work != expect_work) ok = 0;
    if (iws.frame != 640u * 480u * 4u * 2u) ok = 0;
    if (iws.total != expect + 640u*480u*4u*2u + 512u) ok = 0;
    if (iws.overflow) ok = 0;

    /* a display twice as wide costs more, because it is asked and not assumed */
    { tvl_iws_t big; tvl_iws_env_t e2 = env; e2.display_w = 1280u;
      if (!tvl_rom_iws(&r, &e2, &big)) ok = 0;
      if (big.frame != iws.frame * 2u) ok = 0;
      if (big.core != iws.core) ok = 0; }           /* content term unchanged */

    /* an absurd display is refused, not clamped */
    { tvl_iws_t bad; tvl_iws_env_t e3 = env; e3.display_w = 0xFFFFFFFFu;
      if (tvl_rom_iws(&r, &e3, &bad)) ok = 0;
      if (!bad.overflow) ok = 0; }

    /* the witness never grants anything: the probe carries a zero witness, so
     * it must DISAGREE with the loader's honest derivation. */
    if (tvl_rom_witness_agrees(&r, &env)) ok = 0;
    { uint8_t w1[32], w2[32]; tvl_iws_env_t e4 = env; e4.display_h = 481u;
      tvl_rom_witness_of(&r, &env, w1);
      tvl_rom_witness_of(&r, &e4,  w2);
      if (digest_eq(w1, w2)) ok = 0;                /* witness tracks content+env */
      tvl_rom_witness_of(&r, &env, w2);
      if (!digest_eq(w1, w2)) ok = 0; }             /* and is deterministic   */

    /* ---- 3. REJECTIONS — the half that matters --------------------------- */
    { tvl_rom_t z;
      if (tvl_rom_parse(0, s_probe_len, (uint16_t)TRI_POSITIVE, &z) != TVL_E_NULL) ok = 0;
      if (tvl_rom_parse(s_probe, 0u,    (uint16_t)TRI_POSITIVE, &z) != TVL_E_NULL) ok = 0;
      if (tvl_rom_parse(s_probe, s_probe_len, (uint16_t)TRI_POSITIVE, 0) != TVL_E_NULL) ok = 0;
      /* short of the fixed header, and short of the declared section table */
      if (tvl_rom_parse(s_probe, TVL_HDR_SIZE - 1u, 3u, &z) != TVL_E_SHORT) ok = 0;
      if (tvl_rom_parse(s_probe, TVL_HDR_SIZE + 8u, 3u, &z) != TVL_E_SHORT) ok = 0;
      /* truncated inside the last payload */
      if (tvl_rom_parse(s_probe, s_probe_len - 1u, 3u, &z) != TVL_E_BOUNDS) ok = 0;
      /* the triad face cross-check */
      if (tvl_rom_parse(s_probe, s_probe_len, (uint16_t)TRI_NEUTRAL, &z) != TVL_E_ROLE) ok = 0;
    }

    ok &= reject(m_magic,   s_probe_len, TVL_E_MAGIC);
    ok &= reject(m_version, s_probe_len, TVL_E_VERSION);
    ok &= reject(m_role,    s_probe_len, TVL_E_ROLE);
    ok &= reject(m_cnt0,    s_probe_len, TVL_E_COUNT);
    ok &= reject(m_cntbig,  s_probe_len, TVL_E_COUNT);
    ok &= reject(m_seal,    s_probe_len, TVL_E_SEAL);
    ok &= reject(m_root,    s_probe_len, TVL_E_FIELD);
    ok &= reject(m_level,   s_probe_len, TVL_E_FIELD);
    ok &= reject(m_chunk,   s_probe_len, TVL_E_FIELD);

    ok &= reject(m_badform, s_probe_len, TVL_E_SEC_FIELD);
    ok &= reject(m_resv,    s_probe_len, TVL_E_SEC_FIELD);
    ok &= reject(m_zerolen, s_probe_len, TVL_E_SEC_FIELD);
    ok &= reject(m_chandup, s_probe_len, TVL_E_SEC_FIELD);
    ok &= reject(m_ovf,     s_probe_len, TVL_E_OVERFLOW);   /* offset+size wraps */
    ok &= reject(m_outside, s_probe_len, TVL_E_BOUNDS);
    ok &= reject(m_inhdr,   s_probe_len, TVL_E_OVERLAP);    /* section over header */
    ok &= reject(m_overlap, s_probe_len, TVL_E_OVERLAP);
    ok &= reject(m_tamper,  s_probe_len, TVL_E_DIGEST);

    ok &= reject(m_noworld, s_probe_len, TVL_E_WORLD);
    ok &= reject(m_2world,  s_probe_len, TVL_E_WORLD);
    ok &= reject(m_wfold,   s_probe_len, TVL_E_WORLD);
    ok &= reject(m_wstream, s_probe_len, TVL_E_WORLD);

    ok &= reject(m_badelev, s_probe_len, TVL_E_RESIDENT);

    ok &= reject(m_hmagic,  s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hholds,  s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hsize,   s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hlevel,  s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hdup,    s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hsec,    s_probe_len, TVL_E_HOARD);
    ok &= reject(m_hpersist,s_probe_len, TVL_E_HOARD);
    ok &= reject(m_gedge,   s_probe_len, TVL_E_HOARD);
    ok &= reject(m_gself,   s_probe_len, TVL_E_HOARD);
    ok &= reject(m_gresv,   s_probe_len, TVL_E_HOARD);
    ok &= reject(m_gdup,    s_probe_len, TVL_E_HOARD);

    /* ---- 4. registration goes through the EXISTING registry --------------- *
     * Stated plainly, because megarom_selfcheck's own comment sets the
     * precedent: this appends to the REAL 16-slot registry and megarom.h has no
     * unregister, so the probe permanently consumes one slot on any boot that
     * runs it. It is named TVL-PROBE rather than TOL-VOVINA so nothing mistakes
     * a self-check artefact for the shipped world, and the module's
     * one-registration latch is released at the end so a real world can still
     * take its own slot. */
    {
        static const char nm[] = "TVL-PROBE";
        static const char tg[] = "container self-check probe";
        int before = megarom_count();
        int slot;
        tvl_rom_t bad;
        tvl_bzero(&bad, (uint32_t)sizeof bad);
        if (tvl_rom_register(&bad, nm, tg) != -1) ok = 0;   /* invalid: refused */
        if (megarom_count() != before) ok = 0;              /* and no slot burnt */

        slot = tvl_rom_register(&r, nm, tg);
        if (slot < 0) ok = 0;
        else {
            const megarom_t *m = megarom_get(slot);
            if (!m || m->kind != MR_KIND_GAME) ok = 0;
            if (megarom_count() != before + 1) ok = 0;
            if (tvl_rom_active() != &r) ok = 0;
            /* ONE cartridge per world: a second attempt takes no second slot */
            if (tvl_rom_register(&r, nm, tg) != -1) ok = 0;
            if (megarom_count() != before + 1) ok = 0;
            /* the boot hook actually dispatches */
            g_boots = 0;
            if (megarom_boot(slot) != 0) ok = 0;
            if (g_boots != 1u) ok = 0;
        }
        /* Release the one-registration latch. What this CANNOT undo is the
         * megarom slot itself — megarom.h offers no unregister — so the probe
         * cartridge stays listed. That is a real cost of self-checking against
         * the live registry and it is written down rather than hidden. */
        g_slot   = -1;
        g_active = 0;
    }

    return ok;
}

/* ======================================================================== *
 * THE BOOT CARTRIDGE — "the interface IS the MegaROM".
 *
 * build_probe() is the ONE authoring routine in this file. Exposing it here as
 * the boot image means the host tool (build_system/mk_megarom.c, which links
 * this translation unit) and the kernel emit the SAME bytes: the MEGAROM.TVL
 * staged on the ESP is byte-for-byte the blob the kernel validates. No second
 * format, no promise a loader cannot check.
 *
 * HONEST SCOPE: register_boot EMBEDS the image (static storage) and registers
 * it. It does NOT read MEGAROM.TVL off the disc filesystem — that ESP file is
 * the identical-bytes twin, present so a real UEFI disc carries the container,
 * with the on-disc-read path left as a documented follow-on. The claim proven
 * at boot is exactly: media boots -> kernel up -> a VALID TVUL MegaROM parses
 * and takes a MR_KIND_GAME slot in the console registry.
 * ======================================================================== */

TVL_KEEP uint32_t tvl_rom_build_boot(uint8_t *b, uint32_t cap){
    if (!b) return 0u;
    return build_probe(b, cap);           /* the single authoring derivation */
}

/* The registered boot cartridge's image, parse, and one-shot latch. All static
 * so the megarom registry's BORROWED name/tagline/rom pointers outlive the
 * call — the same lifetime discipline tvl_rom_selfcheck() documents for its
 * probe. */
static uint8_t   s_boot_img[TVL_PROBE_CAP];
static tvl_rom_t s_boot_rom;
static int       s_boot_slot = -1;

TVL_KEEP int tvl_rom_register_boot(void){
    /* Static storage duration: megarom_t borrows these pointers. */
    static const char nm[] = "TOL VOVINA UPAAH LOT";
    static const char tg[] = "the interface IS the MegaROM";
    uint32_t len;

    if (s_boot_slot >= 0) return s_boot_slot;     /* idempotent: one slot     */

    len = build_probe(s_boot_img, (uint32_t)sizeof s_boot_img);
    if (len == 0u) return -1;
    if (tvl_rom_parse(s_boot_img, len, (uint16_t)TRI_POSITIVE, &s_boot_rom) != TVL_OK)
        return -1;
    if (!s_boot_rom.valid) return -1;
    {
        int slot = tvl_rom_register(&s_boot_rom, nm, tg);   /* MR_KIND_GAME    */
        if (slot < 0) return -1;
        s_boot_slot = slot;
        return slot;
    }
}
