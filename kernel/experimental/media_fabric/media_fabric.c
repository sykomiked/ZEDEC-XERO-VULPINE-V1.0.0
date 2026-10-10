/* media_fabric.c — ZXV Media Fabric Compound Module Implementation
 *
 * Unifies all media modules: Audio, Video, Codec, Audiogenomics, Art Studio,
 * Games, Font, Theme, Display, with Mesh, Financial, Identity, Orbital,
 * Network, Security, and Storage fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "media_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void mf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void mf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int mf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t mf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void mf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t mf_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t mf_attest(media_fabric_t *fabric, uint32_t stream_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || stream_id >= fabric->num_streams) return LPRES_STATE_NEITHER;
    
    mf_stream_t *stream = &fabric->streams[stream_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t stream_att = stream->attestation;
    lpres_state_t coverage_att = (SR_CMP(stream->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, stream_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    stream->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void mf_init(media_fabric_t *fabric,
             mesh_net_t *mesh,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             network_fabric_t *network,
             security_fabric_t *security,
             storage_fabric_t *storage) {
    if (!fabric) return;
    
    mf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->mesh = mesh;
    fabric->financial = financial;
    fabric->identity = identity;
    fabric->orbital = orbital;
    fabric->network = network;
    fabric->security = security;
    fabric->storage = storage;
    
    /* Initialize sub-modules */
    /* audio_init(&fabric->audio); */
    /* video_init(&fabric->video); */
    /* codec_init(&fabric->codec); */
    /* audiogenomics_pro_init(&fabric->agp); */
    /* art_studio_init(&fabric->art); */
    /* games_init(&fabric->games); */
    /* font_init(&fabric->font); */
    /* theme_init(&fabric->theme); */
    /* display_init(&fabric->display); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(8.0);  /* Media/Knowledge rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = mf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.auto_mesh_streaming = true;
    fabric->config.require_drm = true;
    fabric->config.auto_licensing = true;
    fabric->config.min_stream_quality = SR_FROM_FLOAT(0.8);
    fabric->config.max_latency_ms = 100;
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
    
    /* Register built-in codecs */
    mf_register_codec(fabric, "AV1", MF_CODEC_AV1, true, true, 50000000, 7680);
    mf_register_codec(fabric, "VP9", MF_CODEC_VP9, true, true, 30000000, 7680);
    mf_register_codec(fabric, "HEVC", MF_CODEC_HEVC, true, true, 40000000, 7680);
    mf_register_codec(fabric, "H.264", MF_CODEC_H264, true, true, 20000000, 4096);
    mf_register_codec(fabric, "Opus", MF_CODEC_OPUS, true, true, 512000, 0);
    mf_register_codec(fabric, "FLAC", MF_CODEC_FLAC, true, true, 1411200, 0);
    mf_register_codec(fabric, "AAC", MF_CODEC_AAC, true, true, 320000, 0);
    mf_register_codec(fabric, "MP3", MF_CODEC_MP3, true, true, 320000, 0);
}

void mf_register_builtins(media_fabric_t *fabric) {
    if (!fabric) return;
}

/* ===== Stream Management ===== */

int32_t mf_create_stream(media_fabric_t *fabric,
                         const char *name, mf_stream_type_t type,
                         uint32_t creator_id,
                         uint32_t codec_id,
                         uint32_t mesh_network_id,
                         uint64_t price_per_second, uint8_t pricing_form) {
    if (!fabric || !name || fabric->num_streams >= MF_MAX_STREAMS) return -1;
    
    if_identity_t *creator = if_get_identity(fabric->identity, creator_id);
    if (!creator) return -1;
    
    mf_codec_t *codec = mf_get_codec(fabric, codec_id);
    if (!codec) return -1;
    
    mf_stream_t *stream = &fabric->streams[fabric->num_streams];
    mf_mem_set(stream, 0, sizeof(*stream));
    stream->id = fabric->next_stream_id++;
    
    mf_str_copy(stream->name, name, MF_MAX_NAME_LEN);
    stream->type = type;
    stream->creator_id = creator_id;
    stream->codec_id = codec_id;
    mf_str_copy(stream->codec_name, codec->name, MF_MAX_NAME_LEN);
    stream->mesh_network_id = mesh_network_id;
    stream->price_per_second = price_per_second;
    stream->pricing_form = pricing_form;
    
    /* Set codec parameters */
    stream->bitrate = codec->presets[0].bitrate;
    stream->sample_rate = 48000;
    stream->frame_rate = 30;
    stream->width = 1920;
    stream->height = 1080;
    stream->channels = 2;
    
    /* Generate content CID */
    for (int i = 0; i < 32; i++) stream->content_cid[i] = (uint8_t)(stream->id + i);
    
    /* Initialize M5 */
    stream->m5.omega = fabric->num_streams + 1;
    stream->m5.r = SR_FROM_FLOAT(8.0);
    stream->m5.ell = SR_ONE;
    stream->m5.phi = SR_ZERO;
    stream->m5.chi = 0;
    stream->coverage_ratio = mf_compute_coverage(&stream->m5);
    
    stream->attestation = LPRES_STATE_NEITHER;
    stream->active = true;
    stream->live = false;
    
    fabric->num_streams++;
    fabric->stats.total_streams_created++;
    
    return mf_attest(fabric, stream->id, 0x1000, stream, 0);
}

