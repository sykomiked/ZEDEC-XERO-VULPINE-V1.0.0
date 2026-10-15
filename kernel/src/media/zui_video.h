/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_video.h — opening video files and stepping through their frames.
 *
 * A video file is a container (how the bytes are laid out: Y4M, AVI,
 * MP4/QuickTime, GIF) holding a stream in a codec (how each picture is
 * compressed). Playing one is: demux the container into access units, decode
 * each unit into a picture, show the picture at its presentation time. This
 * file does the first two for every combination that integer kernel code can
 * honestly do, and hands the rest to a platform decoder through one adapter
 * (zui_video_hw_t) or refuses with a reason. See VIDEO.md for the plan and
 * the reasoning about codecs, hardware and patents.
 *
 * Rules:
 *   VD1 One iterator for everything: zui_video_open, then zui_video_next_frame
 *       until it returns 0; each frame is 0xAARRGGBB pixels (stride w) in
 *       the caller's work buffer with its presentation time and duration in
 *       microseconds.
 *   VD2 Freestanding, like zx_image (IM1): no allocation, no floating point;
 *       the work buffer holds the current frame and the decoder's scratch,
 *       and zui_video_probe says how big it must be.
 *   VD3 Fail closed (IM2): every box, chunk and table is bounds checked
 *       before it is used; a sample that points outside the file, a frame
 *       of the wrong size or a malformed picture is an error, never a read
 *       out of range. A file cut short plays the frames it has and then
 *       returns ZX_IMG_E_TRUNC.
 *   VD4 A codec we cannot decode is never guessed at. Without an adapter it
 *       is refused with ZUI_VID_E_CODEC and a reason naming the codec; with
 *       an adapter whose supports() says yes, its access units go to the
 *       adapter unchanged (MP4 samples with their codec configuration
 *       record, AVI chunks as stored).
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   Decoded here:  Y4M (YUV4MPEG2) 8-bit 4:2:0 (jpeg, mpeg2, paldv siting),
 *                  4:2:2, 4:4:4 and mono, BT.601 limited range (full range
 *                  with XCOLORRANGE=FULL), chroma by nearest sample;
 *                  Motion-JPEG in AVI and in MP4/QuickTime ('jpeg', 'mjpa'),
 *                  through the zx_image JPEG decoder (baseline and
 *                  progressive, default tables when a frame has none);
 *                  uncompressed 24- and 32-bit RGB in AVI;
 *                  animated GIF through zui_gif (delays under 2 cs play as
 *                  10 cs, as browsers do).
 *   Demuxed, then handed to an adapter or refused: H.264/AVC, H.265/HEVC,
 *                  AV1, VP9, VP8 and MPEG-4 Part 2 in AVI or MP4/QuickTime.
 *   Not done:      Matroska/WebM, MPEG-TS/PS, Ogg, fragmented MP4 (moof),
 *                  OpenDML AVI beyond the first RIFF (files over 1 GiB),
 *                  MP4 edit lists and composition offsets (frames come in
 *                  decode order with decode times unless the adapter reorders),
 *                  interlaced (two-field) Motion-JPEG, 10-bit Y4M, audio of
 *                  any kind, and seeking other than rewinding to the start.
 */
#ifndef ZXV_ZUI_VIDEO_H
#define ZXV_ZUI_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "zx_image.h"

#define ZUI_VID_E_CODEC (-9) /* the codec needs a decoder we do not have (VD4) */

typedef enum {
    ZUI_VID_NONE = 0,
    ZUI_VID_Y4M,
    ZUI_VID_AVI,
    ZUI_VID_MP4, /* ISO base media: MP4, QuickTime MOV, 3GP */
    ZUI_VID_GIF,
    ZUI_VID_MKV, /* recognised and refused */
    ZUI_VID_TS,  /* recognised and refused */
    ZUI_VID_OGG  /* recognised and refused */
} zui_vid_container_t;

typedef enum {
    ZUI_VC_NONE = 0,
    ZUI_VC_YUV,   /* raw planar YUV (Y4M)          */
    ZUI_VC_RGB,   /* raw packed RGB (AVI DIB)      */
    ZUI_VC_MJPEG, /* Motion-JPEG                   */
    ZUI_VC_GIF,   /* GIF LZW frames                */
    ZUI_VC_H264,
    ZUI_VC_HEVC,
    ZUI_VC_AV1,
    ZUI_VC_VP9,
    ZUI_VC_VP8,
    ZUI_VC_MPEG4, /* MPEG-4 Part 2 (DivX, Xvid)    */
    ZUI_VC_OTHER
} zui_vid_codec_t;

