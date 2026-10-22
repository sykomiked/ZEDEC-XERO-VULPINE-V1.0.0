// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// boot.js — booting ZXV in a browser is still booting it (the owner's "space
// between spaces"). The page runs the same boot screen as bare metal and the
// desktop app: zui_boot built to WebAssembly draws every frame and makes
// every sound, and this file only does what a browser must do for it:
//
//   1. read boot.json from the sealed root: the five logos and the files that
//      make up the system, each with its size and SHA-256;
//   2. stream every file, telling the boot screen each byte as it arrives,
//      so the bars move exactly as fast as the system really loads;
//   3. check each file against its hash before counting the boot ready; a
//      file that does not match stops the boot where it is (no gong);
//   4. put each frame on the canvas and each block of sound on the speakers.
//
// Nothing is a video or a recording. The page reads the sealed files where
// they sit (paths relative to this page); it carries no copy of any of them.

'use strict';

const RATE = 48000;
const PHASE = {
    LOADING : 0,
    OPENING : 1,
    DONE : 2,
    FAILED : 3
};
const LOGOS = [ '36n9', 'acoto', 'ascw', 'smic', 'cis' ]; // left to right, the crest in the centre
const STALL_MS = 15000; // a file that sends nothing for this long has stopped

function say(text)
{
    const el = document.getElementById('why');
    if (el) el.textContent = text;
}

function pref(key, fallback)
{
    try {
        const v = localStorage.getItem(key);
        return v === null ? fallback : v === '1';
    } catch (e) {
        return fallback;
    }
}

async function sha256hex(bytes)
{
    const d = await crypto.subtle.digest('SHA-256', bytes);
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('');
}

// A fetch that is given up when nothing arrives for STALL_MS: arm() each
// time something does. Its failures come back as words, never as a wait.
function watched(path)
{
    const stop = new AbortController();
    let timer = 0;
    const w = {
        signal : stop.signal,
        arm()
        {
            clearTimeout(timer);
            timer = setTimeout(() => stop.abort(), STALL_MS);
        },
        end()
        {
            clearTimeout(timer);
        },
        why(err)
        {
            if (stop.signal.aborted) return new Error(`${path}: nothing arrived for ${STALL_MS / 1000} s, so the load stopped`);
            if (err instanceof TypeError) return new Error(`${path}: the connection broke (${err.message})`);
            return err;
        }
    };
    w.arm();
    return w;
}

// Stream one file, reporting every chunk; resolves to its bytes once its
// hash matches.
async function fetchChecked(entry, onBytes)
{
    const w = watched(entry.path);
    const out = new Uint8Array(entry.size);
    let at = 0;
    try {
        const res = await fetch(entry.path, {cache : 'no-cache', signal : w.signal});
        if (!res.ok) throw new Error(`${entry.path}: ${res.status} ${res.statusText}`);
        const reader = res.body.getReader();
        for (;;) {
            w.arm();
            const {done, value} = await reader.read();
            if (done) break;
            if (at + value.length > entry.size) {
                reader.cancel();
                throw new Error(`${entry.path}: longer than its ${entry.size} bytes`);
            }
            out.set(value, at);
            at += value.length;
            onBytes(value.length);
        }
    } catch (err) {
        throw w.why(err);
    } finally {
        w.end();
    }
    if (at !== entry.size) throw new Error(`${entry.path}: only ${at} of its ${entry.size} bytes arrived`);
    if (entry.sha256 && (await sha256hex(out)) !== entry.sha256.toLowerCase())
        throw new Error(`${entry.path}: does not match its SHA-256`);
    return out;
}

class Sound
{
    constructor(zb, memory)
    {
        this.zb = zb;
        this.memory = memory;
        this.ctx = null;
        this.next = 0;
        this.pulled = 0; // frames pulled while no sound could play
        try {
            this.ctx = new AudioContext({sampleRate : RATE});
        } catch (e) {
            this.ctx = null;
        }
        // A browser lets sound start only after the person touches the page.
        const wake = () => {
            if (this.ctx && this.ctx.state !== 'running') this.ctx.resume();
        };
        addEventListener('pointerdown', wake);
        addEventListener('keydown', wake);
    }

    pull(frames)
    {
        const p = this.zb.zb_audio(frames);
        return new Int16Array(this.memory.buffer, p, frames * 2);
    }