mf_stream_t *mf_get_stream(media_fabric_t *fabric, uint32_t stream_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_streams; i++) {
        if (fabric->streams[i].id == stream_id && fabric->streams[i].active) {
            return &fabric->streams[i];
        }
    }
    return NULL;
}

int32_t mf_start_stream(media_fabric_t *fabric, uint32_t stream_id) {
    if (!fabric) return -1;
    mf_stream_t *stream = mf_get_stream(fabric, stream_id);
    if (!stream) return -1;
    
    /* Create mesh trade route for streaming */
    if (fabric->config.auto_mesh_streaming && fabric->mesh) {
        /* mn_create_route(...); */
        stream->trade_route_id = 1;  /* Placeholder */
    }
    
    /* Create license via Financial Fabric */
    if (fabric->config.auto_licensing && fabric->financial) {
        /* ff_create_license(...); */
        stream->license_id = 1;  /* Placeholder */
    }
    
    stream->live = true;
    return mf_attest(fabric, stream_id, 0x2000, stream, 0);
}

int32_t mf_stop_stream(media_fabric_t *fabric, uint32_t stream_id) {
    if (!fabric) return -1;
    mf_stream_t *stream = mf_get_stream(fabric, stream_id);
    if (!stream) return -1;
    
    stream->live = false;
    return mf_attest(fabric, stream_id, 0x3000, stream, 0);
}

/* ===== Codec Management ===== */

int32_t mf_register_codec(media_fabric_t *fabric,
                          const char *name, mf_codec_type_t type,
                          bool encoder, bool decoder,
                          uint32_t max_bitrate, uint32_t max_resolution) {
    if (!fabric || !name || fabric->num_codecs >= MF_MAX_CODECS) return -1;
    
    mf_codec_t *codec = &fabric->codecs[fabric->num_codecs];
    mf_mem_set(codec, 0, sizeof(*codec));
    codec->id = fabric->num_codecs;
    
    mf_str_copy(codec->name, name, MF_MAX_NAME_LEN);
    codec->type = type;
    codec->encoder = encoder;
    codec->decoder = decoder;
    codec->max_bitrate = max_bitrate;
    codec->max_resolution_width = max_resolution;
    codec->max_resolution_height = max_resolution;
    codec->max_frame_rate = 120;
    codec->max_sample_rate = 192000;
    codec->max_channels = 8;
    
    /* Default presets */
    codec->presets[0] = (typeof(codec->presets[0])){"high", max_bitrate, 90};
    codec->presets[1] = (typeof(codec->presets[0])){"medium", max_bitrate / 2, 70};
    codec->presets[2] = (typeof(codec->presets[0])){"low", max_bitrate / 4, 50};
    codec->num_presets = 3;
    
    /* Initialize M5 */
    codec->m5.omega = fabric->num_codecs + 1;
    codec->m5.r = SR_FROM_FLOAT(8.0);
    codec->m5.ell = SR_ONE;
    codec->m5.phi = SR_ZERO;
    codec->m5.chi = 0;
    codec->coverage_ratio = mf_compute_coverage(&codec->m5);
    
    codec->attestation = LPRES_STATE_NEITHER;
    codec->active = true;
    
    fabric->num_codecs++;
    fabric->stats.total_codecs_registered++;
    
    return mf_attest(fabric, 0xFFFFFFFF, 0x4000 | codec->id, codec, 0);
}

