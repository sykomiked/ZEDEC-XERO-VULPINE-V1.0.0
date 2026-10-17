/* zxv_bank.js — the browser seed-bank client: fetch a bank once, slice
 * seeds locally, resolve 36n9://bank URIs with no round trip. The same
 * reader the pack uses, over the bank's jump table.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
window.ZXVBank = (function () {
    'use strict';

    const SB_HEADER = 32, SB_ENTRY = 40;
    let bank = null, bankDigests = null;

    function rd32(b, o) { return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; }

    function open(bytes) {
        const u = new Uint8Array(bytes);
        if (u.length < SB_HEADER || String.fromCharCode(u[0], u[1], u[2], u[3]) !== 'ZXSB')
            return null;
        const count = rd32(u, 5), tableOff = rd32(u, 9), payloadOff = rd32(u, 13);
        if (tableOff < SB_HEADER || payloadOff < tableOff ||
            tableOff + count * SB_ENTRY > payloadOff || payloadOff > u.length)
            return null;
        bank = {bytes : u, count, tableOff, payloadOff};
        bankDigests = new Map();
        for (let i = 0; i < count; i++) {
            const e = tableOff + i * SB_ENTRY;
            const digest = Array.from(u.subarray(e, e + 32)).map(
                (x) => x.toString(16).padStart(2, '0')).join('');
            bankDigests.set(digest, [rd32(u, e + 32), rd32(u, e + 36)]);
        }
        return bank;
    }

    function slice(digestHex) {
        if (!bank || !bankDigests.has(digestHex)) return null;
        const [off, len] = bankDigests.get(digestHex);
        const start = bank.payloadOff + off;
        return bank.bytes.subarray(start, start + len);
    }

    /* 36n9://bank.<id>/<offset>:<frames> — the offset is into the bank's
     * payload arena; without the bank loaded, the resolver says so. */
    function resolve36n9(uri) {
        const m = uri.match(/^36n9:\/\/bank\.([0-9a-f]+)\/(0x[0-9a-f]+|[0-9a-f]+)(?::([0-9]+))?$/i);
        if (!m) return null;
        const off = parseInt(m[2], 16);
        const frames = m[3] ? parseInt(m[3], 10) : 1;
        if (!bank) return {error : 'bank not loaded — fetch it first'};
        const start = bank.payloadOff + off;
        return bank.bytes.subarray(start, start + Math.min(frames * 168, bank.bytes.length - start));
    }

    return {
        open,
        slice,
        resolve36n9,
        loaded : () => !!bank,
        count : () => (bank ? bank.count : 0)
    };
})();