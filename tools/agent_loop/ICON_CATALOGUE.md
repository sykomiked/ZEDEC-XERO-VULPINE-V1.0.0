# The icon catalogue — the ecosystem, not just the apps

The 407 ZXVG icons are not 407 app icons. They are the **catalogue of the
whole ecosystem**: the applications, the archetypes, the rarity tiers, the
progression stages and the singular artifacts. Mapping them correctly is
part of the completion measure, because the icon set is the system's
public face and every element of the ecosystem must be representable.

## The families

| Family | Count | What it names | Where it lives |
|---|---|---|---|
| **the applications** | 143 | the apps in the matrix | `web/apps/names.tsv`, the desk, the wasm |
| **`dragon-*`** | **84** | the archetypes — the professions and roles (archivist, smith, navigator, healer, gardener, courier, scholar, mechanist, sentinel, cartographer, alchemist, orator, musician, builder, cook, astronomer, sailor, weaver, miner, diplomat, coder, painter, photographer, …) | the bearer's player-card, the raffle, the space identities |
| **the gemstone tiers** | ~180 | the **rarity tiers** — seven per gemstone (crystal, emerald, sapphire, amethyst, ruby, jade, garnet, opal, quartz, citrine, aquamarine, moonstone, tourmaline, obsidian, diamond, topaz, prism, gem) | the six-tier card matrix, the Iridium Black Swan apex |
| **`hatchling`** | 4 | the progression stage — the first form | the account's growth |
| **`geode`, `gemstone`, `jewel`** | 9 | the item classes | the economy's holdings |
| **the singular artifacts** (`hearth`, `cloud`, `storyteller`, `sea`, `moth`, `clockwork`, `star`) | 7 | one-of-a-kind elements | the ecosystem's landmarks |

## Why this matters for completeness

The lattice completion measure counts the **144 matrix cells**, but the
ecosystem is broader than the matrix: it includes the archetypes (who a
bearer *is*), the tiers (what a card *is*), the progression (how an
account *grows*) and the artifacts (what the system *holds*). The icons
name all of them, so the icon set is the **public map of the ecosystem** —
and it must be complete, not just the app slice.

**The honest correction**: `web/apps/names.tsv` maps 143 apps, and the
other 264 icons were reported as "unmatched." They are not unmatched —
they are **correctly named for elements that are not apps.** The mapping
must be extended from "app ↔ icon" to **"ecosystem element ↔ icon"**,
with each icon's family recorded.

## What the H100 must build

1. **Extend the mapping** (`web/apps/names.tsv` → `web/catalogue.tsv`):
   each icon row gains its family (app, archetype, tier, stage, item,
   artifact) and the element it names.
2. **The archetype table**: the 84 `dragon-*` roles, each with the
   capability set and the rail (Dragon / Phoenix / Thunderbird) it serves.
3. **The tier table**: the gemstone families, each with its seven tiers
   and the bps/multiplier they carry (33–99 bps, ×1–×32).
4. **The catalogue test, in all three spaces**:
   - *positive*: every icon resolves to an element and every element has
     an icon;
   - *negative*: no icon resolves to nothing (an orphaned icon is a gap,
     as an orphaned module is);
   - *neutral*: the boundary families (the first hatchling, the apex
     Iridium Black Swan) resolve deterministically.
5. **The desk rendering test**: every family renders at its grid size,
   and the picker shows each element's icon in its own right.

## The honest boundary

The families above are read from the actual icon filenames, so the counts
are exact; what is **not yet built** is the element-by-element mapping
(which archetype is which role, which gemstone is which tier). That is
real, bounded work — and it is the same shape as the app mapping, which
already exists as `names.tsv`. The correction is: **the catalogue is the
ecosystem's, and the icons are its map.**