mf_codec_t *mf_get_codec(media_fabric_t *fabric, uint32_t codec_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_codecs; i++) {
        if (fabric->codecs[i].id == codec_id && fabric->codecs[i].active) {
            return &fabric->codecs[i];
        }
    }
    return NULL;
}

/* ===== Audio/Video Devices ===== */

int32_t mf_register_audio_device(media_fabric_t *fabric,
                                 const char *name,
                                 smart_device_t *sa_device,
                                 yf_device_t *yf_device) {
    if (!fabric || !name || fabric->num_audio_devices >= MF_MAX_AUDIO_DEVICES) return -1;
    
    mf_audio_device_t *dev = &fabric->audio_devices[fabric->num_audio_devices];
    mf_mem_set(dev, 0, sizeof(*dev));
    dev->id = fabric->num_audio_devices;
    
    mf_str_copy(dev->name, name, MF_MAX_NAME_LEN);
    dev->sa_device = sa_device;
    dev->yf_device = yf_device;
    
    /* Default capabilities */
    dev->sample_rates[0] = 48000;
    dev->sample_rates[1] = 96000;
    dev->sample_rates[2] = 192000;
    dev->num_sample_rates = 3;
    dev->formats[0] = 1;  /* PCM 16-bit */
    dev->formats[1] = 2;  /* PCM 24-bit */
    dev->formats[2] = 3;  /* Float 32-bit */
    dev->num_formats = 3;
    dev->max_channels_in = 8;
    dev->max_channels_out = 8;
    dev->max_bitrate = 24576000;  /* 24.576 Mbps */
    
    /* Initialize M5 */
    dev->m5.omega = fabric->num_audio_devices + 1;
    dev->m5.r = SR_FROM_FLOAT(8.0);
    dev->m5.ell = SR_ONE;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_ratio = mf_compute_coverage(&dev->m5);
    
    dev->attestation = LPRES_STATE_NEITHER;
    dev->active = true;
    
    fabric->num_audio_devices++;
    fabric->stats.total_audio_devices++;
    
    return mf_attest(fabric, 0xFFFFFFFF, 0x5000 | dev->id, dev, 0);
}

int32_t mf_register_video_device(media_fabric_t *fabric,
                                 const char *name,
                                 smart_device_t *sa_device,
                                 yf_device_t *yf_device) {
    if (!fabric || !name || fabric->num_video_devices >= MF_MAX_VIDEO_DEVICES) return -1;
    
    mf_video_device_t *dev = &fabric->video_devices[fabric->num_video_devices];
    mf_mem_set(dev, 0, sizeof(*dev));
    dev->id = fabric->num_video_devices;
    
    mf_str_copy(dev->name, name, MF_MAX_NAME_LEN);
    dev->sa_device = sa_device;
    dev->yf_device = yf_device;
    
    /* Default modes */
    dev->modes[0] = (typeof(dev->modes[0])){1920, 1080, 60, 1};
    dev->modes[1] = (typeof(dev->modes[0])){3840, 2160, 30, 1};
    dev->modes[2] = (typeof(dev->modes[0])){7680, 4320, 30, 1};
    dev->num_modes = 3;
    
    dev->encoder_supported = true;
    dev->encoder_codecs[0] = MF_CODEC_AV1;
    dev->encoder_codecs[1] = MF_CODEC_HEVC;
    dev->encoder_codecs[2] = MF_CODEC_VP9;
    dev->num_encoder_codecs = 3;
    
    dev->decoder_supported = true;
    dev->decoder_codecs[0] = MF_CODEC_AV1;
    dev->decoder_codecs[1] = MF_CODEC_HEVC;
    dev->decoder_codecs[2] = MF_CODEC_VP9;
    dev->num_decoder_codecs = 3;
    
    /* Initialize M5 */
    dev->m5.omega = fabric->num_video_devices + 1;
    dev->m5.r = SR_FROM_FLOAT(8.0);
    dev->m5.ell = SR_ONE;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_ratio = mf_compute_coverage(&dev->m5);
    
    dev->attestation = LPRES_STATE_NEITHER;
    dev->active = true;
    
    fabric->num_video_devices++;
    fabric->stats.total_video_devices++;
    
    return mf_attest(fabric, 0xFFFFFFFF, 0x6000 | dev->id, dev, 0);
}

/* ===== Art Studio ===== */

