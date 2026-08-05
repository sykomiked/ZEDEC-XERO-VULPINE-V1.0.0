/* holo.h — ZEDEC Holographic Data System
 *
 * Five file types form a holographic data architecture:
 *
 *   .36n9  — Positive space: constructive data (matter)
 *   .9n63  — Negative space: complementary/inverse data (antimatter)
 *   .36m9  — Dataset: manifest of 36n9 + 9n63 pairs (interference record)
 *   .zedei — Holographic renderer: reconstructs output from 36n9/9n63/36m9
 *   .zedec — Container: bundles 36n9 + 9n63 + 36m9 + zedei into one unit
 *
 * Conceptual mapping to M5 axiomatic framework:
 *   36n9  = positive phase (omega+)
 *   9n63  = negative phase (omega-)
 *   36m9  = synthesis (phase coordinator record)
 *   zedei = collapse/observation (renderer)
 *   zedec = full system envelope (container)
 *
 * The positive (36n9) and negative (9n63) files form interference
 * patterns. When combined through a .zedei renderer, they reconstruct
 * holographic output — visual, auditory, or structured data.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef HOLO_H
#define HOLO_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Magic numbers ---- */

#define HOLO_MAGIC_36N9   0x33366E39  /* "36n9" */
#define HOLO_MAGIC_9N63   0x396E3633  /* "9n63" */
#define HOLO_MAGIC_36M9   0x33366D39  /* "36m9" */
#define HOLO_MAGIC_ZEDEI  0x5A454445  /* "ZEDE" */
#define HOLO_MAGIC_ZEDEC  0x5A454443  /* "ZEDC" */
/* Tri-space neutral extensions (provisional palindromic) */
#define HOLO_MAGIC_9M63   0x396D3633  /* "9m63" */
#define HOLO_MAGIC_0N0    0x304E3000  /* "0n0"  */
#define HOLO_MAGIC_0M0    0x304D3000  /* "0m0"  */
#define HOLO_MAGIC_IEDEZ  0x49454445  /* "IEDE" */
#define HOLO_MAGIC_CEDEZ  0x43454445  /* "CEDE" */
#define HOLO_MAGIC_ZEDEZ  0x5A45445A  /* "ZEDZ" */
#define HOLO_MAGIC_CEDEC  0x43454443  /* "CEDC" */

/* ---- File type identifiers ---- */

typedef enum {
    HOLO_TYPE_NONE = 0,
    HOLO_TYPE_36N9 = 1,   /* Positive space (S+) */
    HOLO_TYPE_9N63 = 2,   /* Negative space (S-) */
    HOLO_TYPE_36M9 = 3,   /* Positive manifest (S+) */
    HOLO_TYPE_ZEDEI = 4,  /* Positive transform (S+) */
    HOLO_TYPE_ZEDEC = 5,  /* Positive container (S+) */
    /* Tri-space negative extensions */
    HOLO_TYPE_9M63 = 6,   /* Negative manifest (S-) */
    HOLO_TYPE_IEDEZ = 7,  /* Negative transform (S-) */
    HOLO_TYPE_CEDEZ = 8,  /* Negative container (S-) */
    /* Tri-space neutral extensions (provisional) */
    HOLO_TYPE_0N0 = 9,    /* Neutral source (S0) */
    HOLO_TYPE_0M0 = 10,   /* Neutral manifest (S0) */
    HOLO_TYPE_ZEDEZ = 11, /* Neutral transform (S0) */
    HOLO_TYPE_CEDEC = 12, /* Neutral container (S0) */
} holo_type_t;

/* ---- Data encoding modes ---- */

typedef enum {
    HOLO_ENC_RAW = 0,       /* Raw binary */
    HOLO_ENC_PHASE = 1,     /* M5 phase-encoded */
    HOLO_ENC_INTERFERENCE = 2, /* Interference pattern */
    HOLO_ENC_FRACTAL = 3,   /* Fractal compressed */
    HOLO_ENC_QUANTUM = 4    /* Quantum superposition */
} holo_encoding_t;

/* ---- Dimensions for spatial data ---- */

typedef enum {
    HOLO_DIM_1D = 1,   /* Linear (audio, time series) */
    HOLO_DIM_2D = 2,   /* Planar (images, textures) */
    HOLO_DIM_3D = 3,   /* Volumetric (voxels, point clouds) */
    HOLO_DIM_4D = 4,   /* Temporal-3D (animations, simulations) */
    HOLO_DIM_HOLO = 5  /* Full holographic (light field) */
} holo_dimension_t;

