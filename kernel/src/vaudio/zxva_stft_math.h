/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_stft_math.h — the integer arithmetic of the analyser's heavy stage
 * (windowed, zero-padded, zero-phase radix-2 FFT, magnitudes, per-frame peak
 * mask), written ONCE in the subset that is C11, CUDA C++ and GLSL 4.50
 * with GL_ARB_gpu_shader_int64 at the same time.
 *
 * One source, four builds (docs/GPU_STRATEGY.md G2: integer-exact on every
 * backend, the GPU kernel is a port of the C reference's steps, shared as
 * source where it can be):
 *   - zxva_analyse.c (the CPU reference) includes it as C, with
 *     ZXVA_K_SRC(...) -> ... ;
 *   - zxva_cuda.c stringifies it (ZXVA_K_SRC(...) -> "...") into the source
 *     NVRTC compiles at run time;
 *   - gen_zxva_vk_spv.sh strips the first and last line and glslang compiles
 *     it inside zxva_vk.comp;
 *   - zxva_metal.m (Apple only) stringifies it like CUDA.
 * Rules for the body: no preprocessor lines and no `#`; only scalars (no
 * pointers, arrays or structs, which GLSL cannot pass); casts written
 * ZKC(type, value); types zl (int64) zw (uint64) zi (int32) zu (uint32);
 * functions declared ZK_FN; no literal wider than 31 bits. Right shifts of
 * negative values go through zk_sar (floor), so no backend's choice
 * matters. Integer sums are exact in any order, and every function here is
 * pure, so the result is the same bits on every device.
 *
 * The stage (per analysis frame k, centre c = c0 + k * num / den rounded
 * half to even (num / den = the hop, 1 for the partial analysis, rate / 100
 * for the 10 ms noise frames), window length n, FFT size N = 2^L >= n):
 *   fill   buffer slot j (0 <= j < N) takes input sample c - n/2 + s(j)
 *          with s(j) = n/2 + j for j < n/2, j - (N - n/2) for j >= N - n/2,
 *          and nothing (0) between: the window's centre lands on slot 0, so
 *          a bin's phase is the phase at the frame centre ("zero phase").
 *          The value is floor(x * w / 2^24) (x Q23 samples, w Q30 window)
 *          and is stored at bitrev(j): a decimation-in-time FFT in place.
 *   stage  for s = 1..L, butterfly b (0 <= b < N/2): h = 2^(s-1),
 *          i0 = (b / h) * 2h + b % h, i1 = i0 + h, twiddle index
 *          t = (b % h) * (N / 2h), W = cos - i sin (Q30 tables);
 *          v = x[i1] * W rounded (floor((p + 2^29) / 2^30));
 *          x[i0] = floor((x[i0] + v) / 2), x[i1] = floor((x[i0] - v) / 2).
 *          The halving per stage keeps |x| < 2^30: the output is X / N.
 *   mag    m = floor(sqrt(re^2 + im^2)) for bins 0..N/2.
 *   mask   bin b in 1..N/2-1 is a candidate when m[b] > m[b-1],
 *          m[b] >= m[b+1] and m[b] > max(floor, top / rel) where top is the
 *          frame's largest m (the rule tools/zxva/wav2zxva.py's Peaks.find
 *          applies in dB). */

ZXVA_K_SRC(

    ZK_FN zl zk_sar(zl v, zi s) { return v >= ZKC(zl, 0) ? (v >> s) : ~((~v) >> s); }

    ZK_FN zu zk_bitrev(zu j, zu L) {
        zu r = 0u;
        for (zu i = 0u; i < L; i++) {
            r = (r << 1) | (j & 1u);
            j = j >> 1;
        }
        return r;
    }

    ZK_FN zl zk_centre(zw k, zu num, zu den, zl c0) {
        zw t = k * ZKC(zw, num);
        zw q = t / ZKC(zw, den);
        zw r = t - q * ZKC(zw, den);
        if (r * ZKC(zw, 2) > ZKC(zw, den) ||
            (r * ZKC(zw, 2) == ZKC(zw, den) && (q & ZKC(zw, 1)) != ZKC(zw, 0)))
            q = q + ZKC(zw, 1);
        return c0 + ZKC(zl, q);
    }

    ZK_FN zl zk_src_offset(zu j, zu n, zu N) {
        zu h = n >> 1;
        if (j < h) return ZKC(zl, h + j);
        if (j >= N - h) return ZKC(zl, j - (N - h));
        return ZKC(zl, -1);
    }

    ZK_FN zi zk_windowed(zi x, zi w) { return ZKC(zi, zk_sar(ZKC(zl, x) * ZKC(zl, w), 24)); }

    ZK_FN zi zk_twmul_re(zi br, zi bi, zi c, zi s) {
        zl p = ZKC(zl, br) * ZKC(zl, c) + ZKC(zl, bi) * ZKC(zl, s);
        return ZKC(zi, zk_sar(p + (ZKC(zl, 1) << 29), 30));
    }

    ZK_FN zi zk_twmul_im(zi br, zi bi, zi c, zi s) {
        zl p = ZKC(zl, bi) * ZKC(zl, c) - ZKC(zl, br) * ZKC(zl, s);
        return ZKC(zi, zk_sar(p + (ZKC(zl, 1) << 29), 30));
    }

    ZK_FN zi zk_half_sum(zi a, zi v) { return ZKC(zi, zk_sar(ZKC(zl, a) + ZKC(zl, v), 1)); }

    ZK_FN zi zk_half_diff(zi a, zi v) { return ZKC(zi, zk_sar(ZKC(zl, a) - ZKC(zl, v), 1)); }

    ZK_FN zu zk_isqrt(zw v) {
        zw r = ZKC(zw, 0);
        zw b = ZKC(zw, 1) << 62;
        while (b > v) b = b >> 2;
        while (b != ZKC(zw, 0)) {
            if (v >= r + b) {
                v = v - (r + b);
                r = (r >> 1) + b;
            } else {
                r = r >> 1;
            }
            b = b >> 2;
        }
        return ZKC(zu, r);
    }

    ZK_FN zu zk_mag(zi re, zi im) {
        zl a = ZKC(zl, re);
        zl b = ZKC(zl, im);
        return zk_isqrt(ZKC(zw, a * a + b * b));
    }

    ZK_FN zu zk_threshold(zu top, zu floor_mag, zu rel) {
        zu t = top / rel;
        return t > floor_mag ? t : floor_mag;
    }

    ZK_FN zu zk_is_peak(zu ml, zu m, zu mr, zu thr) {
        return (m > ml && m >= mr && m > thr) ? 1u : 0u;
    }

)