/* The adapter for a platform decoder (VideoToolbox when hosted on macOS,
 * VA-API on Linux, MediaCodec, a GPU's fixed-function block on bare metal).
 * Nothing in the kernel implements one; a host registers it by passing it to
 * zui_video_probe / zui_video_open. */
typedef struct {
    const char *name;
    void *ctx;
    /* Can it decode this codec at this size? */
    bool (*supports)(void *ctx, zui_vid_codec_t codec, uint32_t w, uint32_t h);
    /* Start a stream. config: the codec configuration record from the
     * container (avcC, hvcC, av1C or vpcC body), or NULL with n 0 (AVI:
     * the stream is Annex B / in-band). 0, or < 0 to refuse. */
    int (*open)(void *ctx, zui_vid_codec_t codec, uint32_t w, uint32_t h, const uint8_t *config,
                uint32_t n);
    /* One access unit in (au NULL, n 0: end of stream, drain); at most one
     * picture out into px (stride in pixels) with its presentation time.
     * 1: a picture was written, 0: it needs more input, < 0: error. */
    int (*decode)(void *ctx, const uint8_t *au, uint32_t n, uint64_t pts_us, uint32_t *px,
                  uint32_t stride, uint64_t *out_pts_us);
    void (*close)(void *ctx);
} zui_video_hw_t;

typedef struct {
    zui_vid_container_t container;
    zui_vid_codec_t codec;
    uint32_t fourcc; /* as stored (AVI handler / MP4 sample entry), else 0 */
    uint32_t w, h;
    uint32_t frames;             /* as the container says (0: unknown)            */
    uint32_t rate_num, rate_den; /* nominal frames per second (0/0: variable)    */
    uint64_t duration_us;        /* 0: unknown                                    */
    uint32_t work;               /* bytes zui_video_open needs                    */
    bool hw;                     /* frames will come from the adapter             */
    const char *why;             /* reason for a failure, else ""                 */
} zui_video_info_t;

typedef struct {
    const uint32_t *px; /* w * h, stride w, inside the work buffer           */
    uint32_t w, h;
    uint32_t index;  /* 0-based                                           */
    uint64_t pts_us; /* presentation time                                 */
    uint64_t dur_us; /* how long it stays up (0: unknown)                 */
} zui_video_frame_t;

typedef struct {
    zui_video_info_t info;
    const uint8_t *d;
    uint32_t n;
    uint32_t *frame;
    uint8_t *scratch;
    uint32_t scratchlen;
    const zui_video_hw_t *hw;
    bool hw_open, eos;
    uint32_t index;
    uint32_t pos, end; /* Y4M / AVI cursor and limit                         */
    uint64_t t_us;     /* GIF: running time                                  */
    /* Y4M */
    uint32_t y4m_cw, y4m_ch, first;
    uint8_t y4m_cs;
    bool y4m_full;
    /* AVI */
    uint32_t scale, rate, stream, bpp;
    bool top_down;
    /* MP4: sample tables (offsets of the box bodies in d) */
    uint32_t timescale, nsamples, stsz, stsz_const, stco, nchunks, stsc, nstsc, stts, nstts;
    uint32_t config, nconfig;
    bool co64;
    uint32_t sample, chunk, chunk_left, stsc_i, stts_i, stts_left;
    uint64_t chunk_off, dts;
    /* GIF */
    zui_gif_t gif;
} zui_video_t;

/* Container, codec, size and the work memory open will need; hw may be
 * NULL. A codec that neither we nor hw can decode fails with
 * ZUI_VID_E_CODEC (info is still filled, with why). */
int zui_video_probe(const uint8_t *d, uint32_t n, const zui_video_hw_t *hw, zui_video_info_t *info);

int zui_video_open(zui_video_t *v, const uint8_t *d, uint32_t n, void *work, uint32_t worklen,
                   const zui_video_hw_t *hw);
/* 1: *f is the next frame; 0: the end; < 0: an error (the frames before it
 * were good). */
int zui_video_next_frame(zui_video_t *v, zui_video_frame_t *f);
void zui_video_rewind(zui_video_t *v);
void zui_video_close(zui_video_t *v); /* closes the adapter stream, if any */

const char *zui_video_codec_name(zui_vid_codec_t c);

#endif /* ZXV_ZUI_VIDEO_H */
