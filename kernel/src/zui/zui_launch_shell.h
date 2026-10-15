/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_launch_shell.h — the desktop's side of every app's launch screen: the
 * pictures and the sounds, for real.
 *
 * zui_launch_theme decides what each launch screen shows and when each cue
 * sounds; this is the piece that draws and plays them. A desktop session
 * makes one, attaches it with zui_launch_theme_attach(..., zui_ls_shell(ls),
 * ...), and calls zui_ls_pump once a frame. Nothing else is needed for every
 * app (the 407 in the catalogue and every app a person makes) to open on its
 * own live launch screen.
 *
 *   LS1 ICONS. The app's own icon, read from disk the first time the app
 *       opens: <icons>/<zxl_icon_file(app)> for an app in the catalogue
 *       (001-burrow.png, l2/145-dragon-archivist.png, ...), <icons>/<name>.png
 *       for an app a person made (its name in lower case, letters and digits
 *       only). The ZXV green it is drawn on is keyed out. A redrawn icon
 *       shows the next time it is read; nothing is built in. The last
 *       ZUI_LS_ICONS apps' icons are kept.
 *   LS2 LIGHT. As on the boot: the icon sits as a faint gold engraving until
 *       its part lights, then comes up in full. A system app's icon is held
 *       to the palette's gold, its own light and shade kept; an app a person
 *       made shows its own colours (zxl_icon_ink).
 *   LS3 LOGOS. The boot's five logos, <logos>/{36n9,acoto,ascw,smic,cis}-gold.png,
 *       read once (zui_ls_load_logos at the start of the session, or on the
 *       first launch), dim until their tile lights.
 *   LS4 SOUND. Every cue is synthesised as it is called for (zxl_synth) and
 *       fed to the audio device as it has room, so each begin, tick and end
 *       sounds when the real load reaches it. No audio device: the cues are
 *       still mixed (zui_ls_render) and nothing is lost.
 *   LS6 VECTORS FIRST. All graphics are native vector (the owner,
 *       2026-10-14). With a vector drawer in the config (zui_vec_draw_simple
 *       through a three-line adapter), each icon and logo is looked for first
 *       as ZXVG beside its PNG, the same name ending .zxvg (001-burrow.zxvg,
 *       l2/145-dragon-archivist.zxvg, logos/ascw-gold.zxvg), kept as bytes
 *       and drawn at the box's own size, sharp at any scale: faint until
 *       its part lights, then in full. Each is drawn once per size (on black
 *       and on white; the difference is its coverage) and blended from there
 *       every frame, so a launch frame is a blit, not a redraw; a box larger
 *       than the kept size is drawn live. A vector is drawn
 *       exactly as it was made, never tinted: each file carries its own
 *       plate and the palette (gold on ZXV green for the base apps, their
 *       own colours on gold above them; tools/zxvg/lint_icons.py holds the
 *       407 to it), and an app a person made keeps its own. The PNG is
 *       used only when there is no vector drawer, no .zxvg, or the .zxvg is
 *       malformed (then that picture falls back for good).
 *   LS5 MISSING FILES. An icon or logo that cannot be read leaves its place
 *       empty (the screen still runs, the sounds still play); it is tried
 *       again when the app next opens.
 *
 * Integer only, no allocation: the session owns the struct (about 2.3 MiB)
 * and the two buffers named in the config. */
#ifndef ZXV_ZUI_LAUNCH_SHELL_H
#define ZXV_ZUI_LAUNCH_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include "audio.h"
#include "zui_image.h"
#include "zui_launch_theme.h"
#include "zxl_synth.h"

#define ZUI_LS_ICON_PX  256 /* icons are kept fitted inside this       */
#define ZUI_LS_LOGO_PX  160 /* and logos inside this                    */
#define ZUI_LS_ICONS    4   /* icons kept: the apps opened most lately  */
#define ZUI_LS_PATH     256
#define ZUI_LS_VEC_ICON 16384 /* room for one icon's ZXVG (the 407 are under 5 KB) */

/* Read the whole file at `path` into buf (at most cap bytes). Returns its
 * length, or a negative number when it cannot be read or is larger. */
typedef int32_t (*zui_ls_read_fn)(void *ctx, const char *path, uint8_t *buf, uint32_t cap);