/* ---- Common header (first 64 bytes of every holo file) ---- */

typedef struct holo_header {
    uint32_t magic;          /* One of HOLO_MAGIC_* */
    uint32_t version;        /* Format version */
    holo_type_t type;        /* File type */
    holo_encoding_t encoding; /* Data encoding */
    holo_dimension_t dimension; /* Spatial dimension */
    uint32_t data_offset;    /* Offset to payload */
    uint32_t data_length;    /* Length of payload */
    uint32_t checksum;       /* CRC32 of payload */

    /* M5 axiomatic metadata */
    uint32_t omega;          /* Phase omega (positive/negative) */
    uint32_t phase;          /* Current phase */
    uint32_t integrity;      /* Integrity score */
    uint32_t origin_id;      /* Originating node */

    /* Reserved for future use */
    uint32_t reserved[12];
} __attribute__((packed)) holo_header_t;

#define HOLO_HEADER_SIZE 64

/* ---- .36n9 Positive Space File ---- */
/* Contains constructive data — the "matter" of the hologram.
 * Paired with a .9n63 file to form complete interference patterns. */

typedef struct holo_positive {
    holo_header_t header;
    uint32_t positive_id;    /* Unique ID within dataset */
    uint32_t pair_id;        /* ID of complementary 9n63 file */
    uint32_t amplitude;      /* Peak amplitude */
    uint32_t frequency;      /* Characteristic frequency */
    /* Payload follows in raw file */
} holo_positive_t;

/* ---- .9n63 Negative Space File ---- */
/* Contains complementary/inverse data — the "antimatter" or void.
 * Together with 36n9, forms the interference pattern. */

typedef struct holo_negative {
    holo_header_t header;
    uint32_t negative_id;    /* Unique ID within dataset */
    uint32_t pair_id;        /* ID of complementary 36n9 file */
    uint32_t inverse_amplitude; /* Inverse amplitude */
    uint32_t phase_offset;   /* Phase offset from positive */
    /* Payload follows in raw file */
} holo_negative_t;

/* ---- .36m9 Dataset File ---- */
/* Manifest of 36n9 + 9n63 pairs. Acts as an index/playlist
 * that groups positive/negative pairs into coherent datasets. */

#define HOLO_MAX_PAIRS 256

typedef struct holo_pair_ref {
    uint32_t positive_id;   /* 36n9 file ID */
    uint32_t negative_id;   /* 9n63 file ID */
    uint32_t pair_checksum; /* Combined checksum */
    uint32_t render_order;  /* Order in rendering pipeline */
} holo_pair_ref_t;

typedef struct holo_dataset {
    holo_header_t header;
    uint32_t dataset_id;     /* Unique dataset ID */
    uint32_t num_pairs;      /* Number of 36n9/9n63 pairs */
    holo_pair_ref_t pairs[HOLO_MAX_PAIRS];
    uint32_t total_positive_bytes; /* Sum of all 36n9 payloads */
    uint32_t total_negative_bytes; /* Sum of all 9n63 payloads */
    char dataset_name[64];
} holo_dataset_t;

/* ---- .zedei Holographic Renderer File ---- */
/* Contains rendering instructions to reconstruct output from
 * 36n9, 9n63, and 36m9 files. Defines the interference
 * reconstruction algorithm, view parameters, and output format. */

typedef enum {
    HOLO_RENDER_VISUAL = 0,     /* Visual output (image/3D) */
    HOLO_RENDER_AUDIO = 1,      /* Audio output */
    HOLO_RENDER_DATA = 2,       /* Structured data output */
    HOLO_RENDER_HOLOGRAM = 3,   /* Full light-field hologram */
    HOLO_RENDER_FRACTAL = 4     /* Fractal visualization */
} holo_render_mode_t;

typedef enum {
    HOLO_BLEND_INTERFERENCE = 0,  /* Standard interference */
    HOLO_BLEND_PHASE_SHIFT = 1,   /* Phase-shifted reconstruction */
    HOLO_BLEND_FOURIER = 2,       /* Fourier transform reconstruction */
    HOLO_BLEND_FRESNEL = 3,       /* Fresnel diffraction */
    HOLO_BLEND_HOLOGRAPHIC = 4    /* Full holographic reconstruction */
} holo_blend_mode_t;