    // Keep the sound at the screen's pace: when it cannot play, the frames are
    // still pulled and dropped, so every cue stays where the load put it.
    pump(elapsedMs)
    {
        if (!this.ctx || this.ctx.state !== 'running') {
            let n = Math.round((elapsedMs * RATE) / 1000);
            while (n > 0) {
                const k = Math.min(n, 8192);
                this.pull(k);
                n -= k;
            }
            this.next = 0;
            return;
        }
        const now = this.ctx.currentTime;
        if (this.next < now) this.next = now + 0.03;
        while (this.next < now + 0.12) {
            const frames = 2048;
            const s = this.pull(frames);
            const buf = this.ctx.createBuffer(2, frames, RATE);
            const l = buf.getChannelData(0), r = buf.getChannelData(1);
            for (let i = 0; i < frames; i++) {
                l[i] = s[2 * i] / 32768;
                r[i] = s[2 * i + 1] / 32768;
            }
            const src = this.ctx.createBufferSource();
            src.buffer = buf;
            src.connect(this.ctx.destination);
            src.start(this.next);
            this.next += frames / RATE;
        }
    }
}

async function boot()
{
    const canvas = document.getElementById('screen');
    const g = canvas.getContext('2d');

    // the boot screen itself
    const ww = watched('zxv_boot.wasm');
    let wasm;
    try {
        wasm = await WebAssembly.instantiateStreaming(fetch('zxv_boot.wasm', {signal : ww.signal}));
    } catch (err) {
        throw ww.why(err);
    } finally {
        ww.end();
    }
    const zb = wasm.instance.exports;
    const memory = zb.memory;
    const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
    zb.zb_init(reduce ? 1 : 0, pref('zxv.sound.boot', true) ? 1 : 0);

    let rgba = 0, img = null;
    function size()
    {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        let w = Math.round(innerWidth * dpr), h = Math.round(innerHeight * dpr);
        const k = Math.min(1, 1920 / w, 1200 / h);
        w = Math.max(1, Math.floor(w * k));
        h = Math.max(1, Math.floor(h * k));
        rgba = zb.zb_size(w, h);
        w = zb.zb_width();
        h = zb.zb_height();
        canvas.width = w;
        canvas.height = h;
        img = new ImageData(new Uint8ClampedArray(memory.buffer, rgba, w * h * 4), w, h);
    }
    size();
    addEventListener('resize', size);

    const sound = new Sound(zb, memory);
    let last = performance.now(), handed = false, manifest = null;
    function frame(now)
    {
        const ms = Math.max(0, Math.min(250, now - last));
        last = now;
        zb.zb_frame(Math.round(ms));
        // the view over wasm memory is rebuilt if memory ever moved
        if (img.data.buffer !== memory.buffer) size();
        g.putImageData(img, 0, 0);
        sound.pump(ms);
        if (zb.zb_phase() === PHASE.DONE && !handed) {
            handed = true;
            dispatchEvent(new CustomEvent('zxv:booted'));
            if (manifest && manifest.next) location.replace(manifest.next);
        }
        requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);

    // the system, from the sealed root
    try {
        const wm = watched('boot.json');
        try {
            const res = await fetch('boot.json', {cache : 'no-cache', signal : wm.signal});
            if (!res.ok) throw new Error(`boot.json: ${res.status} ${res.statusText}`);
            manifest = await res.json();
        } catch (err) {
            if (err instanceof SyntaxError) throw new Error('boot.json is not a whole manifest');
            throw wm.why(err);
        } finally {
            wm.end();
        }
        if (!crypto.subtle)
            throw new Error(
                'this page needs a secure origin (https or localhost) to check the files');
        const logos = LOGOS.map((k) => manifest.logos && manifest.logos[k]).filter(Boolean);
        const files = [...logos, ...(manifest.system || []) ];
        let total = 0;
        for (const f of files) {
            if (!f.path || !(f.size >= 0))
                throw new Error('boot.json: every file needs a path and a size');
            total += f.size;
        }
        let done = 0;
        zb.zb_load(0, total);
        const onBytes = (n) => {
            done += n;
            zb.zb_load(Math.min(done, total), total);
        };
        // the logos first, so each tile has its logo before its gold comes up
        for (let i = 0; i < LOGOS.length; i++) {
            const entry = manifest.logos && manifest.logos[LOGOS[i]];
            if (!entry) continue; // its tile stays carved and empty
            const bytes = await fetchChecked(entry, onBytes);
            const p = zb.zb_logo_buf(bytes.length);
            if (!p) throw new Error(`${entry.path}: too large`);
            new Uint8Array(memory.buffer, p, bytes.length).set(bytes);
            if (zb.zb_logo_set(i, bytes.length) !== 0)
                throw new Error(`${entry.path}: not a ZXVG file`);
        }
        for (const entry of manifest.system || []) await fetchChecked(entry, onBytes);
        zb.zb_ready();
    } catch (err) {
        zb.zb_fail();
        say(`The system could not finish loading. ${err.message}`);
    }
}

boot().catch((err) => say(`The boot screen could not start. ${err.message}`));
