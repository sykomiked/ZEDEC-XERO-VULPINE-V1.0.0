/* media_fabric.h — ZXV Media Fabric Compound Module
 *
 * The Media Fabric unifies all media, audio, video, codec, and creative
 * modules into a single coherent fabric for real-time media processing,
 * streaming, and content creation with economic incentives.
 *
 * Sub-modules integrated:
 *   1. Audio — Audio processing, synthesis, DSP
 *   2. Video — Video encoding/decoding, streaming
 *   3. Codec — Unified codec framework (AV1, VP9, H.265, Opus, FLAC)
 * 4. Audiogenomics Pro — DNA-to-audio translation
 * 5. Art Studio — Creative tools, generative art
 * 6. Games — Game engine, real-time rendering
 * 7. Font — Typography, script rendering, TrueType
 * 8. Theme — UI theming, icon system
 * 9. Display — Display controller, framebuffer
 * 10. Mesh Net — P2P media streaming via trade routes
 * 11. Financial Fabric — Media licensing, royalties, NFTs
 * 12. Identity Fabric — Creator credentials, provenance
 * 13. Orbital Fabric — Schema translation for media events
 *
 * Design principles:
 * - All media is content-addressable (CID-based)
 * - Real-time processing with M5 coverage guarantees
 * - Streaming via Mesh Net trade routes with economic settlement
 * - Licensing via Financial Fabric (Vino vouchers, Treaty tokens)
 * - Provenance via Identity Fabric (credentials, delegation)
 * - Generative media via Audiogenomics (DNA → audio)
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all media operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef MEDIA_FABRIC_H
#define MEDIA_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "audio.h"
#include "video.h"
#include "codec.h"
#include "audiogenomics_pro.h"
#include "art_studio.h"
#include "games.h"
#include "font.h"
#include "theme.h"
#include "display.h"
#include "mesh_net.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define MF_MAX_STREAMS           256
#define MF_MAX_CODECS            64
#define MF_MAX_AUDIO_DEVICES     32
#define MF_MAX_VIDEO_DEVICES     32
#define MF_MAX_FONTS             128
#define MF_MAX_THEMES            64
#define MF_MAX_ART_PROJECTS      512
#define MF_MAX_GAME_SESSIONS     128
#define MF_MAX_NAME_LEN          64

/* ===== Media Stream ===== */

typedef enum {
    MF_STREAM_AUDIO    = 1,
    MF_STREAM_VIDEO    = 2,
    MF_STREAM_AV       = 3,   /* Audio + Video */
    MF_STREAM_DATA     = 4,   /* Data stream (subtitles, metadata) */
    MF_STREAM_GAME     = 5    /* Game state stream */
} mf_stream_type_t;

typedef struct mf_stream {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    mf_stream_type_t type;
    
    /* Source */
    uint32_t source_device_id;       /* Audio/Video device */
    uint32_t creator_id;             /* Identity Fabric identity */
    
    /* Codec */
    uint32_t codec_id;
    char codec_name[MF_MAX_NAME_LEN];
    uint32_t bitrate;                /* bps */
    uint32_t sample_rate;            /* Hz (audio) */
    uint32_t frame_rate;             /* fps (video) */
    uint16_t width, height;          /* Video resolution */
    uint8_t channels;                /* Audio channels */
    
    /* Mesh streaming */
    uint32_t mesh_network_id;
    uint32_t trade_route_id;
    uint64_t price_per_second;       /* Vino vouchers per second */
    uint8_t pricing_form;            /* Capital form */
    
    /* Licensing */
    uint32_t license_id;             /* Financial Fabric license */
    bool drm_enabled;
    uint8_t drm_key[32];
    
    /* Provenance */
    uint8_t content_cid[32];         /* Content ID */
    uint32_t credential_id;          /* Identity Fabric creator credential */
    
    /* Statistics */
    uint64_t bytes_streamed;
    uint64_t frames_encoded;
    uint64_t frames_decoded;
    uint64_t packets_lost;
    surplus_real_t avg_latency_ms;
    surplus_real_t avg_jitter_ms;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool live;
} mf_stream_t;

/* ===== Codec ===== */

typedef enum {
    MF_CODEC_AV1       = 1,
    MF_CODEC_VP9       = 2,
    MF_CODEC_HEVC      = 3,
    MF_CODEC_H264      = 4,
    MF_CODEC_OPUS      = 5,
    MF_CODEC_FLAC      = 6,
    MF_CODEC_AAC       = 7,
    MF_CODEC_MP3       = 8,
    MF_CODEC_RAW_PCM   = 9,
    MF_CODEC_RAW_YUV   = 10
} mf_codec_type_t;