typedef struct holo_renderer {
    holo_header_t header;
    uint32_t renderer_id;
    holo_render_mode_t render_mode;
    holo_blend_mode_t blend_mode;
    uint32_t target_width;     /* Output width (pixels/samples) */
    uint32_t target_height;    /* Output height */
    uint32_t target_depth;     /* Output depth (for 3D) */
    uint32_t view_angle_x;     /* View angle X (millidegrees) */
    uint32_t view_angle_y;     /* View angle Y */
    uint32_t focal_length;     /* Focal length (micrometers) */
    uint32_t wavelength;       /* Reference wavelength (nm) */
    uint32_t dataset_id;       /* Associated 36m9 dataset */
    uint32_t shader_offset;    /* Offset to shader code in payload */
    uint32_t shader_length;    /* Length of shader code */
} holo_renderer_t;

/* ---- .zedec Container File ---- */
/* Top-level container that bundles 36n9, 9n63, 36m9, and zedei
 * files into a single archive. Like a ZIP for holographic data. */

#define HOLO_MAX_ENTRIES 512

typedef enum {
    HOLO_ENTRY_36N9 = 1,
    HOLO_ENTRY_9N63 = 2,
    HOLO_ENTRY_36M9 = 3,
    HOLO_ENTRY_ZEDEI = 4
} holo_entry_type_t;

typedef struct holo_entry {
    holo_entry_type_t type;
    uint32_t offset;          /* Offset within container payload */
    uint32_t length;          /* Length of entry data */
    uint32_t checksum;        /* Entry checksum */
    char name[64];            /* Entry name (filename) */
} holo_entry_t;

typedef struct holo_container {
    holo_header_t header;
    uint32_t container_id;
    uint32_t num_entries;
    holo_entry_t entries[HOLO_MAX_ENTRIES];
    uint32_t total_size;      /* Total payload size */
    uint32_t payload_offset;  /* Offset to concatenated payload */
    char container_name[64];
    char signature[64];       /* Creator signature */
} holo_container_t;

/* ---- In-memory context for holographic operations ---- */

typedef struct holo_ctx {
    holo_positive_t *positive;   /* Loaded 36n9 data */
    holo_negative_t *negative;   /* Loaded 9n63 data */
    holo_dataset_t *dataset;     /* Loaded 36m9 data */
    holo_renderer_t *renderer;   /* Loaded zedei data */
    holo_container_t *container; /* Loaded zedec data */

    /* Raw payload buffers */
    uint8_t *positive_payload;
    uint32_t positive_payload_len;
    uint8_t *negative_payload;
    uint32_t negative_payload_len;
    uint8_t *renderer_shader;
    uint32_t renderer_shader_len;

    /* Render output buffer */
    uint8_t *output;
    uint32_t output_len;
    uint32_t output_capacity;

    /* State */
    bool positive_loaded;
    bool negative_loaded;
    bool dataset_loaded;
    bool renderer_loaded;
    bool container_loaded;
} holo_ctx_t;

/* ---- API Functions ---- */

/* Initialization */
void holo_init(holo_ctx_t *ctx);
void holo_cleanup(holo_ctx_t *ctx);

/* Header creation */
void holo_make_header(holo_header_t *hdr, holo_type_t type,
                      holo_encoding_t enc, holo_dimension_t dim,
                      uint32_t data_len, uint32_t omega, uint32_t phase);

/* Type detection from magic */
holo_type_t holo_detect_type(uint32_t magic);
const char *holo_type_name(holo_type_t type);
const char *holo_type_extension(holo_type_t type);
const char *holo_encoding_name(holo_encoding_t enc);
const char *holo_dimension_name(holo_dimension_t dim);
const char *holo_render_mode_name(holo_render_mode_t mode);
const char *holo_blend_mode_name(holo_blend_mode_t mode);

/* Checksum */
uint32_t holo_checksum(const void *data, uint32_t len);

/* .36n9 Positive space operations */
int32_t holo_positive_create(holo_positive_t *pos, uint32_t pos_id,
                             uint32_t pair_id, uint32_t amplitude,
                             uint32_t frequency,
                             holo_encoding_t enc, holo_dimension_t dim);
int32_t holo_positive_set_payload(holo_ctx_t *ctx, const uint8_t *data,
                                   uint32_t len);
int32_t holo_positive_validate(const holo_positive_t *pos);

/* .9n63 Negative space operations */
int32_t holo_negative_create(holo_negative_t *neg, uint32_t neg_id,
                             uint32_t pair_id, uint32_t inv_amplitude,
                             uint32_t phase_offset,
                             holo_encoding_t enc, holo_dimension_t dim);
