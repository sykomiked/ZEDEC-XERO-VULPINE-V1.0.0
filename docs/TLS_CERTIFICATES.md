<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# TLS server certificate verification

Before this change, the kernel TLS 1.3 client (`kernel/src/tls`) encrypted
connections but did not check who it was talking to. Anyone who could reroute
a connection could stand in for the server. Now a client built with
`TLS_VERIFY_REQUIRED` checks the server's certificate chain and its
CertificateVerify signature. It reports itself authenticated only when both
pass.

## Using it

```c
static tls_trust_t trust;            /* lives as long as the connections */
tls_trust_init(&trust);
tls_trust_add_builtin(&trust);       /* the compiled-in web roots */
tls_trust_add_der(&trust, my_root, my_root_len);  /* optional: a private CA */

tls_client_init(&c, "example.org", random32, priv32, TLS_VERIFY_REQUIRED);
tls_client_set_trust(&c, &trust, timesvc_now_s(&clock, local_us)); /* see TIME.md */
/* ... tls_client_hello / tls_client_feed as before ... */
if (tls_client_is_authenticated(&c)) { /* the server is who it says */ }
```

On failure, `c.error` is `TLS_ERR_BAD_CERT` (and `c.cert_status` says which
check failed), `TLS_ERR_BAD_SIGNATURE`, or `TLS_ERR_NO_VERIFIER` when no
trust store or time was given.

## What is checked

| Check | Where |
|---|---|
| Every signature from the leaf up to a trusted root | `trust.c` |
| Validity dates of every certificate, the root included | `trust.c` |
| The leaf's subjectAltName covers the host name. There is no common-name fallback. A wildcard covers exactly one label, and `*.com` covers nothing | `x509.c` |
| Leaf keyUsage includes digitalSignature, and extendedKeyUsage includes serverAuth, when those extensions are present | `trust.c` |
| Every issuer is a CA, may sign certificates, allows serverAuth, and respects its path length | `trust.c` |
| dNSName and iPAddress name constraints. Any other constraint form fails closed | `x509.c` |
| Unknown critical extensions, including the CT precertificate poison, are refused | `x509.c` |
| Keys: RSA 2048-4096 bits, EC P-256 or P-384. Signatures: RSA PKCS #1 v1.5 and PSS with SHA-256/384/512, and ECDSA. SHA-1 is refused | `rsa.c`, `ecdsa.c` |
| CertificateVerify: ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384, rsa_pss_rsae_sha256/384/512, with the scheme matching the leaf key | `handshake.c` |

## What is not checked

- **Revocation.** There is no OCSP or CRL fetching. The short lifetimes of
  public certificates (90 days or less) are the mitigation for now.
- **Certificate Transparency** and certificate policies.
- **IPv6 literal host names.** They do not match and fail closed until IPv6
  lands.

## Fail closed

- An empty trust store trusts nothing.
- An unknown time (`now <= 0`) verifies nothing, because expiry cannot be
  checked without it. A device must learn the time (RTC, then SNTP) before
  it can make authenticated connections; [TIME.md](TIME.md) covers how.
- Anything not understood is a failure, not a skip.

## The root store

`trust_roots.h` holds 33 roots taken from the Mozilla CA program bundle. It
covers Let's Encrypt (ISRG X1/X2), Google Trust Services, GlobalSign,
DigiCert, Amazon, Sectigo/USERTrust, GoDaddy/Starfield, Microsoft, SSL.com,
Certum and QuoVadis. Each entry carries its SHA-256 fingerprint for
comparison against [CCADB](https://ccadb.org). To regenerate it:

```
python3 kernel/src/tls/gen_trust_roots.py "$(python3 -c 'import certifi;print(certifi.where())')" \
  > kernel/src/tls/trust_roots.h
```

Owners can add their own roots at run time with `tls_trust_add_der()`. Adding
a self-signed device certificate pins that device.

## The server side

zxvd's IRC gateway ([IRC_GATEWAY.md](IRC_GATEWAY.md)) needed the other end
of the connection, built from the same code:

- `ecdsa_sign.c`: P-256 signing with RFC 6979 deterministic nonces (HMAC-
  SHA256 from `hkdf.c`), a constant-time double-and-add-always ladder over
  the verifier's complete addition formulas (`ecdsa_int.h`), and low-s is not
  forced (TLS does not ask for it). Secrets are wiped.
- `selfsigned.c`: a self-signed P-256 certificate for localhost, 127.0.0.1
  and ::1 (serverAuth, not a CA), and its PEM form for clients to pin.
- `server.c`: a TLS 1.3 server (rules V1-V5 in `server.h`): one suite
  (ChaCha20-Poly1305), one group (x25519, no HelloRetryRequest), one
  signature scheme (ecdsa_secp256r1_sha256), no client certificates, no
  resumption, no 0-RTT; KeyUpdate and alerts are handled. Anything else in a
  ClientHello is refused with the alert RFC 8446 names.

A client trusts the gateway by pinning its certificate, exactly as above
with `tls_trust_add_der()`, or with its SHA-256 fingerprint.

## Tests

`make -C kernel test-tls-verify` (part of `verify-all`) runs:

- `test_tls_sig`: SHA-384/512 against FIPS 180-4, and ECDSA P-256/P-384,
  RSA-PSS and PKCS #1 v1.5 against 1,500 Wycheproof vectors. The vectors
  cover malformed DER, out-of-range r and s, edge-case points, padding
  forgeries and hash truncation.
- `test_trust`: real chains for www.mozilla.org (Let's Encrypt RSA),
  valid-isrgrootx2.letsencrypt.org (Let's Encrypt ECDSA), www.google.com and
  github.com against the compiled-in roots. It also runs a real
  precertificate, and known-bad chains made by OpenSSL
  (`gen_test_chains.sh`) for every refusal reason. A second build runs the
  same test under ASan/UBSan.
- `test_handshake`: an in-test server presents an OpenSSL chain and signs
  CertificateVerify with RSA-PSS. The client must reach "authenticated". A
  flipped signature bit, a forbidden scheme, a wrong host, an expired chain,
  an unknown root, a missing intermediate and an unknown time must each be
  refused.
- `test_ecdsa_sign`: the RFC 6979 A.2.5 vectors (key, public point, both
  messages), round trips through the verifier and the refusals.
- `test_tls_server` (ASan/UBSan): the repo's client against the server with
  the self-signed certificate pinned and `TLS_VERIFY_REQUIRED`, application
  data both ways, KeyUpdate, and a hand-built ClientHello for every refusal.
  OpenSSL against the server runs in `kernel/arch/hosted/test_zxvd_irc.py`.
- A freestanding build of every file for aarch64, i386, armv7a and rv32, to
  check that no libc or 64-bit division helper is pulled in.

The fuzz harness `fuzz/parsers/fuzz_x509.c` covers the parser and the chain
validator.