int32_t mf_create_art_project(media_fabric_t *fabric,
                              const char *name, uint32_t creator_id,
                              const uint8_t *content_cid, uint64_t content_size,
                              bool generative, const uint8_t *dna_cid) {
    if (!fabric || !name || fabric->num_art_projects >= MF_MAX_ART_PROJECTS) return -1;
    
    if_identity_t *creator = if_get_identity(fabric->identity, creator_id);
    if (!creator) return -1;
    
    mf_art_project_t *project = &fabric->art_projects[fabric->num_art_projects];
    mf_mem_set(project, 0, sizeof(*project));
    project->id = fabric->num_art_projects;
    
    mf_str_copy(project->name, name, MF_MAX_NAME_LEN);
    project->creator_id = creator_id;
    if (content_cid) mf_mem_copy(project->content_cid, content_cid, 32);
    project->content_size = content_size;
    project->generative = generative;
    if (dna_cid) mf_mem_copy(project->dna_sequence_cid, dna_cid, 32);
    
    /* Initialize M5 */
    project->m5.omega = fabric->num_art_projects + 1;
    project->m5.r = SR_FROM_FLOAT(8.0);
    project->m5.ell = SR_ONE;
    project->m5.phi = SR_ZERO;
    project->m5.chi = 0;
    project->coverage_ratio = mf_compute_coverage(&project->m5);
    
    project->attestation = LPRES_STATE_NEITHER;
    project->active = true;
    
    fabric->num_art_projects++;
    fabric->stats.total_art_projects++;
    if (generative) fabric->stats.total_generative_media++;
    
    return mf_attest(fabric, creator_id, 0x7000 | project->id, project, 0);
}

int32_t mf_license_art(media_fabric_t *fabric, uint32_t project_id,
                       uint64_t royalty_rate_bps, uint8_t royalty_form) {
    if (!fabric || project_id >= fabric->num_art_projects) return -1;
    
    mf_art_project_t *project = &fabric->art_projects[project_id];
    if (!project->active) return -1;
    
    project->royalty_rate_bps = royalty_rate_bps;
    project->royalty_form = royalty_form;
    
    /* Create treaty asset via Financial Fabric */
    if (fabric->financial) {
        /* ff_tokenize_treaty(...); */
        project->treaty_asset_id = 1;  /* Placeholder */
    }
    
    return mf_attest(fabric, project->creator_id, 0x8000 | project_id, project, 0);
}

/* ===== Games ===== */

int32_t mf_create_game_session(media_fabric_t *fabric,
                               const char *name, uint32_t host_id,
                               uint32_t max_players,
                               uint64_t entry_fee, uint8_t entry_fee_form) {
    if (!fabric || !name || fabric->num_game_sessions >= MF_MAX_GAME_SESSIONS) return -1;
    
    if_identity_t *host = if_get_identity(fabric->identity, host_id);
    if (!host) return -1;
    
    mf_game_session_t *session = &fabric->game_sessions[fabric->num_game_sessions];
    mf_mem_set(session, 0, sizeof(*session));
    session->id = fabric->num_game_sessions;
    
    mf_str_copy(session->name, name, MF_MAX_NAME_LEN);
    session->host_id = host_id;
    session->max_players = max_players;
    session->entry_fee = entry_fee;
    session->entry_fee_form = entry_fee_form;
    session->tick = 0;
    
    /* Initialize M5 */
    session->m5.omega = fabric->num_game_sessions + 1;
    session->m5.r = SR_FROM_FLOAT(8.0);
    session->m5.ell = SR_ONE;
    session->m5.phi = SR_ZERO;
    session->m5.chi = 0;
    session->coverage_ratio = mf_compute_coverage(&session->m5);
    
    session->attestation = LPRES_STATE_NEITHER;
    session->active = true;
    
    fabric->num_game_sessions++;
    fabric->stats.total_game_sessions++;
    
    return mf_attest(fabric, host_id, 0x9000 | session->id, session, 0);
}

int32_t mf_join_game_session(media_fabric_t *fabric,
                             uint32_t session_id, uint32_t player_id) {
    if (!fabric || session_id >= fabric->num_game_sessions) return -1;
    
    mf_game_session_t *session = &fabric->game_sessions[session_id];
    if (!session->active || session->player_count >= session->max_players) return -1;
    
    if_identity_t *player = if_get_identity(fabric->identity, player_id);
    if (!player) return -1;
    
    /* Check entry fee */
    if (session->entry_fee > 0) {
        if (player->balances[session->entry_fee_form - 1] < session->entry_fee) return -1;
        player->balances[session->entry_fee_form - 1] -= session->entry_fee;
        session->prize_pool += session->entry_fee;
    }
    
    session->player_count++;
    
    return mf_attest(fabric, player_id, 0xA000 | session_id, session, 0);
}

