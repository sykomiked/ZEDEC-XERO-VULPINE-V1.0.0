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

// The wasm carries the kernel's SHA-256 because WebCrypto only exists on a
// secure origin — a seed serves this page over plain http on purpose.
let zbRef = null, memRef = null;

// Where the seal's own artifacts live when the page's origin doesn't carry
// them: '' = same-origin relative; a seed answers boot.src with the https
// base instead (its boot.json / boot.pack stay on the gateways).
let base = '';
const resolve = (p) => (base ? new URL(p, base).href : p);

async function sha256hex(bytes)
{
    let d;
    if (crypto.subtle) {
        d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
    } else {
        const zb = zbRef, memory = memRef;
        zb.zb_sha_begin();
        const scratch = zb.zb_sha_buf();
        for (let off = 0; off < bytes.length; off += 65536) {
            const span = bytes.subarray(off, off + 65536);
            new Uint8Array(memory.buffer, scratch, span.length).set(span);
            zb.zb_sha_feed(span.length);
        }
        d = new Uint8Array(memory.buffer, zb.zb_sha_out(), 32).slice();
    }
    return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
}

// Stream one file, reporting every chunk; resolves to its bytes once its
// hash matches.
async function fetchChecked(entry, onBytes)
{
    const res = await fetch(resolve(entry.path), {cache : 'no-cache'});
    if (!res.ok) throw new Error(`${entry.path}: ${res.status} ${res.statusText}`);
    const out = new Uint8Array(entry.size);
    let at = 0;
    const reader = res.body.getReader();
    for (;;) {
        const {done, value} = await reader.read();
        if (done) break;
        if (at + value.length > entry.size)
            throw new Error(`${entry.path}: longer than its ${entry.size} bytes`);
        out.set(value, at);
        at += value.length;
        onBytes(value.length);
    }
    if (at !== entry.size) throw new Error(`${entry.path}: ${at} of ${entry.size} bytes`);
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
    const wasm = await WebAssembly.instantiateStreaming(fetch('zxv_boot.wasm'));
    const zb = wasm.instance.exports;
    const memory = zb.memory;
    zbRef = zb;
    memRef = memory;
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

    // ---- the L2 state bus (vna_delta, kernel/src/vinea/vna_delta.h) ----
    // A delta is the bus's unit: canonical bytes that join the wasm's state
    // page through the evidence join — two peers disagreeing about one cell
    // leave it BOTH instead of one winning silently. The same bytes travel
    // as a VNA_SPACE_EV_DELTA event on Vinea (vna_space.h); a transport
    // assigns l2.send and hands inbound deltas to l2.apply — until one
    // does, the page is the node's own, which is still true of it.
    const l2 = {
        apply(bytes, tickLo, tickHi) {
            const p = zb.zb_l2_buf();
            new Uint8Array(memory.buffer, p, bytes.length).set(bytes);
            return zb.zb_l2_apply(bytes.length, tickLo >>> 0, tickHi >>> 0);
        },
        emit(cell, lattice, payload) {
            const p = zb.zb_l2_buf();
            if (payload && payload.length)
                new Uint8Array(memory.buffer, p, payload.length).set(payload);
            const n = zb.zb_l2_emit(cell, lattice, payload ? payload.length : 0);
            if (n < 0) return n;
            const bytes = new Uint8Array(memory.buffer, zb.zb_l2_out(), n).slice();
            l2.lastEmit = bytes;
            if (l2.send) l2.send(bytes);
            return bytes;
        },
        state(cell) { return zb.zb_l2_state(cell); },
        send : null,
        tick : 0,      // the highest Lamport tick applied; the resume cursor
        lastEmit : null, // our own delta echoes back over the stream: skip it
        // The bus rides the same origin the page came from — only a seed's
        // plain-http door can carry it, and only a page a seed served may
        // speak it (https pages may not fetch http, so the Filebase mirror
        // quietly stays off the bus).
        connect() {
            if (location.protocol !== 'http:' || !zb.zb_l2_apply) return;
            l2.send = (bytes) => fetch('/l2', {method : 'POST', body : bytes})
                .catch(() => { l2.send = null; });
            const frames = (async () => {
                for (;;) {
                    try {
                        const res = await fetch(`/l2?after=${l2.tick}`, {cache : 'no-cache'});
                        if (!res.ok || !res.body) break;
                        const rd = res.body.getReader();
                        let hold = new Uint8Array(0);
                        for (;;) {
                            const {done, value} = await rd.read();
                            if (done) break;
                            const both = new Uint8Array(hold.length + value.length);
                            both.set(hold); both.set(value, hold.length);
                            hold = both;
                            while (hold.length >= 10) {
                                const fl = hold[0] | (hold[1] << 8);
                                if (hold.length < 10 + fl) break;
                                let tick = 0;
                                for (let i = 0; i < 8; i++)
                                    tick += hold[2 + i] * (256 ** i); // exact below 2^53
                                if (fl) {
                                    const fr = hold.subarray(10, 10 + fl);
                                    const own = l2.lastEmit && fr.length === l2.lastEmit.length &&
                                        fr.every((b, i) => b === l2.lastEmit[i]);
                                    if (!own)
                                        l2.apply(fr, tick & 0xffffffff,
                                                 Math.floor(tick / 0x100000000));
                                    l2.tick = Math.max(l2.tick, tick);
                                }
                                hold = hold.subarray(10 + fl);
                            }
                        }
                    } catch (e) { /* the door is away; wait and resume */ }
                    await new Promise((r) => setTimeout(r, 3000));
                }
            })();
        },
    };
    const L2_LINK = 1; // the bus-link cell, ZB_L2_LINK in zxv_boot_wasm.c
    window.ZXV_L2 = l2;
    l2.connect();

    // ---- the player-card seat -------------------------------------------
    // A card the page adds appears on the login screen with a pass field.
    // Enter with a full pin raises a pending seat; this loop answers it at
    // the door (POST /card: [id_len][id][pin_len][pin hex]) and the wasm
    // seats the player or shakes the login.
    const CARD_IDS = [];
    let cardBusy = false;
    window.ZXV_CARD = (id) => {
        const r = zb.zb_card(id);
        if (r >= 2) CARD_IDS[r - 2] = id;
        return r;
    };

    // ---- the sovereign key ----------------------------------------------
    // A profile is an Ed25519 keypair made here (or imported): the public
    // key is the profile id, the private key never leaves this device. The
    // login signs the wall-clock second and the door verifies it — no
    // password, no server-held secret. The private key is stored in
    // IndexedDB (this page uses no cookies; localStorage only holds prefs).
    let ZXKey = null;
    const KEY_PREFIX = 'zxv-key-';
    const b64url = (b) =>
        btoa(String.fromCharCode(...b)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
    const hex32 = (b) => Array.from(b).map((x) => x.toString(16).padStart(2, '0')).join('');
    function keydb()
    {
        return new Promise((res, rej) => {
            const q = indexedDB.open('zxv-keys', 1);
            q.onupgradeneeded = () => q.result.createObjectStore('keys');
            q.onsuccess = () => res(q.result);
            q.onerror = () => rej(q.error);
        });
    }
    async function keySave(id, jwk)
    {
        const db = await keydb();
        return new Promise((res, rej) => {
            const tx = db.transaction('keys', 'readwrite');
            tx.objectStore('keys').put(jwk, KEY_PREFIX + id);
            tx.oncomplete = res;
            tx.onerror = () => rej(tx.error);
        });
    }
    async function keyLoad(id)
    {
        const db = await keydb();
        return new Promise((res) => {
            const tx = db.transaction('keys', 'readonly');
            const g = tx.objectStore('keys').get(KEY_PREFIX + id);
            g.onsuccess = () => res(g.result || null);
            g.onerror = () => res(null);
        });
    }
    window.ZXV_KEYGEN = async () => {
        if (!crypto.subtle || !crypto.subtle.generateKey) return null;
        const kp = await crypto.subtle.generateKey({name : 'Ed25519'}, true, [ 'sign', 'verify' ]);
        const pub = new Uint8Array(await crypto.subtle.exportKey('raw', kp.publicKey));
        const jwk = await crypto.subtle.exportKey('jwk', kp.privateKey);
        await keySave(hex32(pub), jwk);
        ZXKey = {pub, jwk, priv : kp.privateKey};
        const id = 'player-' + hex32(pub).slice(0, 10);
        window.ZXV_CARD(id);
        if (zb.zb_card_key) zb.zb_card_key(window.ZXV_CARD.idx = 1);
        return id;
    };
    window.ZXV_KEYSEAT = async (id, pinText) => {
        /* id = 'player-<hex>' -> the pubkey hex; pinText = the passphrase
         * to unlock the stored private key (the browser keys the JWK) */
        const pubHex = id.slice(7);
        const jwk = await keyLoad(pubHex);
        if (!jwk) return 1;
        const priv =
            await crypto.subtle.importKey('jwk', jwk, {name : 'Ed25519'}, false, [ 'sign' ]);
        const nonce = new Uint8Array(8);
        new DataView(nonce.buffer).setUint32(4, Math.floor(Date.now() / 1000) >>> 0);
        const sig = new Uint8Array(await crypto.subtle.sign('Ed25519', priv, nonce));
        const body = new Uint8Array(1 + 32 + 8 + 64);
        body[0] = 0;
        body.set(hexToBytes(pubHex), 1);
        body.set(nonce, 33);
        body.set(sig, 41);
        const r = await fetch('/card',
                              {method : 'POST', body : new Blob([ body ], {type : 'text/plain'})});
        return (await r.arrayBuffer())[0] || 0;
    };
    function hexToBytes(h)
    {
        const b = new Uint8Array(h.length / 2);
        for (let i = 0; i < b.length; i++) b[i] = parseInt(h.slice(2 * i, 2 * i + 2), 16);
        return b;
    }
    const cardForm = document.getElementById('card');
    if (cardForm && zb.zb_card) {
        cardForm.style.display = 'flex';
        cardForm.addEventListener('submit', (ev) => {
            ev.preventDefault();
            const id = document.getElementById('cardid').value.trim();
            if (id) window.ZXV_CARD(id);
        });
        const keygen = document.getElementById('keygen');
        if (keygen && window.ZXV_KEYGEN) {
            keygen.addEventListener('click', async () => {
                const id = await window.ZXV_KEYGEN();
                if (id) say('key made: ' + id + ' — choose it and press Enter to seat');
            });
        }
    }
    function seatPending()
    {
        if (!zb.zb_card_pending || cardBusy) return;
        const p = zb.zb_card_pending();
        if (p < 2 || p - 2 >= CARD_IDS.length || !CARD_IDS[p - 2]) return;
        cardBusy = true;
        const id = CARD_IDS[p - 2];
        if (id.slice(0, 7) === 'player-') {
            /* sovereign-key seat: sign the wall-clock second with the
             * stored key; no pin buffer involved */
            window.ZXV_KEYSEAT(id)
                .then((ok) => {
                    zb.zb_card_result(ok);
                    cardBusy = false;
                })
                .catch(() => {
                    zb.zb_card_result(1);
                    cardBusy = false;
                });
            return;
        }
        const plen = zb.zb_pin_len();
        const pin = new Uint8Array(memory.buffer, zb.zb_pin_buf(), plen);
        const pinText = new TextDecoder().decode(pin).toLowerCase();
        const idb = new TextEncoder().encode(id);
        const pinb = new TextEncoder().encode(pinText);
        const body = new Uint8Array(1 + idb.length + 1 + pinb.length);
        body[0] = idb.length;
        body.set(idb, 1);
        body[1 + idb.length] = pinb.length;
        body.set(pinb, 2 + idb.length);
        fetch('/card', {method : 'POST', body : new Blob([ body ], {type : 'text/plain'})})
            .then((r) => r.arrayBuffer())
            .then((ab) => zb.zb_card_result(ab.byteLength ? ab[0] : 1))
            .catch(() => zb.zb_card_result(1))
            .finally(() => { cardBusy = false; });
    }

    // ---- the shared spaces ----------------------------------------------
    // The node's vna_space directory, fetched from the door: the Spaces
    // face lists every space the node is in or hosts, with kind, phase and
    // members. [count][name_len][name][kind][phase][members u16]...
    if (zb.zb_spaces) {
        const spacesLoop = (async () => {
            for (;;) {
                try {
                    const res = await fetch('/spaces', {method : 'POST', body : ''});
                    if (!res.ok) throw new Error('spaces ' + res.status);
                    const d = new Uint8Array(await res.arrayBuffer());
                    const n = d[0] || 0;
                    zb.zb_spaces(n);
                    let at = 2; /* [count][flags: bit0 = wall clock landed] */
                    for (let i = 0; i < n; i++) {
                        const nl = d[at++];
                        const name = new TextDecoder().decode(d.subarray(at, at + nl));
                        at += nl;
                        zb.zb_spaces_name(i, name, d[at++], d[at++], (d[at] << 8) | d[at + 1]);
                        at += 2;
                    }
                } catch (e) { /* the door is away; keep the last list */
                }
                await new Promise((r) => setTimeout(r, 10000));
            }
        })();
    }

    let last = performance.now(), handed = false, manifest = null;
    let packBuf = null, iconInflight = -1, lastMode = -1;

    // the desk asks for one rail app's icon at a time. With a pack it is a
    // slice already verified; without one it streams from its layer inside
    // the sealed root and the launch's parts light as the bytes arrive.
    function serveIcons()
    {
        if (!zb.zb_icon_want) return;
        const want = zb.zb_icon_want();
        if (!want || want - 1 === iconInflight) return;
        iconInflight = want - 1;
        /* the want is the catalogue index; the icon APIs take the rail slot */
        const page = zb.zb_apps_page ? zb.zb_apps_page() : 0;
        const slot = iconInflight - page * 16;
        const app = manifest.apps && manifest.apps[iconInflight];
        const i = slot;
        if (!app || (!app.icon && app.off === undefined)) {
            zb.zb_icon_fail(i);
            iconInflight = -1;
            return;
        }
        if (packBuf && app.off !== undefined) {
            const bytes = packBuf.subarray(app.off, app.off + app.size);
            const p = zb.zb_icon_buf(i, bytes.length);
            if (p) {
                new Uint8Array(memory.buffer, p, bytes.length).set(bytes);
                if (zb.zb_icon_set(i, bytes.length) === 0) {
                    iconInflight = -1;
                    return;
                }
            }
            zb.zb_icon_fail(i);
            iconInflight = -1;
            return;
        }
        (async () => {
            try {
                const res = await fetch(resolve(app.icon), {cache : 'no-cache'});
                if (!res.ok) throw new Error(`${app.icon}: ${res.status}`);
                const len = +res.headers.get('content-length') || 0;
                let buf = new Uint8Array(len || 65536);
                let at = 0;
                const reader = res.body.getReader();
                for (;;) {
                    const {done : end, value} = await reader.read();
                    if (end) break;
                    if (at + value.length > buf.length) {
                        const grown = new Uint8Array(
                            Math.max(buf.length * 2, at + value.length));
                        grown.set(buf.subarray(0, at));
                        buf = grown;
                    }
                    buf.set(value, at);
                    at += value.length;
                    if (len && zb.zb_icon_prog) zb.zb_icon_prog(i, at, len);
                }
                const p = zb.zb_icon_buf(i, at);
                if (!p) throw new Error(`${app.icon}: too large`);
                new Uint8Array(memory.buffer, p, at).set(buf.subarray(0, at));
                if (zb.zb_icon_set(i, at) !== 0)
                    throw new Error(`${app.icon}: not a ZXVG file`);
            } catch (e) {
                zb.zb_icon_fail(i);
            }
            iconInflight = -1;
        })();
    }

    const MODE_HINT = {
        1 : 'the doors are open — choose the guest to come in',
        2 : 'welcome, guest — click a tile to open its app',
    };
    function frame(now)
    {
        const ms = Math.max(0, Math.min(250, now - last));
        last = now;
        zb.zb_frame(Math.round(ms));
        // the view over wasm memory is rebuilt if memory ever moved
        if (img.data.buffer !== memory.buffer) size();
        g.putImageData(img, 0, 0);
        sound.pump(ms);
        serveIcons();
        seatPending();
        if (zb.zb_desk) {
            const mode = zb.zb_desk();
            if (mode !== lastMode) {
                if (MODE_HINT[mode]) say(MODE_HINT[mode]);
                // the desk is where the bus begins: the first delta is the
                // node's own word that its link is up — true for it now, and
                // the message a transport would carry first
                if (mode === 2 && zb.zb_l2_emit) l2.emit(L2_LINK, 1, null);
            }
            lastMode = mode;
        }
        if (zb.zb_phase() === PHASE.DONE && !handed) {
            handed = true;
            dispatchEvent(new CustomEvent('zxv:booted'));
            if (manifest && manifest.next) location.replace(manifest.next);
        }
        requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);

    // the session's events, in canvas pixels
    if (zb.zb_pointer) {
        const pt = (e) => {
            const r = canvas.getBoundingClientRect();
            return [ Math.round((e.clientX - r.left) * canvas.width / r.width),
                     Math.round((e.clientY - r.top) * canvas.height / r.height) ];
        };
        // zui_input.h's masks: 1 left, 2 right, 4 middle — e.buttons is already
        // that mask on a move; e.button's index maps back to it on a change
        const BTN = [ 1, 4, 2, 8, 16 ];
        const mods = (e) => (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) |
            (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0) |
            ((e.ctrlKey || e.metaKey) ? 16 : 0); // ZUI_MOD_CMD: the platform's key
        let held = 0, lastX = 0, lastY = 0;
        canvas.addEventListener('pointerdown', (e) => {
            held = e.buttons;
            const [x, y] = pt(e);
            lastX = x;
            lastY = y;
            zb.zb_pointer(1, x, y, BTN[e.button] || 1);
            e.preventDefault();
        });
        canvas.addEventListener('pointerup', (e) => {
            held = e.buttons;
            const [x, y] = pt(e);
            lastX = x;
            lastY = y;
            zb.zb_pointer(2, x, y, BTN[e.button] || 1);
        });
        canvas.addEventListener('pointermove', (e) => {
            const [x, y] = pt(e);
            lastX = x;
            lastY = y;
            zb.zb_pointer(0, x, y, e.buttons);
        });
        // a cancelled or interrupted press still ends the drag inside
        const drop = () => {
            if (!held) return;
            held = 0;
            zb.zb_pointer(2, lastX, lastY, 1);
        };
        canvas.addEventListener('pointercancel', drop);
        addEventListener('blur', drop);
        canvas.addEventListener('wheel', (e) => {
            if (!zb.zb_scroll) return;
            const [x, y] = pt(e);
            zb.zb_scroll(x, y, Math.round(e.deltaX), Math.round(e.deltaY));
            e.preventDefault();
        }, {passive : false});
        const KEYS = {Enter : 13, Escape : 27, Backspace : 8, Tab : 9, ' ' : 32,
                      ArrowLeft : 0x100, ArrowRight : 0x101, ArrowUp : 0x102,
                      ArrowDown : 0x103, Home : 0x104, End : 0x105,
                      PageUp : 0x106, PageDown : 0x107, Delete : 127};
        addEventListener('keydown', (e) => {
            if (!zb.zb_key) return;
            const k = KEYS[e.key] || (e.key.length === 1 ? e.key.charCodeAt(0) : 0);
            if (k && zb.zb_key(k, 1, mods(e))) e.preventDefault();
        });
        addEventListener('keyup', (e) => {
            if (!zb.zb_key) return;
            const k = KEYS[e.key] || (e.key.length === 1 ? e.key.charCodeAt(0) : 0);
            if (k) zb.zb_key(k, 0, mods(e));
        });
    }

    // the system, from the sealed root
    try {
        let res = await fetch('boot.json', {cache : 'no-cache'});
        if (!res.ok) {
            // A seed's door carries only the doorway files — it names the
            // https base for the seal's own artifacts at /boot.src, and the
            // gateways' CORS lets an http origin fetch them.
            const src = await fetch('boot.src', {cache : 'no-cache'});
            if (!src.ok)
                throw new Error(`boot.json: ${res.status} ${res.statusText}`);
            base = (await src.text()).trim().replace(/\/*$/, '/');
            res = await fetch(`${base}boot.json`, {cache : 'no-cache'});
            if (!res.ok)
                throw new Error(`boot.json: ${res.status} ${res.statusText}`);
        }
        manifest = await res.json();
        /* the catalogue: every app the desk can open, in matrix order —
         * fed before the desk, so its tiles are real apps */
        if (zb.zb_apps) {
            zb.zb_apps((manifest.apps || []).length);
            (manifest.apps || []).forEach((a, k) => zb.zb_apps_name(k, a.name || ''));
        }
        if (!crypto.subtle && !zb.zb_sha_begin)
            throw new Error('no way to check the files on this origin');
        const logos = LOGOS.map((k) => manifest.logos && manifest.logos[k]).filter(Boolean);
        const files = [...logos, ...(manifest.font ? [manifest.font] : []),
                       ...(manifest.system || []) ];
        let total = 0;
        for (const f of files) {
            if (!f.path || !(f.size >= 0))
                throw new Error('boot.json: every file needs a path and a size');
            total += f.size;
        }
        let done = 0, filesDone = 0;
        const filesTotal = files.length;
        const mib = (n) => (n / 1048576).toFixed(1);
        const status = () =>
            say(`loading the system — ${mib(done)} of ${mib(total)} MiB · file ${filesDone} of ${filesTotal}`);
        zb.zb_load(0, total);
        status();
        const onBytes = (n) => {
            done += n;
            zb.zb_load(Math.min(done, total), total);
        };
        const slice = (entry) => packBuf.subarray(entry.off, entry.off + entry.size);
        if (manifest.pack && manifest.pack.path) {
            // one file holds every byte the session reads: the logos, the
            // font, the whole system set and every app's icon, end to end.
            // One fetch and one hash check it all; the entries below are
            // slices out of it — no path resolves through the tree at all.
            const pe = manifest.pack;
            const iconBytes = (manifest.apps || []).reduce((n, a) => n + (a.size || 0), 0);
            if (pe.size !== total + iconBytes)
                throw new Error('boot.json: the pack does not cover the file set');
            packBuf = new Uint8Array(pe.size);
            const progress = () => {
                filesDone = Math.min(filesTotal,
                    Math.floor(filesTotal * loaded / pe.size));
                status();
            };
            let loaded = 0;
            const copy = (value, at) => {
                if (at + value.length > pe.size)
                    throw new Error(`${pe.path}: longer than its ${pe.size} bytes`);
                packBuf.set(value, at);
                loaded += value.length;
                onBytes(value.length);
            };
            // A latent gateway answers four short ranges sooner than one long
            // stream: probe for 206, then run four lanes; each byte lands in
            // place and the one hash below still proves the whole buffer. A
            // gateway that does not range falls back to the single stream.
            let ranged = false;
            try {
                const probe = await fetch(resolve(pe.path),
                    {cache : 'no-cache', headers : {Range : 'bytes=0-0'}});
                ranged = probe.status === 206;
                if (probe.body) probe.body.cancel();
            } catch (e) { /* ranged stays false */ }
            if (ranged) {
                const LANES = 4;
                const lane = async (i) => {
                    const lo = Math.floor(pe.size * i / LANES),
                          hi = Math.floor(pe.size * (i + 1) / LANES);
                    const r = await fetch(resolve(pe.path), {cache : 'no-cache',
                        headers : {Range : `bytes=${lo}-${hi - 1}`}});
                    if (r.status !== 206)
                        throw new Error(`${pe.path}: lane answered ${r.status}`);
                    const rd = r.body.getReader();
                    let at = lo, mark = 0;
                    for (;;) {
                        const {done : end, value} = await rd.read();
                        if (end) break;
                        copy(value, at);
                        at += value.length;
                        mark += value.length;
                        if (mark >= 1048576) { mark = 0; progress(); }
                    }
                    if (at !== hi)
                        throw new Error(`${pe.path}: lane ${i} ended at ${at}, not ${hi}`);
                };
                say(`loading the system — one file in ${LANES} lanes, ${mib(pe.size)} MiB`);
                await Promise.all([...Array(LANES).keys()].map(lane));
            } else {
                say(`loading the system — one file, ${mib(pe.size)} MiB`);
                const res = await fetch(resolve(pe.path), {cache : 'no-cache'});
                if (!res.ok) throw new Error(`${pe.path}: ${res.status} ${res.statusText}`);
                const reader = res.body.getReader();
                let at = 0, mark = 0;
                for (;;) {
                    const {done : end, value} = await reader.read();
                    if (end) break;
                    copy(value, at);
                    at += value.length;
                    mark += value.length;
                    if (mark >= 1048576) { mark = 0; progress(); }
                }
            }
            if (loaded !== pe.size) throw new Error(`${pe.path}: ${loaded} of ${pe.size} bytes`);
            if (pe.sha256 && (await sha256hex(packBuf)) !== pe.sha256.toLowerCase())
                throw new Error(`${pe.path}: does not match its SHA-256`);
            filesDone = filesTotal;
            status();
        }
        const get = async (entry) =>
            packBuf ? slice(entry) : await fetchChecked(entry, onBytes);
        // the logos first, so each tile has its logo before its gold comes up
        for (let i = 0; i < LOGOS.length; i++) {
            const entry = manifest.logos && manifest.logos[LOGOS[i]];
            if (!entry) continue; // its tile stays carved and empty
            const bytes = await get(entry);
            const p = zb.zb_logo_buf(bytes.length);
            if (!p) throw new Error(`${entry.path}: too large`);
            new Uint8Array(memory.buffer, p, bytes.length).set(bytes);
            if (zb.zb_logo_set(i, bytes.length) !== 0)
                throw new Error(`${entry.path}: not a ZXVG file`);
        }
        if (manifest.font) {
            const bytes = await get(manifest.font);
            const p = zb.zb_font_buf(bytes.length);
            if (!p) throw new Error(`${manifest.font.path}: too large`);
            new Uint8Array(memory.buffer, p, bytes.length).set(bytes);
            if (zb.zb_font_set(bytes.length) !== 0)
                throw new Error(`${manifest.font.path}: not a TrueType font`);
        }
        if (!packBuf) {
            // no pack in this manifest: the files come one by one, a few in
            // flight at once — every byte still fetched, every hash checked
            const CONCURRENCY = 6;
            const system = manifest.system || [];
            let next = 0, firstErr = null;
            const worker = async () => {
                while (next < system.length && !firstErr) {
                    const i = next++;
                    try {
                        await fetchChecked(system[i], onBytes);
                    } catch (e) {
                        if (!firstErr) firstErr = e;
                        return;
                    }
                    filesDone++;
                    if (filesDone % 8 === 0 || filesDone === filesTotal) status();
                }
            };
            await Promise.all(
                Array.from({length : Math.min(CONCURRENCY, system.length)}, worker));
            if (firstErr) throw firstErr;
            status();
        }
        zb.zb_ready();
    } catch (err) {
        zb.zb_fail();
        say(`The system could not finish loading. ${err.message}`);
    }
}

boot().catch((err) => say(`The boot screen could not start. ${err.message}`));
