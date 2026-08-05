/* holo.c — ZEDEC Holographic Data System Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "holo.h"
#include "../phase_coord/phase_coordinator.h"
#include "../../include/m5_types.h"

static int str_len(const char *s){int n=0;while(s[n])n++;return n;}
static int str_cmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return(int)(unsigned char)*a-(int)(unsigned char)*b;}
static void str_copy(char*d,const char*s){int i=0;while(s[i]){d[i]=s[i];i++;}d[i]=0;}
static void mem_set(void*d,int c,uint32_t n){uint8_t*p=d;for(uint32_t i=0;i<n;i++)p[i]=(uint8_t)c;}
static void mem_copy(void*d,const void*s,uint32_t n){uint8_t*dp=d;const uint8_t*sp=s;for(uint32_t i=0;i<n;i++)dp[i]=sp[i];}

/* ---- Checksum (CRC32-like) ---- */
uint32_t holo_checksum(const void *data, uint32_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ---- Init / Cleanup ---- */
void holo_init(holo_ctx_t *ctx) {
    mem_set(ctx, 0, sizeof(*ctx));
}

void holo_cleanup(holo_ctx_t *ctx) {
    mem_set(ctx, 0, sizeof(*ctx));
}

/* ---- Header creation ---- */
void holo_make_header(holo_header_t *hdr, holo_type_t type,
                      holo_encoding_t enc, holo_dimension_t dim,
                      uint32_t data_len, uint32_t omega, uint32_t phase) {
    mem_set(hdr, 0, sizeof(*hdr));
    switch (type) {
        case HOLO_TYPE_36N9:  hdr->magic = HOLO_MAGIC_36N9;  break;
        case HOLO_TYPE_9N63:  hdr->magic = HOLO_MAGIC_9N63;  break;
        case HOLO_TYPE_36M9:  hdr->magic = HOLO_MAGIC_36M9;  break;
        case HOLO_TYPE_ZEDEI: hdr->magic = HOLO_MAGIC_ZEDEI; break;
        case HOLO_TYPE_ZEDEC: hdr->magic = HOLO_MAGIC_ZEDEC; break;
        default: hdr->magic = 0; break;
    }
    hdr->version = 1;
    hdr->type = type;
    hdr->encoding = enc;
    hdr->dimension = dim;
    hdr->data_offset = HOLO_HEADER_SIZE;
    hdr->data_length = data_len;
    hdr->checksum = 0;
    hdr->omega = omega;
    hdr->phase = phase;
    hdr->integrity = 100;
    hdr->origin_id = 0;
}

/* ---- Type detection ---- */
holo_type_t holo_detect_type(uint32_t magic) {
    switch (magic) {
        case HOLO_MAGIC_36N9:  return HOLO_TYPE_36N9;
        case HOLO_MAGIC_9N63:  return HOLO_TYPE_9N63;
        case HOLO_MAGIC_36M9:  return HOLO_TYPE_36M9;
        case HOLO_MAGIC_ZEDEI: return HOLO_TYPE_ZEDEI;
        case HOLO_MAGIC_ZEDEC: return HOLO_TYPE_ZEDEC;
        case HOLO_MAGIC_9M63:  return HOLO_TYPE_9M63;
        case HOLO_MAGIC_IEDEZ: return HOLO_TYPE_IEDEZ;
        case HOLO_MAGIC_CEDEZ: return HOLO_TYPE_CEDEZ;
        case HOLO_MAGIC_0N0:   return HOLO_TYPE_0N0;
        case HOLO_MAGIC_0M0:   return HOLO_TYPE_0M0;
        case HOLO_MAGIC_ZEDEZ: return HOLO_TYPE_ZEDEZ;
        case HOLO_MAGIC_CEDEC: return HOLO_TYPE_CEDEC;
        default: return HOLO_TYPE_NONE;
    }
}

const char *holo_type_name(holo_type_t type) {
    switch (type) {
        case HOLO_TYPE_36N9:  return "Positive Space (36n9)";
        case HOLO_TYPE_9N63:  return "Negative Space (9n63)";
        case HOLO_TYPE_36M9:  return "Positive Manifest (36m9)";
        case HOLO_TYPE_ZEDEI: return "Positive Transform (zedei)";
        case HOLO_TYPE_ZEDEC: return "Positive Container (zedec)";
        case HOLO_TYPE_9M63:  return "Negative Manifest (9m63)";
        case HOLO_TYPE_IEDEZ: return "Negative Transform (iedez)";
        case HOLO_TYPE_CEDEZ: return "Negative Container (cedez)";
        case HOLO_TYPE_0N0:   return "Neutral Source (0n0)";
        case HOLO_TYPE_0M0:   return "Neutral Manifest (0m0)";
        case HOLO_TYPE_ZEDEZ: return "Neutral Transform (zedez)";
        case HOLO_TYPE_CEDEC: return "Neutral Container (cedec)";
        default: return "Unknown";
    }
}

const char *holo_type_extension(holo_type_t type) {
    switch (type) {
        case HOLO_TYPE_36N9:  return ".36n9";
        case HOLO_TYPE_9N63:  return ".9n63";
        case HOLO_TYPE_36M9:  return ".36m9";
        case HOLO_TYPE_ZEDEI: return ".zedei";
        case HOLO_TYPE_ZEDEC: return ".zedec";
        case HOLO_TYPE_9M63:  return ".9m63";
        case HOLO_TYPE_IEDEZ: return ".iedez";
        case HOLO_TYPE_CEDEZ: return ".cedez";
        case HOLO_TYPE_0N0:   return ".0n0";
        case HOLO_TYPE_0M0:   return ".0m0";
        case HOLO_TYPE_ZEDEZ: return ".zedez";
        case HOLO_TYPE_CEDEC: return ".cedec";
        default: return ".unk";
    }
}

const char *holo_encoding_name(holo_encoding_t enc) {
    switch (enc) {
        case HOLO_ENC_RAW:         return "Raw";
        case HOLO_ENC_PHASE:       return "Phase-encoded";
        case HOLO_ENC_INTERFERENCE:return "Interference";
        case HOLO_ENC_FRACTAL:     return "Fractal";
        case HOLO_ENC_QUANTUM:     return "Quantum";
        default: return "Unknown";
    }
}

const char *holo_dimension_name(holo_dimension_t dim) {
    switch (dim) {
        case HOLO_DIM_1D:    return "1D (Linear)";
        case HOLO_DIM_2D:    return "2D (Planar)";
        case HOLO_DIM_3D:    return "3D (Volumetric)";
        case HOLO_DIM_4D:    return "4D (Temporal-3D)";
        case HOLO_DIM_HOLO:  return "Holographic (Light Field)";
        default: return "Unknown";
    }
}

const char *holo_render_mode_name(holo_render_mode_t mode) {
    switch (mode) {
        case HOLO_RENDER_VISUAL:    return "Visual";
        case HOLO_RENDER_AUDIO:     return "Audio";
        case HOLO_RENDER_DATA:      return "Data";
        case HOLO_RENDER_HOLOGRAM:  return "Hologram";
        case HOLO_RENDER_FRACTAL:   return "Fractal";
        default: return "Unknown";
    }
}

const char *holo_blend_mode_name(holo_blend_mode_t mode) {
    switch (mode) {
        case HOLO_BLEND_INTERFERENCE: return "Interference";
        case HOLO_BLEND_PHASE_SHIFT:  return "Phase Shift";
        case HOLO_BLEND_FOURIER:      return "Fourier";
        case HOLO_BLEND_FRESNEL:      return "Fresnel";
        case HOLO_BLEND_HOLOGRAPHIC:  return "Holographic";
        default: return "Unknown";
    }
}

/* ---- .36n9 Positive Space ---- */
int32_t holo_positive_create(holo_positive_t *pos, uint32_t pos_id,
                             uint32_t pair_id, uint32_t amplitude,
                             uint32_t frequency,
                             holo_encoding_t enc, holo_dimension_t dim) {
    mem_set(pos, 0, sizeof(*pos));
    holo_make_header(&pos->header, HOLO_TYPE_36N9, enc, dim, 0, 1, 0);
    pos->positive_id = pos_id;
    pos->pair_id = pair_id;
    pos->amplitude = amplitude;
    pos->frequency = frequency;
    return 0;
}

int32_t holo_positive_set_payload(holo_ctx_t *ctx, const uint8_t *data,
                                   uint32_t len) {
    if (!ctx || (!data && len > 0)) return -1;
    ctx->positive_payload = (uint8_t*)data;
    ctx->positive_payload_len = len;
    ctx->positive_loaded = true;
    if (ctx->positive)
        ctx->positive->header.data_length = len;
    return 0;
}

int32_t holo_positive_validate(const holo_positive_t *pos) {
    if (!pos) return -1;
    if (pos->header.magic != HOLO_MAGIC_36N9) return -1;
    if (pos->header.type != HOLO_TYPE_36N9) return -1;
    return 0;
}

/* ---- .9n63 Negative Space ---- */
int32_t holo_negative_create(holo_negative_t *neg, uint32_t neg_id,
                             uint32_t pair_id, uint32_t inv_amplitude,
                             uint32_t phase_offset,
                             holo_encoding_t enc, holo_dimension_t dim) {
    mem_set(neg, 0, sizeof(*neg));
    holo_make_header(&neg->header, HOLO_TYPE_9N63, enc, dim, 0, 0, 180);
    neg->negative_id = neg_id;
    neg->pair_id = pair_id;
    neg->inverse_amplitude = inv_amplitude;
    neg->phase_offset = phase_offset;
    return 0;
}

int32_t holo_negative_set_payload(holo_ctx_t *ctx, const uint8_t *data,
                                   uint32_t len) {
    if (!ctx || (!data && len > 0)) return -1;
    ctx->negative_payload = (uint8_t*)data;
    ctx->negative_payload_len = len;
    ctx->negative_loaded = true;
    if (ctx->negative)
        ctx->negative->header.data_length = len;
    return 0;
}

int32_t holo_negative_validate(const holo_negative_t *neg) {
    if (!neg) return -1;
    if (neg->header.magic != HOLO_MAGIC_9N63) return -1;
    if (neg->header.type != HOLO_TYPE_9N63) return -1;
    return 0;
}

/* ---- .36m9 Dataset ---- */
uint32_t holo_dataset_pair_checksum(uint32_t pos_id, uint32_t neg_id) {
    uint32_t ids[2] = {pos_id, neg_id};
    return holo_checksum(ids, 8);
}

int32_t holo_dataset_create(holo_dataset_t *ds, uint32_t ds_id,
                            const char *name) {
    mem_set(ds, 0, sizeof(*ds));
    holo_make_header(&ds->header, HOLO_TYPE_36M9, HOLO_ENC_INTERFERENCE,
                     HOLO_DIM_HOLO, 0, 1, 0);
    ds->dataset_id = ds_id;
    ds->num_pairs = 0;
    if (name) str_copy(ds->dataset_name, name);
    return 0;
}

int32_t holo_dataset_add_pair(holo_dataset_t *ds, uint32_t pos_id,
                               uint32_t neg_id, uint32_t render_order) {
    if (!ds || ds->num_pairs >= HOLO_MAX_PAIRS) return -1;
    holo_pair_ref_t *p = &ds->pairs[ds->num_pairs];
    p->positive_id = pos_id;
    p->negative_id = neg_id;
    p->pair_checksum = holo_dataset_pair_checksum(pos_id, neg_id);
    p->render_order = render_order;
    ds->num_pairs++;
    return (int32_t)(ds->num_pairs - 1);
}

int32_t holo_dataset_remove_pair(holo_dataset_t *ds, uint32_t index) {
    if (!ds || index >= ds->num_pairs) return -1;
    for (uint32_t i = index; i < ds->num_pairs - 1; i++)
        ds->pairs[i] = ds->pairs[i + 1];
    ds->num_pairs--;
    return 0;
}

int32_t holo_dataset_validate(const holo_dataset_t *ds) {
    if (!ds) return -1;
    if (ds->header.magic != HOLO_MAGIC_36M9) return -1;
    if (ds->header.type != HOLO_TYPE_36M9) return -1;
    return 0;
}

/* ---- .zedei Renderer ---- */
int32_t holo_renderer_create(holo_renderer_t *r, uint32_t r_id,
                              holo_render_mode_t rmode,
                              holo_blend_mode_t bmode,
                              uint32_t width, uint32_t height,
                              uint32_t dataset_id) {
    mem_set(r, 0, sizeof(*r));
    holo_make_header(&r->header, HOLO_TYPE_ZEDEI, HOLO_ENC_PHASE,
                     HOLO_DIM_HOLO, 0, 1, 0);
    r->renderer_id = r_id;
    r->render_mode = rmode;
    r->blend_mode = bmode;
    r->target_width = width;
    r->target_height = height;
    r->target_depth = 1;
    r->view_angle_x = 0;
    r->view_angle_y = 0;
    r->focal_length = 50000; /* 50mm */
    r->wavelength = 632;     /* HeNe red 632nm */
    r->dataset_id = dataset_id;
    r->shader_offset = 0;
    r->shader_length = 0;
    return 0;
}

int32_t holo_renderer_set_shader(holo_ctx_t *ctx, const uint8_t *shader,
                                  uint32_t len) {
    if (!ctx) return -1;
    ctx->renderer_shader = (uint8_t*)shader;
    ctx->renderer_shader_len = len;
    if (ctx->renderer) {
        ctx->renderer->shader_length = len;
        ctx->renderer->shader_offset = HOLO_HEADER_SIZE + sizeof(holo_renderer_t) - sizeof(holo_header_t);
    }
    return 0;
}

int32_t holo_renderer_validate(const holo_renderer_t *r) {
    if (!r) return -1;
    if (r->header.magic != HOLO_MAGIC_ZEDEI) return -1;
    if (r->header.type != HOLO_TYPE_ZEDEI) return -1;
    return 0;
}

/* ---- .zedec Container ---- */
int32_t holo_container_create(holo_container_t *c, uint32_t c_id,
                               const char *name, const char *signature) {
    mem_set(c, 0, sizeof(*c));
    holo_make_header(&c->header, HOLO_TYPE_ZEDEC, HOLO_ENC_RAW,
                     HOLO_DIM_HOLO, 0, 1, 0);
    c->container_id = c_id;
    c->num_entries = 0;
    c->total_size = 0;
    c->payload_offset = HOLO_HEADER_SIZE + sizeof(holo_container_t) - sizeof(holo_header_t);
    if (name) str_copy(c->container_name, name);
    if (signature) str_copy(c->signature, signature);
    return 0;
}

int32_t holo_container_add_entry(holo_container_t *c, holo_entry_type_t type,
                                  uint32_t offset, uint32_t length,
                                  const char *name) {
    if (!c || c->num_entries >= HOLO_MAX_ENTRIES) return -1;
    holo_entry_t *e = &c->entries[c->num_entries];
    e->type = type;
    e->offset = offset;
    e->length = length;
    e->checksum = 0;
    if (name) str_copy(e->name, name);
    c->num_entries++;
    c->total_size += length;
    return (int32_t)(c->num_entries - 1);
}

int32_t holo_container_add_36n9(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length) {
    return holo_container_add_entry(c, HOLO_ENTRY_36N9, offset, length, name);
}

int32_t holo_container_add_9n63(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length) {
    return holo_container_add_entry(c, HOLO_ENTRY_9N63, offset, length, name);
}

int32_t holo_container_add_36m9(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length) {
    return holo_container_add_entry(c, HOLO_ENTRY_36M9, offset, length, name);
}

int32_t holo_container_add_zedei(holo_container_t *c, const char *name,
                                  uint32_t offset, uint32_t length) {
    return holo_container_add_entry(c, HOLO_ENTRY_ZEDEI, offset, length, name);
}

int32_t holo_container_validate(const holo_container_t *c) {
    if (!c) return -1;
    if (c->header.magic != HOLO_MAGIC_ZEDEC) return -1;
    if (c->header.type != HOLO_TYPE_ZEDEC) return -1;
    return 0;
}

uint32_t holo_container_entry_count(const holo_container_t *c,
                                     holo_entry_type_t type) {
    if (!c) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < c->num_entries; i++)
        if (c->entries[i].type == type) count++;
    return count;
}

