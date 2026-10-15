# Spoken languages in the user space

ZXV's user space aims to speak as many human languages as it has programming
languages. The first step is 104 languages in 37 scripts. They were picked
by number of speakers and by script coverage, including languages of the
Global South and Indigenous languages. This page lists each one and what
works in it today.

## What a language needs

For a person to use the system fully in their language, six things have to
be in place:

1. **Locale data.** How numbers, dates, times, plurals and the week are
   written. This comes from Unicode CLDR 48 (`kernel/src/zui/zui_locale.c`).
2. **Screen reader words.** Everything the screen reader says ("button",
   "3 of 12", "heading level 2"), in the language
   (`tools/zui/locales/words/<tag>.json`).
3. **A font face.** An ASCW Golden face for the script (`assets/fonts/ascw-golden`).
4. **A keyboard or input method.** A way to type it (`zui_keymap`, `zui_ime`).
5. **Shaping.** Joining, reordering or word breaking where the script needs it
   (`zui_bidi`, `zui_shape`).
6. **Voice.** Speech commands and speech output in the language (`zui_voicectl`
   and the reader's voice).

## Status

Items 1 to 4 are done for all 104 languages, except input methods for
Chinese, Cantonese, Japanese, Korean and the Ge'ez syllables. Shaping is shown
per language below.

**Translations are machine-made.** The screen reader words of every
language except English were translated by Claude and have not yet been
checked by a native speaker. `zui_locale_review()` reports this
(`ZUI_REVIEW_MACHINE`), and settings should show it next to the language.
A file is marked `"review": "reviewed"` only after a native speaker has
checked it. [tools/zui/locales/REVIEW.md](../tools/zui/locales/REVIEW.md)
lists the words each translation is least sure of, which is where a reviewer
should start.

**Voice** is English only for now (`zui_voicectl`), so it is not repeated in
the table.

| Tag | Language | Script | Direction | ASCW Golden face | Keyboard | Shaping |
|---|---|---|---|---|---|---|
| en | English | Latn | LTR | Golden Latin II | `us` | none needed |
| es | Spanish | Latn | LTR | Golden Latin II | `es` | none needed |
| fr | French | Latn | LTR | Golden Latin II | `fr` | none needed |
| de | German | Latn | LTR | Golden Latin II | `de` | none needed |
| pt | Portuguese | Latn | LTR | Golden Latin II | `pt-br` | none needed |
| it | Italian | Latn | LTR | Golden Latin II | `it` | none needed |
| ru | Russian | Cyrl | LTR | Golden Cyrillic | `ru` | none needed |
| uk | Ukrainian | Cyrl | LTR | Golden Cyrillic | `ua` | none needed |
| pl | Polish | Latn | LTR | Golden Latin II | `pl` | none needed |
| ar | Arabic | Arab | RTL | Golden Arabic | `ar` | done |
| he | Hebrew | Hebr | RTL | Golden Hebrew | `he` | none needed |
| fa | Persian | Arab | RTL | Golden Arabic | `ir` | done |
| ur | Urdu | Arab | RTL | Golden Arabic | `pk` | done |
| hi | Hindi | Deva | LTR | Golden Devanagari | `in-hi` | done |
| bn | Bangla | Beng | LTR | Golden Bengali | `bd` | done |
| ta | Tamil | Taml | LTR | Golden Tamil | `in-tamilnet` | done |
| zh-Hans | Chinese (Simplified) | Hans | LTR | Golden ChineseSimplified | `us` + input method needed | none needed |
| zh-Hant | Chinese (Traditional) | Hant | LTR | Golden ChineseTraditional | `us` + input method needed | none needed |
| ja | Japanese | Jpan | LTR | Golden Japanese | `us` + input method needed | none needed |
| ko | Korean | Kore | LTR | Golden Korean | `us` + input method needed | none needed |
| th | Thai | Thai | LTR | Golden Thai | `th` | needs word breaking |
| vi | Vietnamese | Latn | LTR | Golden Latin II | `vn` | none needed |
| tr | Turkish | Latn | LTR | Golden Latin II | `tr` | none needed |
| id | Indonesian | Latn | LTR | Golden Latin II | `us` | none needed |
| sw | Swahili | Latn | LTR | Golden Latin II | `us` | none needed |
| am | Amharic | Ethi | LTR | Golden Ethiopic | `et`; syllables need an input method | none needed |
| yo | Yoruba | Latn | LTR | Golden Latin II | `ng-yoruba` | none needed |
| ha | Hausa | Latn | LTR | Golden Latin II | `ng-hausa` | none needed |
| el | Greek | Grek | LTR | Golden Greek | `gr` | none needed |
| nl | Dutch | Latn | LTR | Golden Latin II | `us-intl` | none needed |
| mr | Marathi | Deva | LTR | Golden Devanagari | `in-marathi` | done |
| te | Telugu | Telu | LTR | Golden Telugu | `in-tel` | done |
| gu | Gujarati | Gujr | LTR | Golden Gujarati | `in-guj` | done |
| kn | Kannada | Knda | LTR | Golden Kannada | `in-kan` | done |
| ml | Malayalam | Mlym | LTR | Golden Malayalam | `in-mal` | done |
| pa | Punjabi | Guru | LTR | Golden Gurmukhi | `in-guru` | done |
| or | Odia | Orya | LTR | Golden Oriya | `in-ori` | done |
| as | Assamese | Beng | LTR | Golden Bengali | `in-asm-kagapa` | done |
| ne | Nepali | Deva | LTR | Golden Devanagari | `np` | done |
| si | Sinhala | Sinh | LTR | Golden Sinhala | `lk` | done |
| sd | Sindhi | Arab | RTL | Golden Arabic | `pk-snd` | done |
| ps | Pashto | Arab | RTL | Golden Arabic | `af-ps` | done |
| sat | Santali | Olck | LTR | Golden OlChiki | `in-sat` | none needed |
| dv | Dhivehi | Thaa | RTL | Golden Thaana | `mv` | none needed |
| ms | Malay | Latn | LTR | Golden Latin II | `us` | none needed |
| fil | Filipino | Latn | LTR | Golden Latin II | `ph` | none needed |
| jv | Javanese | Latn | LTR | Golden Latin II | `us` | none needed |
| su | Sundanese | Latn | LTR | Golden Latin II | `us` | none needed |
| my | Burmese | Mymr | LTR | Golden Myanmar | `mm` | done |
| km | Khmer | Khmr | LTR | Golden Khmer | `kh` | done |
| lo | Lao | Laoo | LTR | Golden Lao | `la` | needs word breaking |
| mn | Mongolian | Cyrl | LTR | Golden Cyrillic | `mn` | none needed |
| bo | Tibetan | Tibt | LTR | Golden Tibetan | `cn-tib` | no reordering; stacks drawn as marks |
| yue | Cantonese | Hant | LTR | Golden ChineseTraditional (4 chars from other Golden faces) | `us` + input method needed | none needed |
| kk | Kazakh | Cyrl | LTR | Golden Cyrillic | `kz` | none needed |
| uz | Uzbek | Latn | LTR | Golden Latin II | `uz` | none needed |
| ky | Kyrgyz | Cyrl | LTR | Golden Cyrillic | `kg` | none needed |
| tg | Tajik | Cyrl | LTR | Golden Cyrillic | `tj` | none needed |
| tk | Turkmen | Latn | LTR | Golden Latin II | `tm` | none needed |
| az | Azerbaijani | Latn | LTR | Golden Latin II | `az` | none needed |
| ka | Georgian | Geor | LTR | Golden Georgian | `ge` | none needed |
| hy | Armenian | Armn | LTR | Golden Armenian | `am` | none needed |
| ckb | Central Kurdish | Arab | RTL | Golden Arabic | `iq-ku-ara` | done |
| ug | Uyghur | Arab | RTL | Golden Arabic | `cn-ug` | done |
| ro | Romanian | Latn | LTR | Golden Latin II | `ro` | none needed |
| cs | Czech | Latn | LTR | Golden Latin II | `cz` | none needed |
| hu | Hungarian | Latn | LTR | Golden Latin II | `hu` | none needed |
| sv | Swedish | Latn | LTR | Golden Latin II | `se` | none needed |
| bg | Bulgarian | Cyrl | LTR | Golden Cyrillic | `bg` | none needed |
| sr | Serbian | Cyrl | LTR | Golden Cyrillic | `rs` | none needed |
| hr | Croatian | Latn | LTR | Golden Latin II | `hr` | none needed |
| sk | Slovak | Latn | LTR | Golden Latin II | `sk` | none needed |
| da | Danish | Latn | LTR | Golden Latin II | `dk` | none needed |
| fi | Finnish | Latn | LTR | Golden Latin II | `fi` | none needed |
| nb | Norwegian Bokmål | Latn | LTR | Golden Latin II | `no` | none needed |
| lt | Lithuanian | Latn | LTR | Golden Latin II | `lt` | none needed |
| be | Belarusian | Cyrl | LTR | Golden Cyrillic | `by` | none needed |
| ca | Catalan | Latn | LTR | Golden Latin II | `es-cat` | none needed |
| cy | Welsh | Latn | LTR | Golden Latin II | `gb-extd` | none needed |
| ga | Irish | Latn | LTR | Golden Latin II | `ie` | none needed |
| zu | Zulu | Latn | LTR | Golden Latin II | `us-intl` | none needed |
| xh | Xhosa | Latn | LTR | Golden Latin II | `us-intl` | none needed |
| ig | Igbo | Latn | LTR | Golden Latin II | `ng-igbo` | none needed |
| so | Somali | Latn | LTR | Golden Latin II | `us` | none needed |
| om | Oromo | Latn | LTR | Golden Latin II | `us` | none needed |
| ti | Tigrinya | Ethi | LTR | Golden Ethiopic | `et`; syllables need an input method | none needed |
| rw | Kinyarwanda | Latn | LTR | Golden Latin II | `us` | none needed |
| mg | Malagasy | Latn | LTR | Golden Latin II | `us` | none needed |
| sn | Shona | Latn | LTR | Golden Latin II | `us` | none needed |
| wo | Wolof | Latn | LTR | Golden Latin II | `sn` | none needed |
| ln | Lingala | Latn | LTR | Golden Latin II | `cd` | none needed |
| af | Afrikaans | Latn | LTR | Golden Latin II | `us-intl` | none needed |
| ff | Fula | Latn | LTR | Golden Latin II | `gh-fula` | none needed |
| nqo | N'Ko | Nkoo | RTL | ASCW NKo | `gn` | needs joining from the font |
| zgh | Standard Moroccan Tamazight | Tfng | LTR | ASCW Tifinagh | `ma-tifinagh` | none needed |
| qu | Quechua | Latn | LTR | Golden Latin II | `latam` | none needed |
| gn | Guarani | Latn | LTR | Golden Latin II | `latam` | none needed |
| chr | Cherokee | Cher | LTR | Golden Cherokee | `us-chr` | none needed |
| nv | Navajo | Latn | LTR | Golden Latin II | `us-intl` | none needed |
| mi | Māori | Latn | LTR | Golden Latin II | `nz-mao` | none needed |
| haw | Hawaiian | Latn | LTR | Golden Latin II | `us-haw` | none needed |
| iu | Inuktitut | Cans | LTR | Golden CanadianAboriginal | `ca-ike` | none needed |
| se | Northern Sami | Latn | LTR | Golden Latin II | `no-smi` | none needed |
| ht | Haitian Creole | Latn | LTR | Golden Latin II | `us-intl` | none needed |

The keyboard column is the layout `zui_keymap_for_locale` picks
(`tools/zui/locales/KEYBOARDS.tsv`). 70 of the layouts are generated from
xkeyboard-config by `tools/zui/gen_keymaps.py`: InScript for the Indic
scripts, the national layouts, and the community layouts for Cherokee,
Inuktitut, N'Ko, Tifinagh, Māori, Hawaiian, Northern Sami, Yoruba, Hausa,
Igbo and Fula. Languages typed with `us-intl` (Dutch, Afrikaans, Zulu,
Xhosa, Navajo, Haitian Creole) or `us` have no national layout of their own
in xkeyboard-config; `us-intl` reaches most of their letters through dead
keys. A layout keeps at most eight dead keys, so a few rare ones on the
Czech, Slovak, Hungarian and similar layouts type nothing yet.

## Gaps, in the order they will be closed

1. **Fonts.** Done: every script has an ASCW Golden face. Tibetan and Thaana
   were the last two; their faces were built from the OFL Noto fonts the
   same way Golden Cherokee was, renamed into the family with its metrics.
   Newly drawn glyphs in the Golden style can replace them later.
2. **Input methods.** Every language has a keyboard layout now. Still to
   come: real input methods for Chinese, Cantonese, Japanese and Korean (today
   there is only a sample Pinyin table), a syllable input method for Amharic
   and Tigrinya, and a Navajo layout.
3. **Shaping.** Syllables and reordering now cover all the Brahmic scripts
   that need it: the nine Indic scripts of India, Sinhala, Myanmar and Khmer
   (`zui_shape.h` S7, S8, tables generated from the Unicode 15.1 character
   database by `tools/zui/gen_indic.py`). Still to come: Thai, Lao, Khmer and
   Burmese word breaking, and text drawing through bidi and shaping (today it
   does not use them).
4. **Voice.** Speech commands and voices beyond English.
5. **Native speaker review** of every translation, Cherokee, Navajo,
   Inuktitut, N'Ko, Tibetan and Santali first, because they are the least
   reliable.
6. **More languages.** Next in line: Kalaallisut, K'iche', Mapuche, Mohawk,
   Lakota, Yiddish, Albanian, Latvian, Estonian, Slovenian, Basque, Galician,
   Macedonian, Icelandic, Maithili, Konkani, Kashmiri and Dzongkha.

## Adding or fixing a language

1. Add a line to `tools/zui/locales/LANGUAGES.tsv`.
2. Copy `tools/zui/locales/words/en.json` to `<tag>.json` and translate it.
   The `counts` need the language's CLDR plural categories.
3. Run `python3 tools/zui/gen_locales.py`. It reads CLDR 48.2.0 (downloaded
   from the npm registry once) and rewrites `kernel/src/zui/zui_locale_data.inc`
   and `zui_locale_samples.inc`.
4. Run `sh kernel/src/zui/test_zui_toolkit.sh`. It checks every locale's
   words, placeholders and plural forms against CLDR's own samples.
