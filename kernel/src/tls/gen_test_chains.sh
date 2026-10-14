#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# gen_test_chains.sh — build the synthetic certificate chains test_trust.c
# uses for the known-bad cases (wrong name, not a CA, path length, key usage,
# EKU, critical extension, weak key, SHA-1, name constraints, ...). Every one
# is made by OpenSSL, an independent implementation, so the verifier is not
# graded against its own idea of a certificate.
#
# Usage: gen_test_chains.sh WORKDIR > test_trust_chains.h
# Keys are random, so a rerun produces different bytes with the same meaning.
set -euo pipefail
W=$1
mkdir -p "$W"
cd "$W"
rm -f ./*.pem ./*.der ./*.cnf ./*.srl

ec256() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1" 2>/dev/null; }
ec384() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-384 -out "$1" 2>/dev/null; }
rsa() { openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:"$2" -out "$1" 2>/dev/null; }

# ext NAME LINES...: write an extension section file
ext() {
  local f=$1.cnf
  shift
  printf '[x]\n' >"$f"
  for l in "$@"; do printf '%s\n' "$l" >>"$f"; done
}

# sign SUBJ_KEY CN ISSUER_CERT ISSUER_KEY EXTNAME OUT [extra x509 args]
sign() {
  local key=$1 cn=$2 icert=$3 ikey=$4 extn=$5 out=$6
  shift 6
  openssl req -new -key "$key" -subj "/O=ZXV Test/CN=$cn" -out "$out.csr" 2>/dev/null
  openssl x509 -req -in "$out.csr" -CA "$icert" -CAkey "$ikey" -set_serial "0x$(openssl rand -hex 8)" \
    -days 400 -extfile "$extn.cnf" -extensions x "$@" -out "$out.pem" 2>/dev/null
  openssl x509 -in "$out.pem" -outform DER -out "$out.der"
}

# selfsign KEY SUBJ EXTNAME OUT [extra req args]
selfsign() {
  local key=$1 subj=$2 extn=$3 out=$4
  shift 4
  { printf '[req]\ndistinguished_name=d\n[d]\n'; cat "$extn.cnf"; } >"$extn.req.cnf"
  openssl req -new -x509 -key "$key" -subj "$subj" -config "$extn.req.cnf" -extensions x \
    "$@" -out "$out.pem" 2>/dev/null
}

root() { # KEY CN OUT [extra]
  local key=$1 cn=$2 out=$3
  shift 3
  ext root_ext "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign,cRLSign" \
    "subjectKeyIdentifier=hash"
  selfsign "$key" "/O=ZXV Test/CN=$cn" root_ext "$out" -days 3650 "$@"
  openssl x509 -in "$out.pem" -outform DER -out "$out.der"
}

ec256 root.key
root root.key "ZXV Test Root" root
ec384 other.key
root other.key "ZXV Other Root" otherroot

rsa int.key 2048
ext ca0 "basicConstraints=critical,CA:TRUE,pathlen:0" "keyUsage=critical,keyCertSign,cRLSign" \
  "extendedKeyUsage=serverAuth"
ext ca "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign,cRLSign"
sign int.key "ZXV Test Intermediate" root.pem root.key ca0 int

ec384 leaf.key
ext leaf "basicConstraints=critical,CA:FALSE" "keyUsage=critical,digitalSignature" \
  "extendedKeyUsage=serverAuth" \
  "subjectAltName=DNS:test.zxv.example,DNS:*.wild.zxv.example,IP:192.0.2.7"
# the intermediate signs with RSA-PSS (SHA-256, salt = digest length)
sign leaf.key test.zxv.example int.pem int.key leaf leaf \
  -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:digest -sha256

# RSA-PKCS1 SHA-384 leaf with an RSA key, signed by the intermediate
rsa leafrsa.key 2048
sign leafrsa.key test.zxv.example int.pem int.key leaf leaf_rsa -sha384

# an intermediate that is not a CA, and one with no basicConstraints
ext notca "basicConstraints=critical,CA:FALSE" "keyUsage=critical,keyCertSign"
ec256 nc.key
sign nc.key "ZXV Not A CA" root.pem root.key notca int_notca
sign leaf.key test.zxv.example int_notca.pem nc.key leaf leaf_under_notca
ext nobc "keyUsage=critical,keyCertSign"
sign nc.key "ZXV No BC" root.pem root.key nobc int_nobc
sign leaf.key test.zxv.example int_nobc.pem nc.key leaf leaf_under_nobc

# path length: root -> int (pathlen 0) -> int2 -> leaf
ec256 int2.key
sign int2.key "ZXV Test Intermediate 2" int.pem int.key ca int2
sign leaf.key test.zxv.example int2.pem int2.key leaf leaf_under_int2

# intermediate whose keyUsage lacks keyCertSign
ext noks "basicConstraints=critical,CA:TRUE" "keyUsage=critical,digitalSignature,cRLSign"
sign nc.key "ZXV No CertSign" root.pem root.key noks int_noks
sign leaf.key test.zxv.example int_noks.pem nc.key leaf leaf_under_noks

# intermediate restricted to client auth
ext ekuclient "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign" \
  "extendedKeyUsage=clientAuth"
sign nc.key "ZXV Client Only CA" root.pem root.key ekuclient int_client
sign leaf.key test.zxv.example int_client.pem nc.key leaf leaf_under_client

# leaf problems, all issued by the good intermediate
ext l_client "keyUsage=critical,digitalSignature" "extendedKeyUsage=clientAuth" \
  "subjectAltName=DNS:test.zxv.example"
sign leaf.key test.zxv.example int.pem int.key l_client leaf_eku_client
ext l_ku "keyUsage=critical,keyEncipherment" "subjectAltName=DNS:test.zxv.example"
sign leaf.key test.zxv.example int.pem int.key l_ku leaf_ku_bad
ext l_crit "keyUsage=critical,digitalSignature" "subjectAltName=DNS:test.zxv.example" \
  "1.3.6.1.4.1.55555.1=critical,ASN1:NULL"
sign leaf.key test.zxv.example int.pem int.key l_crit leaf_crit
ext l_nosan "keyUsage=critical,digitalSignature"
sign leaf.key test.zxv.example int.pem int.key l_nosan leaf_nosan
ext l_tld "subjectAltName=DNS:*.example,DNS:*,DNS:a*.zxv.example"
sign leaf.key test.zxv.example int.pem int.key l_tld leaf_badwild
rsa weak.key 1024
sign weak.key test.zxv.example int.pem int.key leaf leaf_weak
sign leafrsa.key test.zxv.example int.pem int.key leaf leaf_sha1 -sha1

# name constraints: permitted zxv.example, excluded bad.zxv.example
ext ncon "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign" \
  "nameConstraints=critical,permitted;DNS:zxv.example,excluded;DNS:bad.zxv.example"
sign nc.key "ZXV Constrained CA" root.pem root.key ncon int_ncon
sign leaf.key test.zxv.example int_ncon.pem nc.key leaf leaf_ncon_ok
ext l_evil "keyUsage=critical,digitalSignature" "subjectAltName=DNS:test.zxv.example,DNS:evil.example"
sign leaf.key test.zxv.example int_ncon.pem nc.key l_evil leaf_ncon_evil
ext l_bad "keyUsage=critical,digitalSignature" "subjectAltName=DNS:x.bad.zxv.example,DNS:test.zxv.example"
sign leaf.key test.zxv.example int_ncon.pem nc.key l_bad leaf_ncon_excluded
ext ncdir "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign" \
  "nameConstraints=critical,permitted;dirName:dir_sect" "[dir_sect]" "O=ZXV Test"
sign nc.key "ZXV DirName CA" root.pem root.key ncdir int_ncdir
sign leaf.key test.zxv.example int_ncdir.pem nc.key leaf leaf_ncdir

# a self-signed server certificate (pinned by adding it to the store)
ext selfs "keyUsage=critical,digitalSignature" "subjectAltName=DNS:device.local"
selfsign leaf.key "/CN=device.local" selfs selfsigned -days 400
openssl x509 -in selfsigned.pem -outform DER -out selfsigned.der

# The in-test TLS server in test_handshake.c signs CertificateVerify with
# the leaf_rsa key, so its modulus and private exponent are exported too.
# A throwaway key made for this test; it protects nothing.
openssl rsa -in leafrsa.key -noout -text 2>/dev/null >leafrsa.txt

python3 - "$W" <<'PY'
import sys, os, re
w = sys.argv[1]
names = ["root", "otherroot", "int", "leaf", "leaf_rsa", "int_notca", "leaf_under_notca",
         "int_nobc", "leaf_under_nobc", "int2", "leaf_under_int2", "int_noks",
         "leaf_under_noks", "int_client", "leaf_under_client", "leaf_eku_client", "leaf_ku_bad",
         "leaf_crit", "leaf_nosan", "leaf_badwild", "leaf_weak", "leaf_sha1", "int_ncon",
         "leaf_ncon_ok", "leaf_ncon_evil", "leaf_ncon_excluded", "int_ncdir", "leaf_ncdir",
         "selfsigned"]
print("/* Generated by gen_test_chains.sh (OpenSSL). Do not edit.")
print(" * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC")
print(" * SPDX-License-Identifier: Apache-2.0 */")
for n in names:
    b = open(os.path.join(w, n + ".der"), "rb").read()
    print("static const uint8_t T_%s[%d] = {" % (n.upper(), len(b)))
    for i in range(0, len(b), 16):
        print("    " + ", ".join("0x%02x" % x for x in b[i:i + 16]) + ",")
    print("};")
txt = open(os.path.join(w, "leafrsa.txt")).read()
def field(name):
    m = re.search(name + r":\s*\n((?:\s+[0-9a-f:]+\n)+)", txt)
    return bytes.fromhex(re.sub(r"[\s:]", "", m.group(1))).lstrip(b"\0")
for n, f in (("LEAFRSA_N", "modulus"), ("LEAFRSA_D", "privateExponent")):
    b = field(f)
    print("/* test-only key: %s of the leaf_rsa key */" % f)
    print("static const uint8_t T_%s[%d] = {" % (n, len(b)))
    for i in range(0, len(b), 16):
        print("    " + ", ".join("0x%02x" % x for x in b[i:i + 16]) + ",")
    print("};")
PY
