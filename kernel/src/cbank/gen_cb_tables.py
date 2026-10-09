#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate kernel/src/cbank/cb_ccy_tbl.c (ISO 4217 + ISO 3166-1 + AU tables).

Inputs (both copied into kernel/src/cbank/data/ so the build is reproducible
without network access; see data/PROVENANCE.txt):
  data/list-one.xml     ISO 4217 "List One" (current currency & funds code
                        list) as published by SIX Financial Information, the
                        ISO 4217 maintenance agency, Pblshd="2026-01-01".
  data/iso_3166-1.json  ISO 3166-1 country codes from Debian iso-codes 4.16.0.

The African Union member list, AU regions and monetary-union tags below are
hand-curated (they are not part of either ISO file). Currencies of each AU
member are NOT hand-written: they are taken from list-one.xml, and the
generator fails if any member has no currency there.

When SIX publishes a new list (amendments are announced on the SIX site),
replace data/list-one.xml with the new file and regenerate:

  python3 -I gen_cb_tables.py data/list-one.xml data/iso_3166-1.json > cb_ccy_tbl.c

(The live download URL, which this build environment could not reach, is
https://www.six-group.com/dam/download/financial-information/data-center/iso-currrency/lists/list-one.xml)
"""
import json
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET

AU_ASOF = "2026-10-09"

# (alpha-2, AU region, monetary union, short AU name)
# Regions: N North, W West, C Central, E East, S Southern (AU five regions).
# Unions: WAEMU (BCEAO, XOF), CEMAC (BEAC, XAF), CMA (Common Monetary Area,
# ZAR anchor), or none.
AU = [
    ("DZ", "N", "", "Algeria"), ("AO", "S", "", "Angola"), ("BJ", "W", "WAEMU", "Benin"),
    ("BW", "S", "", "Botswana"), ("BF", "W", "WAEMU", "Burkina Faso"),
    ("BI", "C", "", "Burundi"), ("CV", "W", "", "Cabo Verde"), ("CM", "C", "CEMAC", "Cameroon"),
    ("CF", "C", "CEMAC", "Central African Republic"), ("TD", "C", "CEMAC", "Chad"),
    ("KM", "E", "", "Comoros"), ("CG", "C", "CEMAC", "Congo"),
    ("CD", "C", "", "Democratic Republic of the Congo"),
    ("CI", "W", "WAEMU", "Cote d'Ivoire"), ("DJ", "E", "", "Djibouti"), ("EG", "N", "", "Egypt"),
    ("GQ", "C", "CEMAC", "Equatorial Guinea"), ("ER", "E", "", "Eritrea"),
    ("SZ", "S", "CMA", "Eswatini"), ("ET", "E", "", "Ethiopia"), ("GA", "C", "CEMAC", "Gabon"),
    ("GM", "W", "", "Gambia"), ("GH", "W", "", "Ghana"), ("GN", "W", "", "Guinea"),
    ("GW", "W", "WAEMU", "Guinea-Bissau"), ("KE", "E", "", "Kenya"), ("LS", "S", "CMA", "Lesotho"),
    ("LR", "W", "", "Liberia"), ("LY", "N", "", "Libya"), ("MG", "E", "", "Madagascar"),
    ("MW", "S", "", "Malawi"), ("ML", "W", "WAEMU", "Mali"), ("MR", "N", "", "Mauritania"),
    ("MU", "E", "", "Mauritius"), ("MA", "N", "", "Morocco"), ("MZ", "S", "", "Mozambique"),
    ("NA", "S", "CMA", "Namibia"), ("NE", "W", "WAEMU", "Niger"), ("NG", "W", "", "Nigeria"),
    ("RW", "E", "", "Rwanda"),
    # The AU member is the Sahrawi Arab Democratic Republic. ISO 3166-1 has
    # no code for the SADR as a state; EH/ESH is the code of the territory
    # "Western Sahara", and ISO 4217 list one gives MAD for that entry.
    ("EH", "N", "", "Sahrawi Arab Democratic Republic"),
    ("ST", "C", "", "Sao Tome and Principe"), ("SN", "W", "WAEMU", "Senegal"),
    ("SC", "E", "", "Seychelles"), ("SL", "W", "", "Sierra Leone"), ("SO", "E", "", "Somalia"),
    ("ZA", "S", "CMA", "South Africa"), ("SS", "E", "", "South Sudan"), ("SD", "E", "", "Sudan"),
    ("TZ", "E", "", "Tanzania"), ("TG", "W", "WAEMU", "Togo"), ("TN", "N", "", "Tunisia"),
    ("UG", "E", "", "Uganda"), ("ZM", "S", "", "Zambia"), ("ZW", "S", "", "Zimbabwe"),
]

REGION = {"N": "CB_AU_NORTH", "W": "CB_AU_WEST", "C": "CB_AU_CENTRAL", "E": "CB_AU_EAST",
          "S": "CB_AU_SOUTHERN"}
UNION = {"": "CB_MU_NONE", "WAEMU": "CB_MU_WAEMU", "CEMAC": "CB_MU_CEMAC", "CMA": "CB_MU_CMA"}

# list-one country names the normaliser cannot match to ISO 3166-1 names.
OVERRIDE = {
    "HOLY SEE (THE)": "VA",
    "KOREA (THE DEMOCRATIC PEOPLE’S REPUBLIC OF)": "KP",
    "LAO PEOPLE’S DEMOCRATIC REPUBLIC (THE)": "LA",
}


def norm(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().upper()
    s = re.sub(r"[^A-Z ]", " ", s)
    return " ".join(sorted(w for w in s.split() if w not in ("THE", "OF", "AND")))


def cstr(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    root = ET.parse(sys.argv[1]).getroot()
    pub = root.get("Pblshd")
    c3166 = json.load(open(sys.argv[2], encoding="utf-8"))["3166-1"]
    byname = {}
    for e in c3166:
        for k in ("name", "official_name", "common_name"):
            if k in e:
                byname[norm(e[k])] = e["alpha_2"]
    a2set = {e["alpha_2"] for e in c3166}

    ccy = {}  # alpha -> (num, minor, fund, name)
    links = []  # (a2, alpha)
    for n in root.iter("CcyNtry"):
        ctry = n.find("CtryNm").text.strip()
        a = n.find("Ccy")
        if a is None:
            continue  # "No universal currency" (Antarctica, Palestine)
        alpha = a.text.strip()
        num = int(n.find("CcyNbr").text)
        mu = n.find("CcyMnrUnts").text.strip()
        minor = 255 if mu == "N.A." else int(mu)
        nm = n.find("CcyNm")
        fund = nm.get("IsFund") == "true"
        rec = (num, minor, fund, nm.text.strip())
        if alpha in ccy and ccy[alpha] != rec:
            if ccy[alpha][:2] != rec[:2]:
                sys.exit("inconsistent entry for " + alpha)
        ccy.setdefault(alpha, rec)
        a2 = OVERRIDE.get(ctry) or byname.get(norm(ctry))
        if a2 and (a2, alpha) not in links:
            links.append((a2, alpha))

    nums = [v[0] for v in ccy.values()]
    if len(set(nums)) != len(nums):
        sys.exit("duplicate numeric code")
    for code in ("VFV",):
        if code in ccy:
            sys.exit("VFV appeared in ISO 4217 list one: review cbank")
    for n in (846, 810, 888):
        if n in nums:
            sys.exit("a Vino rail numeric is now an ISO 4217 code: review cbank")

    alphas = sorted(ccy)
    aidx = {a: i for i, a in enumerate(alphas)}
    out = []
    w = out.append
    w("/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */")
    w("/* SPDX-License-Identifier: Apache-2.0 */")
    w("/* cb_ccy_tbl.c - GENERATED by gen_cb_tables.py. Do not edit by hand.")
    w(" * ISO 4217 list one Pblshd=%s (SIX Financial Information);" % pub)
    w(" * ISO 3166-1 from Debian iso-codes 4.16.0; AU membership as of %s. */" % AU_ASOF)
    w('#include "cb_ccy.h"')
    w("")
    w('const char cb_iso4217_published[] = "%s";' % pub)
    w('const char cb_au_asof[] = "%s";' % AU_ASOF)
    w("")
    w("const cb_ccy_t cb_ccy_tbl[CB_CCY_COUNT] = {")
    for a in alphas:
        num, minor, fund, name = ccy[a]
        fl = []
        if fund:
            fl.append("CB_CCYF_FUND")
        if minor == 255:
            fl.append("CB_CCYF_NA")
        if not any(x[1] == a for x in links):
            fl.append("CB_CCYF_NOCTRY")
        w('    {"%s", %d, %d, %s, %s},' % (a, num, minor, "|".join(fl) or "0", cstr(name)))
    w("};")
    w("")
    byn = sorted(range(len(alphas)), key=lambda i: ccy[alphas[i]][0])
    w("const uint16_t cb_ccy_by_num_idx[CB_CCY_COUNT] = {")
    for i in range(0, len(byn), 12):
        w("    " + ", ".join(str(x) for x in byn[i:i + 12]) + ",")
    w("};")
    w("")
    ctry = sorted(c3166, key=lambda e: e["alpha_2"])
    w("const cb_country_t cb_country_tbl[CB_COUNTRY_COUNT] = {")
    for e in ctry:
        w('    {"%s", "%s", %d, %s},' % (e["alpha_2"], e["alpha_3"], int(e["numeric"]),
                                       cstr(e.get("common_name", e["name"]))))
    w("};")
    w("")
    links.sort(key=lambda x: (x[0], x[1]))
    for a2, _ in links:
        if a2 not in a2set:
            sys.exit("unknown country " + a2)
    w("const cb_ctry_ccy_t cb_ctry_ccy_tbl[CB_CTRY_CCY_COUNT] = {")
    for a2, a in links:
        w('    {"%s", %d}, /* %s */' % (a2, aidx[a], a))
    w("};")
    w("")
    if len(AU) != 55 or len({x[0] for x in AU}) != 55:
        sys.exit("AU list must hold 55 distinct members")
    w("const cb_au_member_t cb_au_tbl[CB_AU_COUNT] = {")
    for a2, r, u, nm in sorted(AU):
        if a2 not in a2set:
            sys.exit("AU member not in ISO 3166-1: " + a2)
        if not any(x[0] == a2 for x in links):
            sys.exit("AU member without an ISO 4217 currency: " + a2)
        w('    {"%s", %s, %s, %s},' % (a2, REGION[r], UNION[u], cstr(nm)))
    w("};")
    sys.stdout.write("\n".join(out) + "\n")
    sys.stderr.write("CB_CCY_COUNT %d CB_COUNTRY_COUNT %d CB_CTRY_CCY_COUNT %d CB_AU_COUNT %d\n" %
                     (len(alphas), len(ctry), len(links), len(AU)))


if __name__ == "__main__":
    main()