typedef struct mf_codec {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    mf_codec_type_t type;
    
    /* Capabilities */
    bool encoder;
    bool decoder;
    uint32_t max_bitrate;
    uint32_t max_resolution_width;
    uint32_t max_resolution_height;
    uint32_t max_frame_rate;
    uint32_t max_sample_rate;
    uint8_t max_channels;
    
    /* Hardware acceleration */
    bool hw_accel_supported;
    uint32_t hw_device_id;           /* Smart Adapter / Yantra device */
    
    /* Quality presets */
    struct {
        char name[MF_MAX_NAME_LEN];
        uint32_t bitrate;
        uint32_t quality;            /* 0-100 */
    } presets[8];
    uint32_t num_presets;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} mf_codec_t;

/* ===== Audio Device ===== */

typedef struct mf_audio_device {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    
    /* Hardware */
    smart_device_t *sa_device;       /* Smart Adapter audio device */
    yf_device_t *yf_device;          /* Yantra audio device */
    
    /* Capabilities */
    uint32_t sample_rates[16];
    uint32_t num_sample_rates;
    uint8_t formats[16];             /* PCM, float, DSD, etc. */
    uint32_t num_formats;
    uint8_t max_channels_in;
    uint8_t max_channels_out;
    uint32_t max_bitrate;
    
    /* DSP */
    bool dsp_supported;
    uint32_t dsp_algorithms[16];
    uint32_t num_dsp_algorithms;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} mf_audio_device_t;

/* ===== Video Device ===== */

typedef struct mf_video_device {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    
    /* Hardware */
    smart_device_t *sa_device;
    yf_device_t *yf_device;
    
    /* Capabilities */
    struct {
        uint16_t width;
        uint16_t height;
        uint32_t frame_rate;
        uint8_t format;              /* YUV420, YUV422, RGB, etc. */
    } modes[32];
    uint32_t num_modes;
    
    /* Encoding */
    bool encoder_supported;
    uint32_t encoder_codecs[16];
    uint32_t num_encoder_codecs;
    
    /* Decoding */
    bool decoder_supported;
    uint32_t decoder_codecs[16];
    uint32_t num_decoder_codecs;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} mf_video_device_t;

/* ===== Art Project ===== */

typedef struct mf_art_project {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    uint32_t creator_id;
    
    /* Content */
    uint8_t content_cid[32];
    uint64_t content_size;
    
    /* Generative */
    bool generative;
    uint8_t dna_sequence_cid[32];    /* Audiogenomics DNA */
    uint32_t generation_params_cid[32];
    
    /* Licensing */
    uint32_t treaty_asset_id;        /* Financial Fabric treaty asset */
    uint64_t royalty_rate_bps;       /* Basis points */
    uint8_t royalty_form;            /* Capital form */
    
    /* Provenance */
    uint32_t credential_id;          /* Identity Fabric credential */
    uint64_t created_tick;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} mf_art_project_t;

/* ===== Game Session ===== */

typedef struct mf_game_session {
    uint32_t id;
    char name[MF_MAX_NAME_LEN];
    uint32_t host_id;
    
    /* Game state */
    uint8_t state_cid[32];
    uint64_t tick;
    uint32_t player_count;
    uint32_t max_players;
    
    /* Streaming */
    uint32_t stream_id;
    uint32_t mesh_network_id;
    uint32_t trade_route_id;
    
    /* Economy */
    uint64_t entry_fee;              /* Vino vouchers */
    uint8_t entry_fee_form;
    uint64_t prize_pool;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} mf_game_session_t;

/* ===== Media Fabric ===== */

typedef struct media_fabric {
    /* Core sub-modules */
    audio_subsystem_t audio;         /* Audio Subsystem */
    video_subsystem_t video;         /* Video Subsystem */
    codec_framework_t codec;         /* Codec Framework */
    audiogenomics_pro_t agp;         /* Audiogenomics Pro */
    art_studio_t art;                /* Art Studio */
    games_engine_t games;            /* Games Engine */
    font_subsystem_t font;           /* Font Subsystem */
    theme_engine_t theme;            /* Theme Engine */
    display_controller_t display;    /* Display Controller */
    
    /* Integration references */
    mesh_net_t *mesh;                /* Mesh Net */
    financial_fabric_t *financial;   /* Financial Fabric */
    identity_fabric_t *identity;     /* Identity Fabric */
    orbital_fabric_t *orbital;       /* Orbital Fabric */
    network_fabric_t *network;       /* Network Fabric */
    security_fabric_t *security;     /* Security Fabric */
    storage_fabric_t *storage;       /* Storage Fabric */
    
    /* Fabric-level state */
    mf_stream_t streams[MF_MAX_STREAMS];
    uint32_t num_streams;
    uint32_t next_stream_id;
    
    mf_codec_t codecs[MF_MAX_CODECS];
    uint32_t num_codecs;
    
    mf_audio_device_t audio_devices[MF_MAX_AUDIO_DEVICES];
    uint32_t num_audio_devices;
    
    mf_video_device_t video_devices[MF_MAX_VIDEO_DEVICES];
    uint32_t num_video_devices;
    
    mf_art_project_t art_projects[MF_MAX_ART_PROJECTS];
    uint32_t num_art_projects;
    
    mf_game_session_t game_sessions[MF_MAX_GAME_SESSIONS];
    uint32_t num_game_sessions;
    
    /* Global statistics */
    struct {
        uint64_t total_streams_created.
        uint64_t total_bytes_streamed.
        uint64_t total_codecs_registered.
        uint64_t total_audio_devices.
        uint64_t total_video_devices.
        uint64_t total_art_projects.
        uint64_t total_game_sessions.
        uint64_t total_streaming_revenue.
        uint64_t total_royalties_paid.
        uint64_t total_generative_media.
    } stats.
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation.
    bool global_safety_gate.
    
    /* M5 coordinates */
    m5_coords_t m5.
    surplus_real_t coverage_ratio.
    surplus_real_t min_coverage_ratio.
    
    /* Configuration */
    struct {
        bool auto_mesh_streaming.
        bool require_drm.
        bool auto_licensing.
        surplus_real_t min_stream_quality.
        uint32_t max_latency_ms.
    } config.
    
    bool initialized.
} media_fabric_t;

