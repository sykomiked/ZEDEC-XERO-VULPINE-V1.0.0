/* zxv_i18n.js — native human-language support for the desk and every
 * surface. The user picks their language once; the whole UI renders in
 * it, including right-to-left scripts. The catalogue is languages.tsv
 * (221 entries: every living language with a million or more speakers,
 * plus Aramaic and Latin). No string is ever hardcoded in the UI.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
(function (global) {
    "use strict";

    var RTL = { Arab: 1, Hebr: 1, Syrc: 1, Thaa: 1, Nkoo: 1, Adlm: 1 };
    var state = { code: "en", table: {}, meta: null, dir: "ltr" };

    /* the catalogue: code, native, english, script, speakers */
    function parseCatalogue(tsv) {
        var lines = tsv.split("\n");
        var out = [];
        for (var i = 1; i < lines.length; i++) {
            var c = lines[i].split("\t");
            if (c.length >= 5) {
                out.push({ code: c[0], native: c[1], english: c[2], script: c[3],
                           speakers: parseInt(c[4], 10) || 0,
                           dir: RTL[c[3]] ? "rtl" : "ltr" });
            }
        }
        return out;
    }

    /* the string tables live beside the catalogue as <code>.json */
    function load(code, cb) {
        var req = new XMLHttpRequest();
        req.open("GET", "i18n/" + code + ".json", true);
        req.onreadystatechange = function () {
            if (req.readyState !== 4) return;
            var t = {};
            if (req.status === 200) {
                try { t = JSON.parse(req.responseText); } catch (e) { t = {}; }
            }
            /* the fallback: a missing string falls back to English, never
             * to a blank label (the UI must always say something) */
            state.code = code;
            state.table = t;
            state.dir = (RTL[(t._script) || "Latn"]) ? "rtl" : "ltr";
            document.documentElement.dir = state.dir;
            document.documentElement.lang = code;
            if (cb) cb(state);
        };
        req.send();
    }

    function t(key, fallback) {
        if (state.table && state.table[key]) return state.table[key];
        if (fallback) return fallback;
        return key;
    }

    /* the picker: every language in its OWN script and name, so a user who
     * reads no English can still find their language */
    function pickerHtml(languages, current) {
        var h = ['<div class="zxv-lang-picker" role="listbox">'];
        for (var i = 0; i < languages.length; i++) {
            var l = languages[i];
            h.push('<button role="option" data-code="' + l.code + '" dir="' + l.dir +
                   '"' + (l.code === current ? ' aria-selected="true"' : "") + '>' +
                   '<span class="native">' + l.native + '</span>' +
                   '<span class="english">' + l.english + '</span></button>');
        }
        h.push("</div>");
        return h.join("");
    }

    global.ZxvI18n = {
        parseCatalogue: parseCatalogue,
        load: load,
        t: t,
        pickerHtml: pickerHtml,
        state: state,
        isRtl: function () { return state.dir === "rtl"; }
    };
}(typeof window !== "undefined" ? window : this));
