# terms: the first-boot end-user agreement gate

DISCLAIMER: this module shows an agreement and records that a person accepted
it. It does not provide legal advice or decide whether an agreement is
enforceable anywhere.

Each person on a device sees the ZXV End-User Agreement at their first
sign-in, after the boot vault opens and before their desktop. The login
screen (`zui_login`) draws it; this module holds the logic every screen and
port must share.

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

## The text

`agreement/en.txt` is embedded into `terms_text.c` by
`tools/terms/embed_terms.py`. The fingerprint recorded is plain SHA-256 of
the file, so `sha256sum agreement/en.txt` gives the same value. `make
test-terms` fails if `terms_text.c` no longer matches the file.

The text in the repository is a placeholder. The draft agreement is under
owner and counsel review and is not published until the owner approves it.
Translations are added as `agreement/<lang>.txt`, each pointing at the English
reference; a language whose local law makes it govern is marked controlling
(`id!=agreement/id.txt`).