/* ===== API ===== */

/* Initialize the Media Fabric */
void mf_init(media_fabric_t *fabric,
             mesh_net_t *mesh,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             network_fabric_t *network,
             security_fabric_t *security,
             storage_fabric_t *storage);

/* Register built-in media modules */
void mf_register_builtins(media_fabric_t *fabric);

/* ===== Stream Management ===== */

int32_t mf_create_stream(media_fabric_t *fabric,
                         const char *name, mf_stream_type_t type,
                         uint32_t creator_id,
                         uint32_t codec_id,
                         uint32_t mesh_network_id,
                         uint64_t price_per_second, uint8_t pricing_form);

mf_stream_t *mf_get_stream(media_fabric_t *fabric, uint32_t stream_id);

int32_t mf_start_stream(media_fabric_t *fabric, uint32_t stream_id);
int32_t mf_stop_stream(media_fabric_t *fabric, uint32_t stream_id);

/* ===== Codec Management ===== */

int32_t mf_register_codec(media_fabric_t *fabric,
                          const char *name, mf_codec_type_t type,
                          bool encoder, bool decoder,
                          uint32_t max_bitrate, uint32_t max_resolution);

mf_codec_t *mf_get_codec(media_fabric_t *fabric, uint32_t codec_id);

/* ===== Audio/Video Devices ===== */

int32_t mf_register_audio_device(media_fabric_t *fabric,
                                 const char *name,
                                 smart_device_t *sa_device,
                                 yf_device_t *yf_device);

int32_t mf_register_video_device(media_fabric_t *fabric,
                                 const char *name,
                                 smart_device_t *sa_device,
                                 yf_device_t *yf_device);

/* ===== Art Studio ===== */

int32_t mf_create_art_project(media_fabric_t *fabric,
                              const char *name, uint32_t creator_id,
                              const uint8_t *content_cid, uint64_t content_size,
                              bool generative, const uint8_t *dna_cid);

int32_t mf_license_art(media_fabric_t *fabric, uint32_t project_id,
                       uint64_t royalty_rate_bps, uint8_t royalty_form);

/* ===== Games ===== */

int32_t mf_create_game_session(media_fabric_t *fabric,
                               const char *name, uint32_t host_id,
                               uint32_t max_players,
                               uint64_t entry_fee, uint8_t entry_fee_form);

int32_t mf_join_game_session(media_fabric_t *fabric,
                             uint32_t session_id, uint32_t player_id);

/* ===== Mesh Streaming Settlement ===== */

int32_t mf_settle_streaming(media_fabric_t *fabric,
                            uint32_t trade_route_id,
                            uint64_t current_cycle);

/* ===== Health & Attestation ===== */

int32_t mf_check_stream_health(media_fabric_t *fabric,
                               uint32_t stream_id,
                               void *health_out);

int32_t mf_check_global_health(media_fabric_t *fabric);

bool mf_global_safety_gate(media_fabric_t *fabric);

lpres_state_t mf_attest(media_fabric_t *fabric, uint32_t stream_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void mf_update_coverage(media_fabric_t *fabric);
bool mf_enforce_coverage(media_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void mf_get_stats(media_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t mf_get_attestation(media_fabric_t *fabric, uint32_t stream_id);
void mf_set_attestation(media_fabric_t *fabric, uint32_t stream_id, lpres_state_t state);

/* Utility */
const char *mf_lpres_state_name(lpres_state_t state);
const char *mf_stream_type_name(mf_stream_type_t type);
const char *mf_codec_type_name(mf_codec_type_t type);

#endif /* MEDIA_FABRIC_H */
