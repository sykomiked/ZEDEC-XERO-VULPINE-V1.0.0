/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_desktop_main.c — the hosted ZXV desktop as a program (zxv_desktop.h).
 *
 *   zxv-desktop [--backend metal|x11|vulkan|d3d11] [--rpc SOCKET]
 *               [--font FILE] [--mono FILE] [--size W H]
 *               [--launch APP]... [--frames N] [--screenshot OUT.ppm]
 *
 * --rpc names zxvd's control socket (else $ZXV_RPC; zxvd --rpc PATH makes
 * one). --launch opens an app's window at start (its rail name: Burrow,
 * Quillon, Glint, Spindle, Murmuration; "all" opens every one). --frames N
 * runs N steps and exits; --screenshot writes the desktop's own surface as a
 * binary PPM on exit (what every backend shows, byte for byte: zui_present.h
 * P2). Built by build_zxv_desktop.sh. Exit 0 on close, 3 when no present
 * layer opened, 4 when the window went away, 5 when the shot failed.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_desktop.h"

static int write_ppm(const char *path, const zui_surface_t *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", (int) s->w, (int) s->h);
    for (int32_t y = 0; y < s->h; y++)
        for (int32_t x = 0; x < s->w; x++) {
            uint32_t p = s->px[(size_t) y * (size_t) s->stride + (size_t) x];
            unsigned char rgb[3] = {(unsigned char) (p >> 16), (unsigned char) (p >> 8),
                                    (unsigned char) p};
            fwrite(rgb, 1, 3, f);
        }
    return fclose(f);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: zxv-desktop [--backend NAME] [--rpc SOCKET] [--font FILE] [--mono FILE]\n"
            "                   [--size W H] [--launch APP]... [--frames N]\n"
            "                   [--screenshot OUT.ppm]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    zxv_desktop_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    const char *shot = NULL, *launch[16];
    uint32_t nlaunch = 0;
    long frames = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc)
            cfg.backend = argv[++i];
        else if (!strcmp(argv[i], "--rpc") && i + 1 < argc)
            cfg.rpc = argv[++i];
        else if (!strcmp(argv[i], "--font") && i + 1 < argc)
            cfg.font = argv[++i];
        else if (!strcmp(argv[i], "--mono") && i + 1 < argc)
            cfg.mono = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--launch") && i + 1 < argc && nlaunch < 16)
            launch[nlaunch++] = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            cfg.w_pt = (int32_t) strtol(argv[++i], NULL, 10);
            cfg.h_pt = (int32_t) strtol(argv[++i], NULL, 10);
        } else
            usage();
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setlocale(LC_CTYPE, ""); /* the input method commits text in the locale */
    const char *why = NULL;
    zxv_desktop_t *d = zxv_desktop_open(&cfg, &why);
    if (!d) {
        fprintf(stderr, "zxv-desktop: %s\n", why ? why : "cannot open");
        return 3;
    }
    zui_present_t *p = zxv_desktop_present(d);
    printf("desktop %s %ux%u scale_q8=%u %s\n", p ? p->ops->name : "headless",
           p ? p->screen.px_w : 0, p ? p->screen.px_h : 0, p ? p->screen.scale_q8 : 0,
           zxv_desktop_connected(d) ? "zxvd connected" : "zxvd not running");
    for (uint32_t k = 0; k < nlaunch; k++)
        for (uint32_t a = 0; a < zxv_desktop_apps(d); a++)
            if (!strcmp(launch[k], "all") || !strcmp(launch[k], zxv_desktop_app_name(d, a)))
                printf("launch %s window %u\n", zxv_desktop_app_name(d, a),
                       zxv_desktop_launch(d, a));
    int rc = 1;
    long n = 0;
    while (frames < 0 || n < frames) {
        rc = zxv_desktop_step(d, 16);
        n++;
        if (rc != 1) break;
    }
    printf("exit %s steps=%ld\n", rc == 0 ? "close" : rc < 0 ? "lost" : "frames", n);
    int out = rc < 0 ? 4 : 0;
    if (shot && write_ppm(shot, zxv_desktop_surface(d)) != 0) {
        fprintf(stderr, "zxv-desktop: cannot write %s\n", shot);
        out = 5;
    }
    zxv_desktop_close(d);
    return out;
}
