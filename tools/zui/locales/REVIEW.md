# Reviewing the screen reader's words

Every file in `words/` except `en.json` is a machine translation made by Claude.
None has been checked by a native speaker yet. This page is for reviewers.

## How to review a language

1. Open `words/<tag>.json` next to `words/en.json`. The keys are the same; `en.json` has the English the screen reader says.
2. Fix any word that is wrong or unnatural for a screen reader. Keep `{x}`, `{n}` and `{k}` where the grammar wants them. `error_prefix` keeps its trailing space.
3. Check the `counts` forms with real numbers (1, 2, 5, 11, 21, 100...).
4. When the whole file is right, change `"review": "unreviewed"` to `"review": "reviewed"` and set `note` to who reviewed it and when.
5. Run `python3 tools/zui/gen_locales.py` and `sh kernel/src/zui/test_zui_toolkit.sh`.

Only a native speaker should mark a file reviewed. The locale test expects every language but English to be unreviewed, so marking one reviewed also means changing that test, which keeps the change visible in a pull request.

## Where to start

These are the keys each translator was least sure of. Earlier languages (the first 30) were translated before this list was kept; review all of their words.

| Tag | Language | Least sure of |
|---|---|---|
| es | Spanish | all words (translated before this list was kept) |
| fr | French | all words (translated before this list was kept) |
| de | German | all words (translated before this list was kept) |
| pt | Portuguese | all words (translated before this list was kept) |
| it | Italian | all words (translated before this list was kept) |
| ru | Russian | all words (translated before this list was kept) |
| uk | Ukrainian | all words (translated before this list was kept) |
| pl | Polish | all words (translated before this list was kept) |
| ar | Arabic | all words (translated before this list was kept) |
| he | Hebrew | all words (translated before this list was kept) |
| fa | Persian | all words (translated before this list was kept) |
| ur | Urdu | all words (translated before this list was kept) |
| hi | Hindi | all words (translated before this list was kept) |
| bn | Bangla | all words (translated before this list was kept) |
| ta | Tamil | all words (translated before this list was kept) |
| zh-Hans | Chinese (Simplified) | all words (translated before this list was kept) |
| zh-Hant | Chinese (Traditional) | all words (translated before this list was kept) |
| ja | Japanese | all words (translated before this list was kept) |
| ko | Korean | all words (translated before this list was kept) |
| th | Thai | all words (translated before this list was kept) |
| vi | Vietnamese | all words (translated before this list was kept) |
| tr | Turkish | all words (translated before this list was kept) |
| id | Indonesian | all words (translated before this list was kept) |
| sw | Swahili | all words (translated before this list was kept) |
| am | Amharic | all words (translated before this list was kept) |
| yo | Yoruba | all words (translated before this list was kept) |
| ha | Hausa | all words (translated before this list was kept) |
| el | Greek | all words (translated before this list was kept) |
| nl | Dutch | all words (translated before this list was kept) |
| mr | Marathi | blank, dimmed, region, verb_low, verb_medium, verb_high |
| te | Telugu | blank, busy, dimmed, not_a_control, verb_medium |
| gu | Gujarati | blank, pending, dimmed, heading_level, verb_low, verb_high |
| kn | Kannada | blank, region, checked, not_checked, partly_checked, no_actions |
| ml | Malayalam | blank, pending, by, from, no_actions |
| pa | Punjabi | region, blank, pending, dialog, verb_medium |
| or | Odia | dimmed, blank, pending, progress, no_actions |
| as | Assamese | blank, pending, dimmed, no_actions, lm_* |
| ne | Nepali | dimmed, pending, verb_low, verb_medium, verb_high |
| si | Sinhala | separator, lm_*, no_next_landmark, no_prev_landmark, no_focus, blank, dialog, reader_on, reader_off |
| sd | Sindhi | separator, pending, lm_complementary, progress, dimmed, no_prev_heading, no_prev_landmark, no_prev_ai |
| ps | Pashto | pending, dimmed, partly_checked, reader_on, lm_*, no_actions |
| sat | Santali | lowest confidence overall: readonly, separator, busy, disabled, partly_checked, dimmed, blank, accepted, rejected, ai_rejected, lm_main, lm_complementary, lm_status, verb_*, by, about, list, region, counts |
| dv | Dhivehi | progress, separator, disabled, dimmed, pending, nothing_to_activate, lm_complementary, heading_level |
| ms | Malay | blank, pending, lm_banner |
| fil | Filipino | readonly, scroll, checkbox, by, no_focus, counts |
| jv | Javanese | readonly, dimmed, lm_banner, lm_search |
| su | Sundanese | dimmed, blank, progress, by, lm_banner |
| my | Burmese | by, no_focus, lm_banner, lm_complementary, nothing_to_activate, position |
| km | Khmer | dimmed, no_focus, lm_complementary, counts.token |
| lo | Lao | dimmed, blank, checkbox, lm_banner, no_actions |
| mn | Mongolian | no_next_landmark, no_prev_landmark, lm_*, from, by, position, textfield |
| bo | Tibetan | lowest confidence overall; most interface terms are coined (button, scroll, progress, checkbox, menu, menuitem, dialog, dimmed, no_focus, nothing_to_activate, not_a_control, lm_*, accept_hint, by, position, counts.token) |
| yue | Cantonese | checkbox, by, counts.token |
| kk | Kazakh | busy, lm_banner, verb_low, verb_medium, verb_high, from, heading_level |
| uz | Uzbek | progress, blank, lm_banner, lm_complementary, shortcut, position |
| ky | Kyrgyz | checkbox, blank, nothing_to_activate, verb_low, verb_medium, verb_high, shortcut, heading_level |
| tg | Tajik | scroll, progress, checkbox, blank, lm_navigation, lm_banner, shortcut, verb_low, verb_medium, verb_high |
| tk | Turkmen | scroll, progress, checkbox, region, blank, lm_banner, shortcut, verb_low, verb_medium, verb_high |
| az | Azerbaijani | checkbox, blank, top, lm_complementary, verb_low, verb_medium, verb_high |
| ka | Georgian | scroll, checkbox, dimmed, blank, position, heading_level |
| hy | Armenian | region, dimmed, blank, lm_banner, position |
| ckb | Central Kurdish | checkbox, dimmed, blank, lm_navigation, lm_complementary, not_a_control, verb_low, verb_medium, verb_high, accept_hint |
| ug | Uyghur | scroll, checkbox, list, menu, lm_banner, lm_navigation, verb_low, verb_medium, verb_high, reader_off, heading_level |
| ro | Romanian | blank, lm_banner, secure, shortcut |
| cs | Czech | no_focus, region, lm_status, blank |
| hu | Hungarian | position, from, by, lm_banner, ai, about |
| sv | Swedish | region, lm_banner, blank, secure |
| bg | Bulgarian | progress, dimmed, blank, not_a_control |
| sr | Serbian | ai, scroll, lm_complementary, blank |
| hr | Croatian | ai, lm_banner, textarea, textfield, blank |
| sk | Slovak | no_focus, pending, region, blank |
| da | Danish | lm_complementary, verb_medium, by, top |
| fi | Finnish | from, by, not_a_control, position, counts.token, blank |
| nb | Norwegian Bokmål | lm_complementary, blank |
| lt | Lithuanian | lm_banner, by, dimmed, counts.token, counts.*.many |
| be | Belarusian | checkbox, secure, lm_banner, lm_navigation, lm_search, lm_status |
| ca | Catalan | lm_banner, counts.*.many, counts.token |
| cy | Welsh | ai, disabled, dimmed, no_actions, verb_low, verb_medium, verb_high, counts.token, counts.*.zero |
| ga | Irish | readonly, dimmed, no_prev_heading, no_prev_landmark, no_prev_ai, not_a_control, verb_low, verb_medium, verb_high, counts.token, counts.*.other |
| zu | Zulu | dimmed, blank, no_focus, nothing_to_activate, lm_banner, lm_complementary, position, counts.character |
| xh | Xhosa | checkbox, busy, blank, no_focus, lm_navigation, about, heading_level, counts (concord prefixes) |
| ig | Igbo | dimmed, scroll, checkbox, checked, not_checked, partly_checked, reader_on, reader_off, lm_navigation, lm_banner, by, counts.token |
| so | Somali | scroll, menu, menuitem, dimmed, shortcut, by, lm_banner, lm_navigation, counts.token |
| om | Oromo | button, scroll, separator, menu, dimmed, blank, by, lm_banner, lm_navigation, counts.token |
| ti | Tigrinya | button, progress, scroll, menu, blank, nothing_to_activate, lm_navigation, verb_low, verb_medium, verb_high, counts.token |
| rw | Kinyarwanda | textarea, scroll, dialog, secure, no_focus, nothing_to_activate, reader_on, reader_off, lm_navigation, lm_status |
| mg | Malagasy | scroll, dialog, no_focus, nothing_to_activate, not_a_control, accept_hint, lm_banner, by, counts.token |
| sn | Shona | scroll, lm_banner, dialog, dimmed, not_a_control, no_focus, by, position, counts.percent |
| wo | Wolof | list, button, checkbox, checked, not_checked, partly_checked, dimmed, not_a_control, lm_banner, lm_navigation, lm_status, by, heading_level, counts.item, progress |
| ln | Lingala | button, checked, not_checked, partly_checked, dimmed, reader_on, reader_off, no_focus, not_a_control, lm_banner, heading_level, counts.character |
| af | Afrikaans | lm_banner, dimmed, not_a_control |
| ff | Fula | progress, checkbox, dialog, secure, disabled, dimmed, no_focus, not_a_control, lm_navigation, by, counts.percent, scroll |
| nqo | N'Ko | every word (written in Latin-letter Manding then converted to N'Ko by rule; no tone or vowel-length marks, not standard spelling); terms especially list, scroll, separator, lm_*, no_focus, not_a_control, reader_on, reader_off, dimmed, position, counts.character |
| zgh | Standard Moroccan Tamazight | button, scroll, separator, dimmed, rejected, no_focus, nothing_to_activate, not_a_control, lm_banner, lm_navigation, verb_low, verb_medium, verb_high, shortcut, counts.token, counts.percent |
| qu | Quechua | button, checkbox, menu, menuitem, separator, no_focus, not_a_control, lm_banner, lm_main, lm_status, heading_level, position, counts.item, counts.token |
| gn | Guarani | ai, heading, checked, not_checked, partly_checked, blank, scroll, lm_banner, lm_complementary, no_focus, heading_level, counts.percent |
| chr | Cherokee | rough draft, needs a full review; lowest: button, list, scroll, ai, separator, secure, region, lm_*, progress, checked, dimmed, error, not_a_control, no_actions, accept_hint, position, counts.token, counts.percent |
| nv | Navajo | rough draft, needs a full review; lowest: group, button, list, scroll, ai, progress, separator, heading, checkbox, region, error, menu, menuitem, option, selected, secure, dimmed, top, end, lm_*, no_focus, not_a_control, about, counts.character, counts.token |
| mi | Māori | not_a_control, dimmed, ai, counts.token |
| haw | Hawaiian | scroll, dimmed, no_focus, not_a_control, lm_*, checkbox, counts.token |
| iu | Inuktitut | rough draft, needs a full review; lowest: button, ai, scroll, region, heading, about, by, no_focus, no_actions, verb_medium, heading_level, counts.token, counts.percent |
| se | Northern Sami | checkbox, dimmed, blank, from, by, position, no_focus, not_a_control, lm_* |
| ht | Haitian Creole | dimmed, blank, from |
