<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Video in ZEDEC-XERO: what plays, what does not, and why

The system's own graphics are vector (ZXVG, `zui/zui_vec.h`). Photos and
downloads are raster and open through `zx_image.h`. This note covers moving
pictures: how a video file becomes frames on screen, which formats integer
kernel code can honestly decode, and which ones need something bigger.

## How a video plays

A video file has two layers:

1. **Container**: how the bytes are laid out. It says which streams exist
   (video, audio, subtitles), the frame size, the timing, and where each
   compressed picture (an *access unit*) sits. Examples: AVI (RIFF), MP4 and
   QuickTime MOV (ISO base media), Matroska/WebM, MPEG-TS, Y4M.
2. **Codec**: how each picture is compressed. Examples: Motion-JPEG, H.264,
   H.265/HEVC, AV1, VP9, raw YUV.

Playback is a pipeline:

```
bytes --demux--> access units + times --decode--> ARGB frame --present--> screen
      (zui_video)                      (in kernel, or the       (zx_image_blit at
                                        platform adapter)        the frame's pts)
```

`zui_video.h` is the demux-and-decode half behind one iterator:
`zui_video_open`, then `zui_video_next_frame` until it returns 0. Each frame
comes with its presentation time and duration in microseconds; the shell
shows it with `zx_image_blit` (scaled, letterboxed, alpha-blended) when the
clock reaches that time. There is no audio path yet, so there is no A/V sync;
frames are paced by their own timestamps.

## What can be decoded in integer kernel code

The test for "realistic in the kernel" is: small enough to audit, integer
only, no SIMD needed for useful speed, and no patent pool. That gives:

| Format | Container | Codec | Status |
|---|---|---|---|
| Y4M (YUV4MPEG2) | Y4M | raw 8-bit 4:2:0 / 4:2:2 / 4:4:4 / mono | **decoded** (BT.601, limited or full range) |
| Motion-JPEG | AVI, MP4/MOV | JPEG per frame (`MJPG`, `jpeg`, `mjpa`, ...) | **decoded** with the zx_image JPEG decoder, incl. frames without Huffman tables |
| Uncompressed | AVI | 24/32-bit RGB DIB | **decoded** |
| Animated GIF | GIF | LZW | **decoded** (disposal, transparency, delays) |

Motion-JPEG reuses the still-image JPEG decoder, which is libjpeg-compatible
(integer islow IDCT, libjpeg colour conversion and fancy upsampling) and is
bit-exact with libjpeg-turbo on every test fixture. MJPEG is what webcams,
many cameras and screen recorders produce, and it is simple: every frame is
an independent JPEG, so seeking and damage are trivial.

## What needs a large decoder or the platform's hardware

| Codec | Typical container | Why not in the kernel |
|---|---|---|
| H.264 / AVC | MP4, MOV, MKV, TS | full decoder: CABAC/CAVLC, intra and motion-compensated prediction, deblocking, B-frame reordering; tens of thousands of lines, SIMD for HD speed; patent pool |
| H.265 / HEVC | MP4, MOV, MKV | larger again (CTUs, SAO, more prediction modes); several patent pools |
| AV1 | MP4, WebM | dav1d is the reference-quality software decoder: large, heavily hand-written assembly for speed |
| VP9 / VP8 | WebM, MP4 | libvpx-sized decoders; VP9 needs SIMD for HD |
| MPEG-4 Part 2 (DivX/Xvid) | AVI | legacy; a medium decoder, old patent pool |

A full software decoder for any of these is a very large, attack-exposed
parser running on untrusted input. That does not belong in kernel space. The
two honest options are:

1. **The platform's decoder behind an adapter** (`zui_video_hw_t`):
   - *macOS host*: VideoToolbox (`VTDecompressionSession`, format description
     built from the `avcC`/`hvcC` record the MP4 demuxer hands over; BGRA
     output copied into the frame). Takes whole access units, so the adapter
     is thin.
   - *Linux host*: VA-API. Note VA-API is a **low-level** API: the caller must
     parse sequence/picture/slice headers and fill parameter buffers, so a
     VA-API adapter needs an H.264/HEVC *header* parser (far smaller than a
     decoder, but not nothing), or it can sit on a wrapper such as GStreamer
     or FFmpeg's hwaccel in user space. Some distributions build Mesa without
     H.264/HEVC VA-API support for patent reasons, so availability varies.
     V4L2 *stateful* decoders (common on ARM SoCs) take whole access units
     like VideoToolbox; V4L2 *stateless* ones need parsed headers like VA-API.
   - *Windows host*: Media Foundation / D3D11 video decoding (whole units).
   - *Bare metal / VM*: no decoder driver exists today; a GPU's fixed-function
     block or a virtio video device (still a draft in the virtio
     specification) would back the adapter.
2. **A user-space decoder process** (sandboxed, outside the kernel) that
   implements the same adapter interface over IPC. This is where a software
   AV1 (dav1d) or VP9 decoder would live if one is ever shipped.

The adapter contract (`zui_video.h`): `supports(codec, w, h)`, `open(codec,
w, h, config record)`, `decode(access unit, pts) -> 0 or 1 picture + its
pts` (NULL unit drains at the end), `close`. Access units go through
unchanged: MP4 samples (length-prefixed NAL units, with the `avcC`/`hvcC`/
`av1C`/`vpcC` record passed at open) and AVI chunks as stored. The decoder
returns pictures in presentation order with their own timestamps, so B-frame
reordering is its business. With no adapter, or one whose `supports()` says
no, the stream is refused with `ZUI_VID_E_CODEC` and a reason naming the
codec. Nothing is ever guessed at.

## Patents and licences (orientation, not legal advice)

- **H.264/AVC**: essential patents are licensed through a pool (Via LA,
  formerly MPEG LA) plus some independent holders. Many patents have expired
  and the rest expire over the coming years, unevenly by country. Shipping a
  software H.264 decoder needs a check of the current position.
- **H.265/HEVC**: several pools (Access Advance, Via LA) and independent
  licensors; the most fragmented licensing situation of the common codecs.
- **AV1**: released by the Alliance for Open Media under a royalty-free patent
  licence. A third party (Sisvel) runs a pool asserting AV1 and VP9 patents;
  AOMedia's members dispute that a licence is needed.
- **VP9 / VP8**: Google's royalty-free patent grants (VP8 also covered by a
  2013 agreement with MPEG LA).
- **JPEG baseline / Motion-JPEG, GIF (LZW), Y4M**: no live patents (the LZW
  patents expired in 2003-2004; baseline JPEG is unencumbered). The JPEG
  arithmetic-coding mode is refused for scope, not for patents.
- Using a **platform decoder** usually means the platform vendor (OS or GPU
  maker) carries the codec licence for its hardware/software decoder, which is
  one more reason to route H.264/HEVC through the adapter rather than ship our
  own.

## What is implemented now

- `zui_video.{h,c}`: Y4M reader; AVI (RIFF) demuxer (first video stream,
  `##dc`/`##db` chunks, `LIST rec` groups, dropped-frame empty chunks);
  MP4/QuickTime demuxer (first video track; `stsd`, `stts`, `stsc`, `stsz`,
  `stco`/`co64`; 64-bit box sizes); Motion-JPEG and raw RGB decoding; animated
  GIF through `zui_gif`; codec identification for H.264, HEVC, AV1, VP9, VP8
  and MPEG-4 Part 2; the platform adapter and the refusal.
- Every table and chunk is bounds checked before use (VD3); the test cuts
  every container at every length and flips bytes under AddressSanitizer.

## Not implemented (yet)

Matroska/WebM, MPEG-TS/PS and Ogg demuxing (recognised and refused);
fragmented MP4 (`moof`); MP4 edit lists and composition offsets (frames come
in decode order unless the adapter reorders); OpenDML AVI past the first
RIFF (files over 1 GiB); interlaced (two-field) Motion-JPEG; 10-bit Y4M;
audio; seeking other than rewind; any adapter implementation (the interface
is defined and tested with a stand-in).
