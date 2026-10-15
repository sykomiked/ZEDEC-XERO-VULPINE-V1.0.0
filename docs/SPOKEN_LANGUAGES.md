# Spoken languages in the user space

ZXV's user space aims to speak as many human languages as it has programming
languages. It has 183 locales: 104 living languages in 37 scripts, picked by
number of speakers and by script coverage (including languages of the
Global South and Indigenous languages), and 79 ancient and historical
languages, from Latin, Sanskrit and Classical Chinese to Akkadian cuneiform,
Egyptian hieroglyphs and Linear B. This page lists each one and what works
in it today.

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

Items 1 to 4 are done for all 104 languages. Chinese, Cantonese and Japanese
input converts one character at a time; word conversion is still to come.
Shaping is shown per language below.

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

The living languages are in the first table; the ancient and historical
languages have [their own section](#ancient-and-historical-languages).

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
| zh-Hans | Chinese (Simplified) | Hans | LTR | Golden ChineseSimplified | `us` + Pinyin (`zui_ime_pinyin`) | none needed |
| zh-Hant | Chinese (Traditional) | Hant | LTR | Golden ChineseTraditional | `us` + Pinyin (`zui_ime_pinyin_hant`) | none needed |
| ja | Japanese | Jpan | LTR | Golden Japanese | `us` + romaji to kana and kanji (`zui_ime_kana`) | none needed |
| ko | Korean | Kore | LTR | Golden Korean | `us` + two-set Hangul (`zui_ime_hangul`) | none needed |
| th | Thai | Thai | LTR | Golden Thai | `th` | needs word breaking |
| vi | Vietnamese | Latn | LTR | Golden Latin II | `vn` | none needed |
| tr | Turkish | Latn | LTR | Golden Latin II | `tr` | none needed |
| id | Indonesian | Latn | LTR | Golden Latin II | `us` | none needed |
| sw | Swahili | Latn | LTR | Golden Latin II | `us` | none needed |
| am | Amharic | Ethi | LTR | Golden Ethiopic | `us` + Ge'ez syllables (`zui_ime_geez`), or `et` | none needed |
| yo | Yoruba | Latn | LTR | Golden Latin II | `ng-yoruba` | none needed |
| ha | Hausa | Latn | LTR | Golden Latin II | `ng-hausa` | none needed |
| el | Greek | Grek | LTR | Golden Greek | `gr` | none needed |
| nl | Dutch | Latn | LTR | Golden Latin II | `nv` | none needed |
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
| yue | Cantonese | Hant | LTR | Golden ChineseTraditional (4 chars from other Golden faces) | `us` + Jyutping (`zui_ime_jyutping`) | none needed |
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
| zu | Zulu | Latn | LTR | Golden Latin II | `nv` | none needed |
| xh | Xhosa | Latn | LTR | Golden Latin II | `nv` | none needed |
| ig | Igbo | Latn | LTR | Golden Latin II | `ng-igbo` | none needed |
| so | Somali | Latn | LTR | Golden Latin II | `us` | none needed |
| om | Oromo | Latn | LTR | Golden Latin II | `us` | none needed |
| ti | Tigrinya | Ethi | LTR | Golden Ethiopic | `us` + Ge'ez syllables (`zui_ime_geez`), or `et` | none needed |
| rw | Kinyarwanda | Latn | LTR | Golden Latin II | `us` | none needed |
| mg | Malagasy | Latn | LTR | Golden Latin II | `us` | none needed |
| sn | Shona | Latn | LTR | Golden Latin II | `us` | none needed |
| wo | Wolof | Latn | LTR | Golden Latin II | `sn` | none needed |
| ln | Lingala | Latn | LTR | Golden Latin II | `cd` | none needed |
| af | Afrikaans | Latn | LTR | Golden Latin II | `nv` | none needed |
| ff | Fula | Latn | LTR | Golden Latin II | `gh-fula` | none needed |
| nqo | N'Ko | Nkoo | RTL | ASCW NKo | `gn` | needs joining from the font |
| zgh | Standard Moroccan Tamazight | Tfng | LTR | ASCW Tifinagh | `ma-tifinagh` | none needed |
| qu | Quechua | Latn | LTR | Golden Latin II | `latam` | none needed |
| gn | Guarani | Latn | LTR | Golden Latin II | `latam` | none needed |
| chr | Cherokee | Cher | LTR | Golden Cherokee | `us-chr` | none needed |
| nv | Navajo | Latn | LTR | Golden Latin II | `nv` | none needed |
| mi | Māori | Latn | LTR | Golden Latin II | `nz-mao` | none needed |
| haw | Hawaiian | Latn | LTR | Golden Latin II | `us-haw` | none needed |
| iu | Inuktitut | Cans | LTR | Golden CanadianAboriginal | `ca-ike` | none needed |
| se | Northern Sami | Latn | LTR | Golden Latin II | `no-smi` | none needed |
| ht | Haitian Creole | Latn | LTR | Golden Latin II | `nv` | none needed |

The keyboard column is the layout `zui_keymap_for_locale` picks
(`tools/zui/locales/KEYBOARDS.tsv`). 75 of the layouts are generated from
xkeyboard-config by `tools/zui/gen_keymaps.py`: InScript for the Indic
scripts, the national layouts, and the community layouts for Cherokee,
Inuktitut, N'Ko, Tifinagh, Māori, Hawaiian, Northern Sami, Yoruba, Hausa,
Igbo and Fula. Languages typed with `us-intl` (Dutch, Afrikaans, Zulu,
Xhosa, Navajo, Haitian Creole) or `us` have no national layout of their own
in xkeyboard-config; `us-intl` reaches most of their letters through dead
keys. A layout keeps at most eight dead keys, so a few rare ones on the
Czech, Slovak, Hungarian and similar layouts type nothing yet.

## Ancient and historical languages

79 locales are for languages no one speaks natively today. They come in two
kinds:

- **Translated (9).** Latin, Ancient Greek, Sanskrit, Pali, Church Slavonic,
  Old English, Classical Chinese, Ottoman Turkish and Classical Syriac have a
  vocabulary for modern things (the Vatican's Latin lexicon, the Wikipedias
  written in these languages, liturgical use), so the screen reader's words
  are translated. Like every translation here they are machine-made and
  unreviewed (`ZUI_REVIEW_MACHINE`) until a scholar of the language checks
  them.
- **Display and input only (70).** For Akkadian, Sumerian, Egyptian,
  Mycenaean Greek and the rest there is no vocabulary for a user interface,
  so nothing was invented. These locales give the script its ASCW Golden
  face, its direction and a way to type it, and the screen reader's words
  stay English. `zui_locale_review()` reports `ZUI_REVIEW_UNTRANSLATED` and
  settings should say "script only" next to them.

Where CLDR has no data for a language (it has Latin, Sanskrit, Pali, Church
Slavonic, Ge'ez, Syriac and Coptic), its numbers and dates come from the
`data` column of `LANGUAGES.tsv` (Greek for Ancient Greek, Traditional
Chinese for Classical Chinese, CLDR's root locale for the script-only ones).
Latin and Sanskrit get plural rules CLDR does not have (Sanskrit's dual), so
counts agree with the number. A tag with a script subtag (`arc-Nbat`,
`cu-Glag`, `sa-Sidd`) is its own locale.

**Typing.** Latin, Old English, Pali, Old Norse and the medieval Latin-script
languages use `latn-hist`: AltGr gives æ þ ð ƿ ȝ ſ œ ǫ ę, and dead keys
give the macron (AltGr -), dot below (AltGr ;), dot above (AltGr =) and
acute (AltGr '). Ancient Greek, Biblical Hebrew, Syriac, Church Slavonic and
Ogham use their xkeyboard-config layouts. Fifty scripts with no layout of
their own are typed by their Unicode sign names (`zui_ime.h` M7,
`tools/zui/gen_ime_signs.py`): `ka2` gives the cuneiform sign KA2, `a001` the
hieroglyph A1 of Gardiner's list, `a` or `b008` the Linear B syllable A,
`fehu` the rune ᚠ.

**Not covered.** Old Uyghur has no ASCW face yet (no OFL font could be
fetched). Tangut and Khitan have faces but no input method: their signs
have numbers, not names, in Unicode. Maya hieroglyphs, Linear Elamite, the
Indus script and Rongorongo are not in Unicode, so nothing can show them
yet.

| Tag | Language | Script | Direction | ASCW Golden face | Keyboard | Shaping | Reader words |
|---|---|---|---|---|---|---|---|
| la | Latin | Latn | LTR | Golden Latin II | `latn-hist` | none needed | translated, unreviewed |
| grc | Ancient Greek | Grek | LTR | Golden Greek | `gr-polytonic` | none needed | translated, unreviewed |
| sa | Sanskrit | Deva | LTR | Golden Devanagari | `in-hi` | done | translated, unreviewed |
| pi | Pali | Latn | LTR | Golden Latin II | `latn-hist` | none needed | translated, unreviewed |
| cu | Church Slavonic | Cyrl | LTR | Golden Cyrillic | `ru-chu` | none needed | translated, unreviewed |
| ang | Old English | Latn | LTR | Golden Latin II | `latn-hist` | none needed | translated, unreviewed |
| lzh | Classical Chinese | Hant | LTR | Golden Chinese Traditional | `us` + Pinyin (`zui_ime_pinyin_hant`) | none needed | translated, unreviewed |
| ota | Ottoman Turkish | Arab | RTL | Golden Arabic | `ir` | done (joining) | translated, unreviewed |
| syr | Syriac | Syrc | RTL | Golden Syriac | `sy-syc` | done (joining) | translated, unreviewed |
| gez | Geez | Ethi | LTR | Golden Ethiopic | `us` + Ge'ez syllables (`zui_ime_geez`) | none needed | display and input only (words in English) |
| cop | Coptic | Copt | LTR | Golden Coptic | `us` + sign names (`signs-copt`) | none needed | display and input only (words in English) |
| hbo | Biblical Hebrew | Hebr | RTL | Golden Hebrew | `il-biblical` | none needed | display and input only (words in English) |
| arc | Imperial Aramaic | Armi | RTL | Golden Aramaic | `us` + sign names (`signs-armi`) | none needed | display and input only (words in English) |
| arc-Nbat | Nabataean Aramaic | Nbat | RTL | Golden Nabataean | `us` + sign names (`signs-nbat`) | none needed | display and input only (words in English) |
| arc-Palm | Palmyrene Aramaic | Palm | RTL | Golden Palmyrene | `us` + sign names (`signs-palm`) | none needed | display and input only (words in English) |
| arc-Hatr | Hatran Aramaic | Hatr | RTL | Golden Hatran | `us` + sign names (`signs-hatr`) | none needed | display and input only (words in English) |
| arc-Elym | Elymaic Aramaic | Elym | RTL | Golden Elymaic | `us` + sign names (`signs-elym`) | none needed | display and input only (words in English) |
| myz | Classical Mandaic | Mand | RTL | Golden Mandaic | `us` + sign names (`signs-mand`) | done (joining) | display and input only (words in English) |
| akk | Akkadian | Xsux | LTR | Golden Cuneiform | `us` + sign names (`signs-xsux`) | none needed | display and input only (words in English) |
| sux | Sumerian | Xsux | LTR | Golden Cuneiform | `us` + sign names (`signs-xsux`) | none needed | display and input only (words in English) |
| hit | Hittite | Xsux | LTR | Golden Cuneiform | `us` + sign names (`signs-xsux`) | none needed | display and input only (words in English) |
| elx | Elamite | Xsux | LTR | Golden Cuneiform | `us` + sign names (`signs-xsux`) | none needed | display and input only (words in English) |
| xur | Urartian | Xsux | LTR | Golden Cuneiform | `us` + sign names (`signs-xsux`) | none needed | display and input only (words in English) |
| egy | Ancient Egyptian | Egyp | LTR | Golden Egyptian Hieroglyphs | `us` + sign names (`signs-egyp`) | none needed | display and input only (words in English) |
| peo | Old Persian | Xpeo | LTR | Golden Old Persian | `us` + sign names (`signs-xpeo`) | none needed | display and input only (words in English) |
| uga | Ugaritic | Ugar | LTR | Golden Ugaritic | `us` + sign names (`signs-ugar`) | none needed | display and input only (words in English) |
| phn | Phoenician | Phnx | RTL | Golden Phoenician | `us` + sign names (`signs-phnx`) | none needed | display and input only (words in English) |
| gmy | Mycenaean Greek | Linb | LTR | Golden Linear B | `us` + sign names (`signs-linb`) | none needed | display and input only (words in English) |
| lab | Linear A | Lina | LTR | Golden Linear A | `us` + sign names (`signs-lina`) | none needed | display and input only (words in English) |
| grc-Cprt | Arcadocypriot Greek | Cprt | RTL | Golden Cypriot | `us` + sign names (`signs-cprt`) | none needed | display and input only (words in English) |
| und-Cpmn | Cypro-Minoan | Cpmn | LTR | Golden Cypro Minoan | `us` + sign names (`signs-cpmn`) | none needed | display and input only (words in English) |
| ett | Etruscan | Ital | LTR | Golden Old Italic | `us` + sign names (`signs-ital`) | none needed | display and input only (words in English) |
| osc | Oscan | Ital | LTR | Golden Old Italic | `us` + sign names (`signs-ital`) | none needed | display and input only (words in English) |
| xum | Umbrian | Ital | LTR | Golden Old Italic | `us` + sign names (`signs-ital`) | none needed | display and input only (words in English) |
| xlc | Lycian | Lyci | LTR | Golden Lycian | `us` + sign names (`signs-lyci`) | none needed | display and input only (words in English) |
| xld | Lydian | Lydi | RTL | Golden Lydian | `us` + sign names (`signs-lydi`) | none needed | display and input only (words in English) |
| xcr | Carian | Cari | LTR | Golden Carian | `us` + sign names (`signs-cari`) | none needed | display and input only (words in English) |
| hlu | Hieroglyphic Luwian | Hluw | LTR | Golden Anatolian Hieroglyphs | `us` + sign names (`signs-hluw`) | none needed | display and input only (words in English) |
| xmr | Meroitic | Mero | RTL | Golden Meroitic | `us` + sign names (`signs-mero`) | none needed | display and input only (words in English) |
| onw | Old Nubian | Copt | LTR | Golden Coptic | `us` + sign names (`signs-copt`) | none needed | display and input only (words in English) |
| otk | Old Turkic | Orkh | RTL | Golden Old Turkic | `us` + sign names (`signs-orkh`) | none needed | display and input only (words in English) |
| sog | Sogdian | Sogd | RTL | Golden Sogdian | `us` + sign names (`signs-sogd`) | done (joining) | display and input only (words in English) |
| sog-Sogo | Old Sogdian | Sogo | RTL | Golden Old Sogdian | `us` + sign names (`signs-sogo`) | done (joining) | display and input only (words in English) |
| xpr | Parthian | Prti | RTL | Golden Inscriptional Parthian | `us` + sign names (`signs-prti`) | none needed | display and input only (words in English) |
| pal | Middle Persian | Phli | RTL | Golden Inscriptional Pahlavi | `us` + sign names (`signs-phli`) | none needed | display and input only (words in English) |
| pal-Phlp | Psalter Pahlavi | Phlp | RTL | Golden Psalter Pahlavi | `us` + sign names (`signs-phlp`) | done (joining) | display and input only (words in English) |
| xmn | Manichaean Middle Persian | Mani | RTL | Golden Manichaean | `us` + sign names (`signs-mani`) | done (joining) | display and input only (words in English) |
| ae | Avestan | Avst | RTL | Golden Avestan | `us` + sign names (`signs-avst`) | none needed | display and input only (words in English) |
| xco | Chorasmian | Chrs | RTL | Golden Chorasmian | `us` + sign names (`signs-chrs`) | done (joining) | display and input only (words in English) |
| xsa | Sabaean | Sarb | RTL | Golden Old South Arabian | `us` + sign names (`signs-sarb`) | none needed | display and input only (words in English) |
| xna | Ancient North Arabian | Narb | RTL | Golden Old North Arabian | `us` + sign names (`signs-narb`) | none needed | display and input only (words in English) |
| smp | Samaritan Hebrew | Samr | RTL | Golden Samaritan | `us` + sign names (`signs-samr`) | none needed | display and input only (words in English) |
| pra | Prakrit | Brah | LTR | Golden Brahmi | `us` + sign names (`signs-brah`) | reordering not yet | display and input only (words in English) |
| pgd | Gandhari | Khar | RTL | Golden Kharoshthi | `us` + sign names (`signs-khar`) | reordering not yet | display and input only (words in English) |
| sa-Sidd | Sanskrit (Siddham) | Sidd | LTR | Golden Siddham | `us` + sign names (`signs-sidd`) | reordering not yet | display and input only (words in English) |
| sa-Shrd | Sanskrit (Sharada) | Shrd | LTR | Golden Sharada | `us` + sign names (`signs-shrd`) | reordering not yet | display and input only (words in English) |
| sa-Gran | Sanskrit (Grantha) | Gran | LTR | Golden Grantha | `us` + sign names (`signs-gran`) | reordering not yet | display and input only (words in English) |
| kaw | Old Javanese | Kawi | LTR | Golden Kawi | `us` + sign names (`signs-kawi`) | reordering not yet | display and input only (words in English) |
| xct | Classical Tibetan | Tibt | LTR | Golden Tibetan | `cn-tib` | none needed | display and input only (words in English) |
| xzh | Zhang-Zhung | Marc | LTR | Golden Marchen | `us` + sign names (`signs-marc`) | reordering not yet | display and input only (words in English) |
| cmg | Classical Mongolian | Mong | LTR | Golden Mongolian | `us` + sign names (`signs-mong`) | done (joining) | display and input only (words in English) |
| txg | Tangut | Tang | LTR | Golden Tangut | `us` | none needed | display and input only (words in English) |
| zkt | Khitan | Kits | LTR | Golden Khitan Small Script | `us` | none needed | display and input only (words in English) |
| ohu | Old Hungarian | Hung | RTL | Golden Old Hungarian | `us` + sign names (`signs-hung`) | none needed | display and input only (words in English) |
| oge | Old Georgian | Geor | LTR | Golden Georgian | `ge` | none needed | display and input only (words in English) |
| xcl | Classical Armenian | Armn | LTR | Golden Armenian | `am` | none needed | display and input only (words in English) |
| xag | Caucasian Albanian | Aghb | LTR | Golden Caucasian Albanian | `us` + sign names (`signs-aghb`) | none needed | display and input only (words in English) |
| kv-Perm | Old Permic | Perm | LTR | Golden Old Permic | `us` + sign names (`signs-perm`) | none needed | display and input only (words in English) |
| sq-Elba | Albanian (Elbasan) | Elba | LTR | Golden Elbasan | `us` + sign names (`signs-elba`) | none needed | display and input only (words in English) |
| sq-Vith | Albanian (Vithkuqi) | Vith | LTR | Golden Vithkuqi | `us` + sign names (`signs-vith`) | none needed | display and input only (words in English) |
| cu-Glag | Church Slavonic (Glagolitic) | Glag | LTR | Golden Glagolitic | `us` + sign names (`signs-glag`) | none needed | display and input only (words in English) |
| got | Gothic | Goth | LTR | Golden Gothic | `us` + sign names (`signs-goth`) | none needed | display and input only (words in English) |
| non | Old Norse | Runr | LTR | Golden Runic | `latn-hist` + sign names (`signs-runr`) | none needed | display and input only (words in English) |
| pgl | Primitive Irish | Ogam | LTR | Golden Ogham | `ie-ogam` | none needed | display and input only (words in English) |
| sga | Old Irish | Latn | LTR | Golden Latin II | `latn-hist` | none needed | display and input only (words in English) |
| goh | Old High German | Latn | LTR | Golden Latin II | `latn-hist` | none needed | display and input only (words in English) |
| enm | Middle English | Latn | LTR | Golden Latin II | `latn-hist` | none needed | display and input only (words in English) |
| fro | Old French | Latn | LTR | Golden Latin II | `latn-hist` | none needed | display and input only (words in English) |
| pro | Old Occitan | Latn | LTR | Golden Latin II | `latn-hist` | none needed | display and input only (words in English) |

## Rosetta: carrying text between languages

Rosetta (`kernel/src/zui/zui_rosetta.h`) is the human-side bridge between
the 183 languages. It does not speak each language natively. It carries what
it recognises through a shared pivot and says plainly what it could not.

- **The pivot.** A sentence in any language is read as parts. A part that is
  one of the screen reader's words or patterns ("кнопка", "3 из 12",
  "5 элементов") is recognised by writing it back with that language's own
  locale data and comparing. It then becomes the reader's English form and
  is said in the target language, numbers in the target's digits included.
  Every pair of the 92 translated languages carries the reader's sentences
  exactly (`test_zui_rosetta`: 153,228 of 153,228 pairs).
- **Bridges.** A script-only language is spoken through its bridge, the
  translated language its readers most likely know: Biblical Hebrew through
  Hebrew, the Aramaic scripts through Syriac, Old and Middle Persian and
  Avestan through Persian, Linear B through Greek, Akkadian and Sumerian
  through English (`bridge` column of `LANGUAGES.tsv`, `zui_locale_bridge`).
- **Gaps.** A part Rosetta does not recognise (a name, a label, typed text)
  goes to a translator the caller plugs in, such as a local model of the
  swarm. Without one, the part is kept as written. The report counts the
  parts bridged, provided and kept.
- **Trust.** The report carries the weakest review state involved. Anything
  from the plugged-in translator or from a machine-made word list is
  `ZUI_REVIEW_MACHINE`, so it is never shown as reviewed.
- **Babel.** Babel is the machine-side counterpart, which explains code. It
  is built by the legacy-languages work and is not in the tree yet.
  `zui_rosetta_explain` takes Babel's plain English explanation through a
  callback (`zui_babel_explain_fn`) and carries it into the person's
  language. Babel's English always counts as machine-made.

**Name.** "Rosetta" and "Babel" are working names. Apple uses Rosetta for its
translation software, and Babel names several software products, so both
need a trademark check before release.

## Gaps, in the order they will be closed

1. **Fonts.** Done: every script has an ASCW Golden face. Tibetan and Thaana
   were the last two; their faces were built from the OFL Noto fonts the
   same way Golden Cherokee was, renamed into the family with its metrics.
   Newly drawn glyphs in the Golden style can replace them later.
2. **Input methods.** Every language has a keyboard layout, and the
   languages that need one have an input method (`zui_ime.h` M6). Pinyin,
   Jyutping and the kanji of Japanese come from the Unihan database
   (`tools/zui/gen_ime_han.py`) one character at a time. Still to come: word
   dictionaries for Chinese, Cantonese and Japanese (phrase conversion), and
   Zhuyin for Taiwan.
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

1. Add a line to `tools/zui/locales/LANGUAGES.tsv`. For a language CLDR
   lacks, name a CLDR locale in the `data` column; for one with no
   vocabulary for a user interface, write `script` in the `ui` column and
   skip step 2.
2. Copy `tools/zui/locales/words/en.json` to `<tag>.json` and translate it.
   The `counts` need the language's CLDR plural categories.
3. Run `python3 tools/zui/gen_locales.py`. It reads CLDR 48.2.0 (downloaded
   from the npm registry once) and rewrites `kernel/src/zui/zui_locale_data.inc`
   and `zui_locale_samples.inc`.
4. Run `sh kernel/src/zui/test_zui_toolkit.sh`. It checks every locale's
   words, placeholders and plural forms against CLDR's own samples.
