/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_stft_kern.h — the body of the GPU kernel for the analyser's heavy
 * stage: one workgroup (CUDA block) per analysis frame, ZK_NT threads,
 * written once for CUDA C++ and GLSL (the same rules as zxva_stft_math.h,
 * whose functions it calls; ZXVA_K_SRC wraps it for the same reasons).
 *
 * The including side provides, before the body, with these exact names:
 *   p           the launch parameters, a struct (CUDA: by value; GLSL: the
 *               push-constant block) with members n N L num den c0lo c0hi
 *               lenlo lenhi k0lo k0hi floor_mag rel nf (all 32-bit unsigned;
 *               64-bit values split low/high)
 *   x win tw    input buffers (int): the channel (Q23), the window, the
 *               twiddles (N/2 cos then N/2 sin)
 *   scr         scratch (int), 2N per frame of the batch
 *   re im       outputs (int), N/2 + 1 per frame
 *   mag mask top outputs (uint): magnitudes, peak-mask words, frame maxima
 *   s_top       one workgroup-shared uint
 *   ZK_TID ZK_GID ZK_NT  thread in the group, group (= frame of the batch),
 *               threads per group
 *   ZK_BARRIER()          a workgroup barrier that also orders the group's
 *               global-memory writes (CUDA __syncthreads; GLSL
 *               memoryBarrierBuffer + memoryBarrierShared + barrier)
 *   ZK_ATOMIC_MAX(v)      s_top = max(s_top, v), atomically
 *
 * It computes exactly what zxva_stft_cpu (zxva_analyse.c) computes, in the
 * same steps; only the order in which independent elements are done
 * differs, and every element is a pure function of values the previous
 * barrier fixed, so the bits are the same on every device. */

ZXVA_K_SRC(
    zu tid = ZK_TID;
    zu f = ZK_GID;
    zu nt = ZK_NT;
    if (f < p.nf) {
        zu N = p.N;
        zu H = N / 2u + 1u;
        zu mw = (H + 31u) / 32u;
        zu sb = f * 2u * N;
        zw kk = (ZKC(zw, p.k0lo) | (ZKC(zw, p.k0hi) << 32)) + ZKC(zw, f);
        zl c0 = ZKC(zl, ZKC(zw, p.c0lo) | (ZKC(zw, p.c0hi) << 32));
        zl len = ZKC(zl, ZKC(zw, p.lenlo) | (ZKC(zw, p.lenhi) << 32));
        zl c = zk_centre(kk, p.num, p.den, c0);
        if (tid == 0u) s_top = 0u;
        for (zu j = tid; j < N; j += nt) {
            zl so = zk_src_offset(j, p.n, N);
            zi v = 0;
            if (so >= ZKC(zl, 0)) {
                zl idx = c - ZKC(zl, p.n >> 1) + so;
                if (idx >= ZKC(zl, 0) && idx < len) v = zk_windowed(x[ZKC(zu, idx)], win[ZKC(zu, so)]);
            }
            zu r = zk_bitrev(j, p.L);
            scr[sb + r] = v;
            scr[sb + N + r] = 0;
        }
        ZK_BARRIER();
        for (zu st = 1u; st <= p.L; st++) {
            zu h = 1u << (st - 1u);
            for (zu b = tid; b < N / 2u; b += nt) {
                zu i0 = (b / h) * 2u * h + b % h;
                zu i1 = i0 + h;
                zu t = (b % h) * (N / (2u * h));
                zi cw = tw[t];
                zi sw = tw[N / 2u + t];
                zi br = scr[sb + i1];
                zi bi = scr[sb + N + i1];
                zi vr = zk_twmul_re(br, bi, cw, sw);
                zi vi = zk_twmul_im(br, bi, cw, sw);
                zi ar = scr[sb + i0];
                zi ai = scr[sb + N + i0];
                scr[sb + i0] = zk_half_sum(ar, vr);
                scr[sb + N + i0] = zk_half_sum(ai, vi);
                scr[sb + i1] = zk_half_diff(ar, vr);
                scr[sb + N + i1] = zk_half_diff(ai, vi);
            }
            ZK_BARRIER();
        }
        for (zu j = tid; j < H; j += nt) {
            zi a = scr[sb + j];
            zi bb = scr[sb + N + j];
            zu m = zk_mag(a, bb);
            re[f * H + j] = a;
            im[f * H + j] = bb;
            mag[f * H + j] = m;
            ZK_ATOMIC_MAX(m);
        }
        ZK_BARRIER();
        zu tp = s_top;
        if (tid == 0u) top[f] = tp;
        zu thr = zk_threshold(tp, p.floor_mag, p.rel);
        for (zu w = tid; w < mw; w += nt) {
            zu bits = 0u;
            for (zu q = 0u; q < 32u; q++) {
                zu j = w * 32u + q;
                if (j >= 1u && j + 1u < H &&
                    zk_is_peak(mag[f * H + j - 1u], mag[f * H + j], mag[f * H + j + 1u], thr) != 0u)
                    bits = bits | (1u << q);
            }
            mask[f * mw + w] = bits;
        }
    }
)
