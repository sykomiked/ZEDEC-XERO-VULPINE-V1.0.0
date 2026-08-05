#!/usr/bin/env python3
"""build_card_index.py — index the Glyph & Grid activation cards for ZXV.

WHY THERE IS NO OCR HERE
------------------------
The cards were *generated* from structured source data, and that data still
exists: metadata/full_card_metadata.json holds 52,095 records keyed by an
`index`, and every rendered card is named seal_<index>.png. Verified:
seal_24525.png renders exactly the record at index 24525 (OLPIRT HPOU /
"Light Portal Creation" / gematria 54 / XVI The Tower).

So the catalog index is built by an exact join, not by reading pixels.
Running OCR over 52,099 PNGs would be slower, lossy, and would produce a
worse index than the ground truth already on disk.

OCR still has a real job — recognising a PHYSICAL card someone photographs
to activate it on a device — but that is a lookup against this catalog,
not a way to build it. And having the catalog makes that recognition far
more reliable, because a scan only has to select among known cards rather
than transcribe free text.

OUTPUT
  cards.tsv     human/diff friendly, one card per line
  cards.zcx     compact binary the kernel loads from ZXVFS

Usage:
  build_card_index.py <seals_dir> <metadata.json> <outdir>
"""
import sys, os, json, struct, hashlib

def main():
    if len(sys.argv) != 4:
        print(__doc__); sys.exit(2)
    seals_dir, meta_path, outdir = sys.argv[1:4]
    os.makedirs(outdir, exist_ok=True)

    meta = json.load(open(meta_path, encoding="utf-8"))
    by_index = {r["index"]: r for r in meta}
    print(f"metadata records: {len(by_index)}")

    # walk the rendered cards; the directory gives discipline + set,
    # the filename gives the index
    rows, missing, malformed = [], 0, 0
    for entry in sorted(os.listdir(seals_dir)):
        d = os.path.join(seals_dir, entry)
        if not os.path.isdir(d):
            continue
        if "_Set" in entry:
            discipline, setno = entry.rsplit("_Set", 1)
        else:
            discipline, setno = entry, "0"
        discipline = discipline.replace("_", " ").strip()
        for fn in sorted(os.listdir(d)):
            if not fn.startswith("seal_") or not fn.endswith(".png"):
                continue
            try:
                idx = int(fn[5:-4])
            except ValueError:
                malformed += 1
                continue
            rec = by_index.get(idx)
            if rec is None:
                missing += 1
                continue
            rows.append({
                "index": idx,
                "discipline": discipline,
                "set": int(setno) if setno.isdigit() else 0,
                "name": rec["name"],
                "enochian": rec["enochian"],
                "phonetic": rec.get("phonetic", ""),
                "gematria": int(rec.get("gematria", 0) or 0),
                "root": int(rec.get("root", 0) or 0),
                "tarot": rec.get("tarot_card", ""),
                "path": os.path.join(entry, fn),
                "activation": rec.get("arcane_description", ""),
            })

    rows.sort(key=lambda r: (r["discipline"], r["set"], r["index"]))
    print(f"cards indexed : {len(rows)}")
    print(f"missing meta  : {missing}")
    print(f"malformed name: {malformed}")

    disciplines = sorted({r["discipline"] for r in rows})
    print(f"disciplines   : {len(disciplines)}")

    # ---- TSV (diffable ground truth) ----
    tsv = os.path.join(outdir, "cards.tsv")
    with open(tsv, "w", encoding="utf-8") as f:
        f.write("index\tdiscipline\tset\tname\tenochian\tgematria\troot\ttarot\tpath\n")
        for r in rows:
            f.write(f"{r['index']}\t{r['discipline']}\t{r['set']}\t{r['name']}\t"
                    f"{r['enochian']}\t{r['gematria']}\t{r['root']}\t{r['tarot']}\t{r['path']}\n")

    # ---- compact binary for the kernel ----
    # Header: magic 'ZCX1', count, discipline count
    # Record (fixed 64 B): index u32 | disc u8 | set u8 | gematria u16 |
    #                      root u8 | tarot u8 | pad u2 | name[36] | enochian[16]
    disc_id = {d: i for i, d in enumerate(disciplines)}
    TAROT = sorted({r["tarot"] for r in rows})
    tarot_id = {t: i for i, t in enumerate(TAROT)}

    def fixed(s, n):
        b = s.encode("utf-8", "replace")[:n]
        return b + b"\0" * (n - len(b))

    zcx = os.path.join(outdir, "cards.zcx")
    with open(zcx, "wb") as f:
        f.write(b"ZCX1")
        f.write(struct.pack("<II", len(rows), len(disciplines)))
        for d in disciplines:
            f.write(fixed(d, 24))
        for r in rows:
            f.write(struct.pack("<IBBHBBH",
                                r["index"], disc_id[r["discipline"]], r["set"],
                                min(r["gematria"], 65535), r["root"] & 0xFF,
                                tarot_id.get(r["tarot"], 0) & 0xFF, 0))
            f.write(fixed(r["name"], 36))
            f.write(fixed(r["enochian"], 16))

    size = os.path.getsize(zcx)
    print(f"\nwrote {tsv}  ({os.path.getsize(tsv):,} bytes)")
    print(f"wrote {zcx}  ({size:,} bytes, {size/len(rows):.1f} B/card)")

    # integrity digest so the index can be signed like any other payload
    h = hashlib.sha256(open(zcx, "rb").read()).hexdigest()
    print(f"cards.zcx sha256: {h}")

    # a per-discipline summary — this is the collection structure
    print("\ndiscipline            cards   sets")
    from collections import Counter
    cnt = Counter(r["discipline"] for r in rows)
    sets = {}
    for r in rows:
        sets.setdefault(r["discipline"], set()).add(r["set"])
    for d in disciplines[:12]:
        print(f"  {d:<20s} {cnt[d]:5d}   {sorted(sets[d])}")
    if len(disciplines) > 12:
        print(f"  ... and {len(disciplines)-12} more disciplines")

if __name__ == "__main__":
    main()