/* ---- Holographic Reconstruction ---- */

/* Interference reconstruction: XOR-style blend of positive and negative */
int32_t holo_interference(const uint8_t *positive, uint32_t pos_len,
                           const uint8_t *negative, uint32_t neg_len,
                           uint8_t *output, uint32_t max_out,
                           holo_blend_mode_t blend) {
    if (!positive || !negative || !output) return -1;
    uint32_t out_len = pos_len < neg_len ? pos_len : neg_len;
    if (out_len > max_out) out_len = max_out;

    switch (blend) {
        case HOLO_BLEND_INTERFERENCE:
            /* XOR interference pattern */
            for (uint32_t i = 0; i < out_len; i++)
                output[i] = positive[i] ^ negative[i];
            break;
        case HOLO_BLEND_PHASE_SHIFT:
            /* Phase-shifted: add with 90-degree rotation */
            for (uint32_t i = 0; i < out_len; i++)
                output[i] = (uint8_t)((positive[i] + negative[i]) >> 1);
            break;
        case HOLO_BLEND_FOURIER:
            /* Simplified Fourier: magnitude spectrum approximation */
            for (uint32_t i = 0; i < out_len; i++) {
                int32_t re = (int32_t)positive[i] - 128;
                int32_t im = (int32_t)negative[i] - 128;
                int32_t mag = re * re + im * im;
                output[i] = (uint8_t)(mag > 255 ? 255 : (mag < 0 ? 0 : mag));
            }
            break;
        case HOLO_BLEND_FRESNEL:
            /* Fresnel diffraction approximation */
            for (uint32_t i = 0; i < out_len; i++) {
                int32_t v = (int32_t)positive[i] - (int32_t)negative[i] + 128;
                output[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            break;
        case HOLO_BLEND_HOLOGRAPHIC:
            /* Full holographic: complex interference */
            for (uint32_t i = 0; i < out_len; i++) {
                uint8_t a = positive[i];
                uint8_t b = negative[i];
                /* Constructive where phases align, destructive where opposite */
                int32_t construct = (a + b) / 2;
                int32_t destruct = (a ^ b);
                output[i] = (uint8_t)((construct + destruct) / 2);
            }
            break;
        default:
            for (uint32_t i = 0; i < out_len; i++)
                output[i] = positive[i] ^ negative[i];
            break;
    }
    return (int32_t)out_len;
}

int32_t holo_reconstruct(holo_ctx_t *ctx, uint8_t *output, uint32_t max_out) {
    if (!ctx || !output) return -1;
    if (!ctx->positive_loaded || !ctx->negative_loaded) return -1;
    holo_blend_mode_t blend = HOLO_BLEND_INTERFERENCE;
    if (ctx->renderer) blend = ctx->renderer->blend_mode;
    return holo_interference(ctx->positive_payload, ctx->positive_payload_len,
                              ctx->negative_payload, ctx->negative_payload_len,
                              output, max_out, blend);
}

int32_t holo_render(holo_ctx_t *ctx, uint8_t *output, uint32_t max_out) {
    if (!ctx || !output) return -1;

    /* M5 Phase Coordinator: synchronize render with axiomatic tick */
    /* The holographic render is a collapse/observation event —
     * it must be coherent with the current phase tick */
    static phase_tick_t holo_tick;
    phase_coordinator_tick(&holo_tick);
    /* Store phase metadata in context for provenance */
    if (ctx->positive)
        ctx->positive->header.omega = (uint32_t)holo_tick.omega;

    /* Step 1: Reconstruct interference pattern */
    int32_t len = holo_reconstruct(ctx, output, max_out);
    if (len < 0) return -1;
    /* Step 2: Apply renderer transformations if available */
    if (ctx->renderer && ctx->renderer_loaded) {
        /* Apply view angle rotation (simplified) */
        uint32_t w = ctx->renderer->target_width;
        if (w == 0) w = len;
        /* For now, the reconstruction IS the render output.
         * A full implementation would apply shader code from zedei. */
    }
    return len;
}

/* ---- Serialization ---- */
int32_t holo_serialize_positive(const holo_positive_t *pos,
                                 const uint8_t *payload, uint32_t payload_len,
                                 uint8_t *out, uint32_t max_out) {
    if (!pos || !out) return -1;
    uint32_t total = sizeof(*pos) + payload_len;
    if (total > max_out) return -1;
    holo_positive_t *o = (holo_positive_t*)out;
    mem_copy(o, pos, sizeof(*pos));
    o->header.data_length = payload_len;
    o->header.checksum = payload ? holo_checksum(payload, payload_len) : 0;
    if (payload && payload_len > 0)
        mem_copy(out + sizeof(*pos), payload, payload_len);
    return (int32_t)total;
}

int32_t holo_serialize_negative(const holo_negative_t *neg,
                                 const uint8_t *payload, uint32_t payload_len,
                                 uint8_t *out, uint32_t max_out) {
    if (!neg || !out) return -1;
    uint32_t total = sizeof(*neg) + payload_len;
    if (total > max_out) return -1;
    holo_negative_t *o = (holo_negative_t*)out;
    mem_copy(o, neg, sizeof(*neg));
    o->header.data_length = payload_len;
    o->header.checksum = payload ? holo_checksum(payload, payload_len) : 0;
    if (payload && payload_len > 0)
        mem_copy(out + sizeof(*neg), payload, payload_len);
    return (int32_t)total;
}

int32_t holo_serialize_dataset(const holo_dataset_t *ds,
                                uint8_t *out, uint32_t max_out) {
    if (!ds || !out) return -1;
    uint32_t total = sizeof(*ds);
    if (total > max_out) return -1;
    mem_copy(out, ds, sizeof(*ds));
    ((holo_dataset_t*)out)->header.data_length = sizeof(*ds) - sizeof(holo_header_t);
    return (int32_t)total;
}

int32_t holo_serialize_renderer(const holo_renderer_t *r,
                                 const uint8_t *shader, uint32_t shader_len,
                                 uint8_t *out, uint32_t max_out) {
    if (!r || !out) return -1;
    uint32_t total = sizeof(*r) + shader_len;
    if (total > max_out) return -1;
    holo_renderer_t *o = (holo_renderer_t*)out;
    mem_copy(o, r, sizeof(*r));
    o->shader_length = shader_len;
    o->shader_offset = sizeof(*r);
    o->header.data_length = sizeof(*r) - sizeof(holo_header_t) + shader_len;
    if (shader && shader_len > 0)
        mem_copy(out + sizeof(*r), shader, shader_len);
    return (int32_t)total;
}

int32_t holo_serialize_container(const holo_container_t *c,
                                  uint8_t *out, uint32_t max_out) {
    if (!c || !out) return -1;
    uint32_t total = sizeof(*c);
    if (total > max_out) return -1;
    mem_copy(out, c, sizeof(*c));
    ((holo_container_t*)out)->header.data_length = sizeof(*c) - sizeof(holo_header_t);
    return (int32_t)total;
}

/* ---- Deserialization ---- */
int32_t holo_deserialize_header(const uint8_t *data, uint32_t len,
                                 holo_header_t *hdr) {
    if (!data || !hdr || len < HOLO_HEADER_SIZE) return -1;
    mem_copy(hdr, data, HOLO_HEADER_SIZE);
    if (holo_detect_type(hdr->magic) == HOLO_TYPE_NONE) return -1;
    return 0;
}

int32_t holo_deserialize_positive(const uint8_t *data, uint32_t len,
                                   holo_positive_t *pos,
                                   uint8_t *payload_out, uint32_t *payload_len) {
    if (!data || !pos || len < sizeof(*pos)) return -1;
    mem_copy(pos, data, sizeof(*pos));
    if (holo_positive_validate(pos) != 0) return -1;
    uint32_t plen = pos->header.data_length;
    if (payload_len) *payload_len = plen;
    if (payload_out && plen > 0 && len >= sizeof(*pos) + plen)
        mem_copy(payload_out, data + sizeof(*pos), plen);
    return 0;
}

int32_t holo_deserialize_negative(const uint8_t *data, uint32_t len,
                                   holo_negative_t *neg,
                                   uint8_t *payload_out, uint32_t *payload_len) {
    if (!data || !neg || len < sizeof(*neg)) return -1;
    mem_copy(neg, data, sizeof(*neg));
    if (holo_negative_validate(neg) != 0) return -1;
    uint32_t plen = neg->header.data_length;
    if (payload_len) *payload_len = plen;
    if (payload_out && plen > 0 && len >= sizeof(*neg) + plen)
        mem_copy(payload_out, data + sizeof(*neg), plen);
    return 0;
}

int32_t holo_deserialize_dataset(const uint8_t *data, uint32_t len,
                                  holo_dataset_t *ds) {
    if (!data || !ds || len < sizeof(*ds)) return -1;
    mem_copy(ds, data, sizeof(*ds));
    if (holo_dataset_validate(ds) != 0) return -1;
    return 0;
}

int32_t holo_deserialize_renderer(const uint8_t *data, uint32_t len,
                                   holo_renderer_t *r,
                                   uint8_t *shader_out, uint32_t *shader_len) {
    if (!data || !r || len < sizeof(*r)) return -1;
    mem_copy(r, data, sizeof(*r));
    if (holo_renderer_validate(r) != 0) return -1;
    uint32_t slen = r->shader_length;
    if (shader_len) *shader_len = slen;
    if (shader_out && slen > 0 && len >= sizeof(*r) + slen)
        mem_copy(shader_out, data + sizeof(*r), slen);
    return 0;
}

int32_t holo_deserialize_container(const uint8_t *data, uint32_t len,
                                    holo_container_t *c) {
    if (!data || !c || len < sizeof(*c)) return -1;
    mem_copy(c, data, sizeof(*c));
    if (holo_container_validate(c) != 0) return -1;
    return 0;
}

/* ---- File extension matching ---- */
holo_type_t holo_type_from_extension(const char *filename) {
    if (!filename) return HOLO_TYPE_NONE;
    int len = str_len(filename);
    if (len < 4) return HOLO_TYPE_NONE;

    /* Check 4-char extensions first (.0n0, .0m0) */
    if (len >= 4) {
        const char *ext4 = filename + len - 4;
        if (str_cmp(ext4, ".0n0") == 0)  return HOLO_TYPE_0N0;
        if (str_cmp(ext4, ".0m0") == 0)  return HOLO_TYPE_0M0;
    }

    if (len < 5) return HOLO_TYPE_NONE;
    const char *ext = filename + len - 5;
    if (str_cmp(ext, ".36n9") == 0)  return HOLO_TYPE_36N9;
    if (str_cmp(ext, ".9n63") == 0)  return HOLO_TYPE_9N63;
    if (str_cmp(ext, ".36m9") == 0)  return HOLO_TYPE_36M9;
    if (str_cmp(ext, ".9m63") == 0)  return HOLO_TYPE_9M63;
    if (str_cmp(ext, ".vino") == 0)  return HOLO_TYPE_NONE;  /* not holo */
    if (str_cmp(ext, ".ula\0") == 0) return HOLO_TYPE_NONE;  /* not holo */
    /* 6-char extensions */
    if (len >= 6) {
        const char *ext6 = filename + len - 6;
        if (str_cmp(ext6, ".zedei") == 0) return HOLO_TYPE_ZEDEI;
        if (str_cmp(ext6, ".zedec") == 0) return HOLO_TYPE_ZEDEC;
        if (str_cmp(ext6, ".iedez") == 0) return HOLO_TYPE_IEDEZ;
        if (str_cmp(ext6, ".cedez") == 0) return HOLO_TYPE_CEDEZ;
        if (str_cmp(ext6, ".zedez") == 0) return HOLO_TYPE_ZEDEZ;
        if (str_cmp(ext6, ".cedec") == 0) return HOLO_TYPE_CEDEC;
    }
    return HOLO_TYPE_NONE;
}

bool holo_is_holo_file(const char *filename) {
    return holo_type_from_extension(filename) != HOLO_TYPE_NONE;
}