/* LS6: draw ZXVG bytes d[0..n) fitted into box (aspect kept, centred) at
 * strength 0..256, every paint held to tint's RGB when tint is not 0.
 * Returns 0, or -1 when the bytes are malformed. zui_vec_draw_simple with
 * its zui_vec_work_t as ctx. */
typedef int (*zui_ls_vec_fn)(void *ctx, zui_surface_t *s, const uint8_t *d, uint32_t n,
                             zui_rect_t box, uint32_t strength, uint32_t tint);

typedef struct {
    zui_ls_read_fn read;
    void *read_ctx;
    const char *icons_dir; /* the app icons folder                       */
    const char *logos_dir; /* the five gold logos' folder                */
    audio_device_t *audio; /* where the sounds go; NULL = nowhere         */
    uint8_t *file;         /* room for one PNG file while it is read      */
    uint32_t file_cap;
    uint8_t *work; /* zui_image_load's scratch (zui_image_work_size) */
    uint32_t work_cap;
    zui_ls_vec_fn vec; /* LS6; NULL = PNG only                         */
    void *vec_ctx;
    uint8_t *vec_store; /* room for the five logos' ZXVG (about 0.5 MB)  */
    uint32_t vec_store_cap;
} zui_ls_config_t;

enum { ZUI_LS_EMPTY = 0, ZUI_LS_LOADED, ZUI_LS_MISSING, ZUI_LS_VECTOR };

typedef struct {
    char app[ZUI_WM_NAME_MAX];
    uint8_t state;
    uint32_t age;
    zui_image_t img;
    uint8_t no_vec; /* its .zxvg was malformed: PNG from now on */
    uint32_t px[ZUI_LS_ICON_PX * ZUI_LS_ICON_PX];
    uint32_t vec_len;
    uint8_t vec[ZUI_LS_VEC_ICON];
} zui_ls_icon_t;

typedef struct {
    zui_ls_config_t cfg;
    zxl_player_t player;
    uint32_t stream; /* the launch sounds' stream on cfg.audio, 0 = none */
    zui_ls_icon_t icon[ZUI_LS_ICONS];
    uint32_t age;
    zui_image_t logo[ZUI_LT_LOGOS];
    uint8_t logo_state[ZUI_LT_LOGOS];
    uint32_t logo_px[ZUI_LT_LOGOS][ZUI_LS_LOGO_PX * ZUI_LS_LOGO_PX];
    const uint8_t *logo_vec[ZUI_LT_LOGOS];
    uint32_t logo_vec_len[ZUI_LT_LOGOS];
    uint8_t logo_no_vec[ZUI_LT_LOGOS];
    uint32_t vec_used;                                 /* of cfg.vec_store */
    uint32_t bake[2][ZUI_LS_ICON_PX * ZUI_LS_ICON_PX]; /* LS6: a vector drawn on black, on white */
    int16_t pcm[ZXL_SYN_BLOCK * 2];
    uint32_t frames_out; /* frames handed to the audio device so far */
} zui_launch_shell_t;

/* Set up; opens a 48 kHz stereo stream on cfg->audio when there is one. */
void zui_ls_init(zui_launch_shell_t *ls, const zui_ls_config_t *cfg);

/* The callbacks for zui_launch_theme_attach. */
zui_lt_shell_t zui_ls_shell(zui_launch_shell_t *ls);

/* LS3: read the five logos now. Returns how many were read. */
uint32_t zui_ls_load_logos(zui_launch_shell_t *ls);

/* LS4: once a frame. Mixes what is sounding and hands the audio device as
 * much as it has room for. Returns the frames handed over. */
uint32_t zui_ls_pump(zui_launch_shell_t *ls);

/* LS4 without a device: mix the next `frames` frames into lr. Returns the
 * cues still sounding. */
uint32_t zui_ls_render(zui_launch_shell_t *ls, int16_t *lr, uint32_t frames);

/* LS1: the icon file for app (its PNG), joined to dir, into out. Returns 0,
 * or -1 when it does not fit. */
int zui_ls_icon_path(const char *dir, const char *app, char *out, uint32_t cap);

#endif /* ZXV_ZUI_LAUNCH_SHELL_H */
