# terms: the first-boot end-user agreement gate

DISCLAIMER: this module shows an agreement and records that a person accepted
it. It does not provide legal advice or decide whether an agreement is
enforceable anywhere.

Each person on a device sees the ZXV End-User Agreement at their first
sign-in, after the boot vault opens and before their desktop. The native
screen is `src/zui/zui_terms.c` (the shell opens it after the seat unlocks;
`zui_login` knows nothing about it); this module holds the logic every screen
and port must share.

## Flow at sign-in

1. Read the device's acceptance log (one line per acceptance) and call
   `terms_log_status(log, len, seat, terms_builtin_lang(locale))`.
   `TERMS_STATUS_CURRENT` goes straight to the desktop. `NEEDED` or
   `OUTDATED` shows the agreement. A negative result means the log failed
   its hash chain: show the agreement again rather than trust it.
2. `terms_reader_init` (character cells) or `terms_reader_init_ex` (pixels,
   with a measure callback from the real font). On a text-size change or
   window resize call `terms_reader_resize`; the place and the "read" state
   are kept.
3. Feed input: `terms_scroll` (wheel, touch, scroll bar), `terms_page`,
   `terms_home`, `terms_end` (keyboard), `terms_goto_section` (section list),
   `terms_reader_spoke` (screen reader progress). Draw `terms_view`: the
   visible lines, a scroll bar, the hint line and two buttons. Lines with
   `key = 1` are drawn highlighted; they are the clauses that limit liability
   or decide disputes, marked `[KEY]` in the text.
4. "I agree" stays disabled until `view.can_accept`. On press, call
   `terms_accept` with the seat id, the clock and the log tip from
   `terms_log_verify`, then append `terms_record_format` to the log.
   "Decline" calls `terms_decline` and writes nothing.

## Accessibility

- No timer, no mouse-only path. Keyboard, section list and screen reader each
  reach the end the same way scrolling does, and the record notes which one.
- Text reflows at any size; wide (CJK) characters and combining marks are
  measured correctly; a measure callback gives exact pixel widths.
- The agreement can be read again later under Settings, About, Agreement.

## The screen

`src/zui/zui_terms.c` draws the card. A contents list sits beside the text
on a wide screen, and behind a "Contents" button on a phone. Its first entry,
the short version, is pinned, and the `[KEY]` sections carry a gold bar. A
person can jump straight to the clauses that limit liability or decide
disputes, and the end still has to be shown before "I agree" works. Every
text colour on the card is at least 4.5:1 against its background (WCAG 2.1
AA), and the control outlines are at least 3:1. `test_zui_terms` checks each
pair.

## The text

`agreement/en.txt` is embedded into `terms_text.c` by
`tools/terms/embed_terms.py`. The fingerprint recorded is plain SHA-256 of
the file, so `sha256sum agreement/en.txt` gives the same value. The generator
also writes that SHA-256 into the image beside the text (`terms_doc_t.sha256`).
`terms_reader_init*` and `terms_accept` recompute it and refuse with
`TERMS_ERR_TEXT` if they differ: a damaged image never shows or records a text
other than the one that was built. `make test-terms` fails if `terms_text.c`
no longer matches the file, or its embedded fingerprint no longer matches the
file's SHA-256.

`agreement/en.txt` is the ZXV End-User Agreement 1.0, approved by the owner
on 2026-10-14. Counsel review in the release markets is still recommended
before the first public release; a changed text gets a new version number and
fingerprint, and everyone sees it again at their next sign-in.

## Clauses the product backs in code

- §2.2, the acceptance record: the per-seat, hash-chained log above, with the
  build-time fingerprint check.
- §4.1(e), no commercial, encrypted or settlement traffic over amateur radio
  bands: the network rule is that an amateur-radio link carries only
  cleartext, signed messages (docs/ARCHITECTURE.md, radio row; US 47 CFR
  97.113 and the per-jurisdiction radio table). Choosing an amateur-radio
  transport (AX.25, APRS, JS8, VARA on ham frequencies) switches the link to
  cleartext signed-only mode. Encryption and settlement traffic are refused
  there, not silently sent. The protocol-twins router (PR #137,
  `docs/P2P_PROTOCOL_TWINS.md` section 4.3) enforces it: on AX.25/APRS and HF
  it sends only plain-text signed emergency, priority, welfare or drill
  messages, and the person cannot change that. Counsel should confirm that
  an open signature is not "obscuring meaning" under Part 97. The AX.25/APRS
  encoders on main (`src/net/dtmf.c`) only build frames and are not connected
  to the encrypted mesh or the settlement paths.
- §20.5, support: support tickets go to SCENIC BYPASS CONSULTING, LLC, at
  the contact shown under Settings, About, Support (see `SUPPORT.md`). The
  address lives on that screen, not in the agreement, so it can change
  without a new agreement version.
- §1.7 and §20.6, the root organisation and partner routing: 36N9 GENETICS
  LLC originated the concept and designs; manufacturers partner with it under
  its master partnership licence, approaching only through ILLUMAHEART
  FOUNDATION or SCENIC BYPASS CONSULTING, LLC (README, SUPPORT.md,
  hardware/README.md say the same).

Translations are added as `agreement/<lang>.txt`, each pointing at the English
reference; a language whose local law makes it govern is marked controlling
(`id!=agreement/id.txt`).