/* ===== Mesh Streaming Settlement ===== */

int32_t mf_settle_streaming(media_fabric_t *fabric,
                            uint32_t trade_route_id,
                            uint64_t current_cycle) {
    if (!fabric || !fabric->mesh) return -1;
    
    /* Settle via Mesh Token */
    /* mn_send_data(&fabric->mesh, trade_route_id, data_size, current_cycle); */
    /* mesh_token_settle(...); */
    
    fabric->stats.total_streaming_revenue += 1000;  /* Placeholder */
    
    return mf_attest(fabric, 0xFFFFFFFF, 0xB000 | trade_route_id, NULL, 0);
}

/* ===== Health & Attestation ===== */

int32_t mf_check_stream_health(media_fabric_t *fabric,
                               uint32_t stream_id,
                               void *health_out) {
    if (!fabric) return -1;
    mf_stream_t *stream = mf_get_stream(fabric, stream_id);
    if (!stream) return -1;
    
    /* Update coverage */
    stream->coverage_ratio = mf_compute_coverage(&stream->m5);
    
    /* Check quality metrics */
    if (stream->avg_latency_ms > fabric->config.max_latency_ms) {
        stream->attestation = LPRES_STATE_BOTH;
        return -1;
    }
    
    if (stream->packets_lost > stream->frames_encoded / 100) {  /* >1% loss */
        stream->attestation = LPRES_STATE_BOTH;
        return -1;
    }
    
    stream->attestation = LPRES_STATE_TRUE;
    return 0;
}

int32_t mf_check_global_health(media_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_streams; i++) {
        if (fabric->streams[i].active) {
            if (mf_check_stream_health(fabric, fabric->streams[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_art_projects; i++) {
        if (fabric->art_projects[i].active) {
            fabric->art_projects[i].coverage_ratio = mf_compute_coverage(&fabric->art_projects[i].m5);
            if (SR_CMP(fabric->art_projects[i].coverage_ratio, fabric->min_coverage_ratio) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool mf_global_safety_gate(media_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void mf_update_coverage(media_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = mf_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_streams; i++) {
        if (fabric->streams[i].active) {
            fabric->streams[i].coverage_ratio = mf_compute_coverage(&fabric->streams[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_codecs; i++) {
        if (fabric->codecs[i].active) {
            fabric->codecs[i].coverage_ratio = mf_compute_coverage(&fabric->codecs[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_audio_devices; i++) {
        if (fabric->audio_devices[i].active) {
            fabric->audio_devices[i].coverage_ratio = mf_compute_coverage(&fabric->audio_devices[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_video_devices; i++) {
        if (fabric->video_devices[i].active) {
            fabric->video_devices[i].coverage_ratio = mf_compute_coverage(&fabric->video_devices[i].m5);
        }
    }
}

bool mf_enforce_coverage(media_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_streams; i++) {
        if (fabric->streams[i].active) {
            if (SR_CMP(fabric->streams[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void mf_get_stats(media_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    mf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t mf_get_attestation(media_fabric_t *fabric, uint32_t stream_id) {
    if (!fabric || stream_id >= fabric->num_streams) return LPRES_STATE_NEITHER;
    return fabric->streams[stream_id].attestation;
}

void mf_set_attestation(media_fabric_t *fabric, uint32_t stream_id, lpres_state_t state) {
    if (!fabric || stream_id >= fabric->num_streams) return;
    fabric->streams[stream_id].attestation = state;
}

/* ===== Utility ===== */

const char *mf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *mf_stream_type_name(mf_stream_type_t type) {
    static const char *names[] = {"UNUSED", "AUDIO", "VIDEO", "AV", "DATA", "GAME"};
    if (type <= MF_STREAM_GAME) return names[type];
    return "UNKNOWN";
}

const char *mf_codec_type_name(mf_codec_type_t type) {
    static const char *names[] = {"UNKNOWN", "AV1", "VP9", "HEVC", "H264", "OPUS", "FLAC", "AAC", "MP3", "RAW_PCM", "RAW_YUV"};
    if (type <= MF_CODEC_RAW_YUV) return names[type];
    return "UNKNOWN";
}
