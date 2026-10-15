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

Items 1 to 3 are done for all 104 languages. The other three are shown per
language below.

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
| en | English | Latn | LTR | Golden Latin II | us, uk, dvorak, colemak | none needed |
| es | Spanish | Latn | LTR | Golden Latin II | es | none needed |
| fr | French | Latn | LTR | Golden Latin II | fr | none needed |
| de | German | Latn | LTR | Golden Latin II | de | none needed |
| pt | Portuguese | Latn | LTR | Golden Latin II | pt-br | none needed |
| it | Italian | Latn | LTR | Golden Latin II | it | none needed |
| ru | Russian | Cyrl | LTR | Golden Cyrillic | ru | none needed |
| uk | Ukrainian | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| pl | Polish | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ar | Arabic | Arab | RTL | Golden Arabic | ar | done |
| he | Hebrew | Hebr | RTL | Golden Hebrew | he | none needed |
| fa | Persian | Arab | RTL | Golden Arabic | needed | done |
| ur | Urdu | Arab | RTL | Golden Arabic | needed | done |
| hi | Hindi | Deva | LTR | Golden Devanagari | needed | done |
| bn | Bangla | Beng | LTR | Golden Bengali | needed | needs reordering |
| ta | Tamil | Taml | LTR | Golden Tamil | needed | needs reordering |
| zh-Hans | Chinese (Simplified) | Hans | LTR | Golden ChineseSimplified | input method needed | none needed |
| zh-Hant | Chinese (Traditional) | Hant | LTR | Golden ChineseTraditional | input method needed | none needed |
| ja | Japanese | Jpan | LTR | Golden Japanese | input method needed | none needed |
| ko | Korean | Kore | LTR | Golden Korean | input method needed | none needed |
| th | Thai | Thai | LTR | Golden Thai | needed | needs word breaking |
| vi | Vietnamese | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| tr | Turkish | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| id | Indonesian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| sw | Swahili | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| am | Amharic | Ethi | LTR | Golden Ethiopic | needed | none needed |
| yo | Yoruba | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ha | Hausa | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| el | Greek | Grek | LTR | Golden Greek | gr | none needed |
| nl | Dutch | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| mr | Marathi | Deva | LTR | Golden Devanagari | needed | done |
| te | Telugu | Telu | LTR | Golden Telugu | needed | needs reordering |
| gu | Gujarati | Gujr | LTR | Golden Gujarati | needed | needs reordering |
| kn | Kannada | Knda | LTR | Golden Kannada | needed | needs reordering |
| ml | Malayalam | Mlym | LTR | Golden Malayalam | needed | needs reordering |
| pa | Punjabi | Guru | LTR | Golden Gurmukhi | needed | needs reordering |
| or | Odia | Orya | LTR | Golden Oriya | needed | needs reordering |
| as | Assamese | Beng | LTR | Golden Bengali | needed | needs reordering |
| ne | Nepali | Deva | LTR | Golden Devanagari | needed | done |
| si | Sinhala | Sinh | LTR | Golden Sinhala | needed | needs reordering |
| sd | Sindhi | Arab | RTL | Golden Arabic | needed | done |
| ps | Pashto | Arab | RTL | Golden Arabic | needed | done |
| sat | Santali | Olck | LTR | Golden OlChiki | needed | none needed |
| dv | Dhivehi | Thaa | RTL | Golden Thaana | needed | none needed |
| ms | Malay | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| fil | Filipino | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| jv | Javanese | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| su | Sundanese | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| my | Burmese | Mymr | LTR | Golden Myanmar | needed | needs reordering |
| km | Khmer | Khmr | LTR | Golden Khmer | needed | needs reordering |
| lo | Lao | Laoo | LTR | Golden Lao | needed | needs word breaking |
| mn | Mongolian | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| bo | Tibetan | Tibt | LTR | Golden Tibetan | needed | needs reordering |
| yue | Cantonese | Hant | LTR | Golden ChineseTraditional (4 chars from other Golden faces) | input method needed | none needed |
| kk | Kazakh | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| uz | Uzbek | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ky | Kyrgyz | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| tg | Tajik | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| tk | Turkmen | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| az | Azerbaijani | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ka | Georgian | Geor | LTR | Golden Georgian | needed | none needed |
| hy | Armenian | Armn | LTR | Golden Armenian | needed | none needed |
| ckb | Central Kurdish | Arab | RTL | Golden Arabic | needed | done |
| ug | Uyghur | Arab | RTL | Golden Arabic | needed | done |
| ro | Romanian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| cs | Czech | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| hu | Hungarian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| sv | Swedish | Latn | LTR | Golden Latin II | se | none needed |
| bg | Bulgarian | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| sr | Serbian | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| hr | Croatian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| sk | Slovak | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| da | Danish | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| fi | Finnish | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| nb | Norwegian Bokmål | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| lt | Lithuanian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| be | Belarusian | Cyrl | LTR | Golden Cyrillic | needed | none needed |
| ca | Catalan | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| cy | Welsh | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ga | Irish | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| zu | Zulu | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| xh | Xhosa | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ig | Igbo | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| so | Somali | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| om | Oromo | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ti | Tigrinya | Ethi | LTR | Golden Ethiopic | needed | none needed |
| rw | Kinyarwanda | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| mg | Malagasy | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| sn | Shona | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| wo | Wolof | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ln | Lingala | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| af | Afrikaans | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ff | Fula | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| nqo | N'Ko | Nkoo | RTL | ASCW NKo | needed | needs joining from the font |
| zgh | Standard Moroccan Tamazight | Tfng | LTR | ASCW Tifinagh | needed | none needed |
| qu | Quechua | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| gn | Guarani | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| chr | Cherokee | Cher | LTR | Golden Cherokee | needed | none needed |
| nv | Navajo | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| mi | Māori | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| haw | Hawaiian | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| iu | Inuktitut | Cans | LTR | Golden CanadianAboriginal | needed | none needed |
| se | Northern Sami | Latn | LTR | Golden Latin II | us-intl, partly | none needed |
| ht | Haitian Creole | Latn | LTR | Golden Latin II | us-intl, partly | none needed |

"us-intl, partly" means the US international layout types most of the
language's letters through dead keys, but not all of them (for example
Hausa ɓ ɗ ƙ, Yoruba ẹ ọ ṣ with tones, or Vietnamese stacked tones).

## Gaps, in the order they will be closed

1. **Fonts.** Done: every script has an ASCW Golden face. Tibetan and Thaana
   were the last two; their faces were built from the OFL Noto fonts the
   same way Golden Cherokee was, renamed into the family with its metrics.
   Newly drawn glyphs in the Golden style can replace them later.
2. **Keyboards and input methods.** Layouts for every script in the table and
   for the Latin-script languages with letters us-intl lacks. Real input
   methods for Chinese, Cantonese, Japanese and Korean (today there is only
   a sample Pinyin table).
3. **Shaping.** Indic reordering beyond Devanagari (Bengali, Tamil, Telugu,
   Kannada, Malayalam, Gujarati, Gurmukhi, Odia, Sinhala), Myanmar, Khmer and
   Tibetan reordering, Thai and Lao word breaking. Text drawing also has to
   use bidi and shaping; today it does not.
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
