# Data packs

Three apps need data the system does not make: Weatherglass (88), Wordhoard (46) and Fieldguide (102). Their data comes as packs: release assets, never committed to git, each carrying its licences and attribution. The rule is the owner's decision of 2026-10-15 (inside-out gap map §5.2), in the kernel's four values:

| Value | Licence | Where it goes |
|---|---|---|
| TRUE | commercial use with attribution (CC0, CC BY, NLOD, public domain, the WordNet licence, MIT, BSD, Apache) | the core pack |
| BOTH | commercial use, share-alike (CC BY-SA, ODbL, GFDL) | a separate optional pack, never mixed with the Apache-2.0 code |
| FALSE | non-commercial or no derivatives | never used |
| NEITHER | a licence the table does not know | refused, never guessed |

## Sources

`tools/apps/datapacks/SOURCES.tsv` is the recipe: one row per source, with its pack, kind, licence, TLS address and attribution.

| App | Core pack (TRUE) | Optional pack (BOTH) |
|---|---|---|
| 88 Weatherglass | MET Norway Locationforecast (CC BY 4.0), US NWS alerts (public domain): live endpoints | none |
| 46 Wordhoard | Open English WordNet 2024 (CC BY 4.0); Open Multilingual Wordnet 1.4, each lexicon by its own licence | Wiktionary via Wiktextract (CC BY-SA 4.0) |
| 102 Fieldguide | GBIF Backbone Taxonomy (CC BY 4.0); Wikidata species with an English name (CC0) | Wikipedia, Kiwix ZIM (CC BY-SA 4.0) |

Kinds: `file` is one download; `sparql` is `wikidata_taxa.rq`, run once per first digit of the GBIF ID (one query is too slow for the service); `lmf` is a wordnet archive whose lexicons are classed one by one by the `license` they carry, so a non-commercial or unlicensed lexicon is left out and named in the pack's `EXCLUDED.tsv`; `live` is an endpoint the app reads over TLS itself, so the pack carries only its terms (`ENDPOINTS.tsv`).

## Making a pack

```
python3 tools/apps/datapacks/datapack.py check                      # the table is sound
python3 tools/apps/datapacks/datapack.py fetch wordhoard --approved # only on the owner's go
python3 tools/apps/datapacks/datapack.py build wordhoard core       # build/datapacks/wordhoard-core.tar
python3 tools/apps/datapacks/datapack.py build wordhoard optional
```

Nothing is fetched without `--approved`, and nothing is written outside `build/`. A pack is a plain tar with its names sorted and times, owners and modes fixed, so the same fetched files give the same pack byte for byte; it holds `data/<source>/...`, `PACK.tsv` (app, slot, pack, value), `LICENSES.tsv`, `ATTRIBUTION.txt` (which the app shows), `MANIFEST.tsv` (sha256 and size of every file), and `EXCLUDED.tsv` or `ENDPOINTS.tsv` when they apply. `fetch` records each file's sha256 in `FETCHED.tsv`, and `build` refuses a file that changed since.

`test_zxapp.sh` runs `datapack.py check` and `test_datapack.py` (the classes, every table rule, no fetch without approval or outside `build/`, and fixture packs built twice to the same bytes).

## Open

Fieldguide's caution ("a matched species is toxic or dangerous") has no source in the table yet: neither GBIF's backbone nor the Wikidata query marks toxicity. It stays NEITHER until a permissively licensed source for it is named.