int32_t holo_negative_set_payload(holo_ctx_t *ctx, const uint8_t *data,
                                   uint32_t len);
int32_t holo_negative_validate(const holo_negative_t *neg);

/* .36m9 Dataset operations */
int32_t holo_dataset_create(holo_dataset_t *ds, uint32_t ds_id,
                            const char *name);
int32_t holo_dataset_add_pair(holo_dataset_t *ds, uint32_t pos_id,
                               uint32_t neg_id, uint32_t render_order);
int32_t holo_dataset_remove_pair(holo_dataset_t *ds, uint32_t index);
int32_t holo_dataset_validate(const holo_dataset_t *ds);
uint32_t holo_dataset_pair_checksum(uint32_t pos_id, uint32_t neg_id);

/* .zedei Renderer operations */
int32_t holo_renderer_create(holo_renderer_t *r, uint32_t r_id,
                              holo_render_mode_t rmode,
                              holo_blend_mode_t bmode,
                              uint32_t width, uint32_t height,
                              uint32_t dataset_id);
int32_t holo_renderer_set_shader(holo_ctx_t *ctx, const uint8_t *shader,
                                  uint32_t len);
int32_t holo_renderer_validate(const holo_renderer_t *r);

/* .zedec Container operations */
int32_t holo_container_create(holo_container_t *c, uint32_t c_id,
                               const char *name, const char *signature);
int32_t holo_container_add_entry(holo_container_t *c, holo_entry_type_t type,
                                  uint32_t offset, uint32_t length,
                                  const char *name);
int32_t holo_container_add_36n9(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length);
int32_t holo_container_add_9n63(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length);
int32_t holo_container_add_36m9(holo_container_t *c, const char *name,
                                 uint32_t offset, uint32_t length);
int32_t holo_container_add_zedei(holo_container_t *c, const char *name,
                                  uint32_t offset, uint32_t length);
int32_t holo_container_validate(const holo_container_t *c);
uint32_t holo_container_entry_count(const holo_container_t *c,
                                     holo_entry_type_t type);

/* Holographic reconstruction — the core rendering pipeline */
int32_t holo_reconstruct(holo_ctx_t *ctx, uint8_t *output, uint32_t max_out);
int32_t holo_interference(const uint8_t *positive, uint32_t pos_len,
                           const uint8_t *negative, uint32_t neg_len,
                           uint8_t *output, uint32_t max_out,
                           holo_blend_mode_t blend);
int32_t holo_render(holo_ctx_t *ctx, uint8_t *output, uint32_t max_out);

/* Serialization — pack/unpack to byte buffers */
int32_t holo_serialize_positive(const holo_positive_t *pos,
                                 const uint8_t *payload, uint32_t payload_len,
                                 uint8_t *out, uint32_t max_out);
int32_t holo_serialize_negative(const holo_negative_t *neg,
                                 const uint8_t *payload, uint32_t payload_len,
                                 uint8_t *out, uint32_t max_out);
int32_t holo_serialize_dataset(const holo_dataset_t *ds,
                                uint8_t *out, uint32_t max_out);
int32_t holo_serialize_renderer(const holo_renderer_t *r,
                                 const uint8_t *shader, uint32_t shader_len,
                                 uint8_t *out, uint32_t max_out);
int32_t holo_serialize_container(const holo_container_t *c,
                                  uint8_t *out, uint32_t max_out);

/* Deserialization — unpack from byte buffers */
int32_t holo_deserialize_header(const uint8_t *data, uint32_t len,
                                 holo_header_t *hdr);
int32_t holo_deserialize_positive(const uint8_t *data, uint32_t len,
                                   holo_positive_t *pos,
                                   uint8_t *payload_out, uint32_t *payload_len);
int32_t holo_deserialize_negative(const uint8_t *data, uint32_t len,
                                   holo_negative_t *neg,
                                   uint8_t *payload_out, uint32_t *payload_len);
int32_t holo_deserialize_dataset(const uint8_t *data, uint32_t len,
                                  holo_dataset_t *ds);
int32_t holo_deserialize_renderer(const uint8_t *data, uint32_t len,
                                   holo_renderer_t *r,
                                   uint8_t *shader_out, uint32_t *shader_len);
int32_t holo_deserialize_container(const uint8_t *data, uint32_t len,
                                    holo_container_t *c);

/* File extension matching */
holo_type_t holo_type_from_extension(const char *filename);
bool holo_is_holo_file(const char *filename);

#endif /* HOLO_H */
