# Two-node demo: one laptop asks, another answers and gets paid

Two ZXV nodes on the same network. Node A sends a prompt to node B, B answers
with its model, and A pays B for the answer plus the 0.08889% assurance fee.
Everything travels in a post-quantum session, and both nodes end up holding
the same signed receipt.

## Run it

You need Python 3.8 or later and a C compiler (on a Mac: `xcode-select
--install`). The script builds `zxvd` once into `~/.zxv-demo/build`, or uses
the one you name with `--zxvd PATH`.

**On one computer** (both nodes, on 127.0.0.2 and 127.0.0.3):

```sh
python3 tools/zxv_two_node_demo.py local
```

A Mac has only 127.0.0.1 on loopback until you add two more addresses (once
per boot): `sudo ifconfig lo0 alias 127.0.0.2 up` and
`sudo ifconfig lo0 alias 127.0.0.3 up`. Linux needs nothing.

**On two laptops** on the same Wi-Fi or wired LAN:

```sh
# laptop B, the one that answers (add --model FILE.gguf for real answers)
python3 tools/zxv_two_node_demo.py serve

# laptop A, the one that asks: use the address B printed
python3 tools/zxv_two_node_demo.py ask 192.168.1.23
```

B listens on UDP and TCP port 8723. If A reports that B did not answer, let
`zxvd` accept incoming connections in B's firewall (macOS: System Settings >
Network > Firewall). Options: `--price` (B's price), `--max-price` and
`--prompt` (A's side), `--float` (A's demo wallet), `--port`, `--bind`.

## What happens

1. **Discovery (UDP).** A pings B's address. Every Vinea message is signed
   with ML-DSA-87 and B's NodeID is the SHA3-256 of its public key, so A
   learns a NodeID it can check, not just an address.
2. **Session (TCP).** A opens a TCP connection to the same port and runs the
   Vinea handshake (`kernel/src/vinea/vna_session.h`): X25519 and
   ML-KEM-1024 key exchange combined, ML-DSA-87 signatures on both sides. A
   server whose key does not hash to the NodeID A expected fails here. All
   later messages are ChaCha20-Poly1305 records with a replay window.
3. **Request and answer.** A sends the prompt and the most it will pay. B
   refuses if that is below its price; otherwise it answers and signs an
   invoice binding the session, both NodeIDs, the hashes of prompt and
   answer, the price and the time.
4. **Payment.** A checks B's invoice signature, then posts the payment on
   its peer book: price to the "mesh counterparties" account plus
   fee = floor(price × 8889 / 10,000,000), split exactly into the four
   buckets (reserve floor 50% plus the remainder, V-Bill dividend pool 25%,
   node bounties 15%, regenerative capital 10%). A signs the settlement and
   sends it.
5. **Acknowledgement.** B checks the price, the fee (it may be one unit
   higher than the plain formula because A's sub-unit carry from earlier
   payments is applied), the split and A's signature, credits the price to
   its wallet, and signs an acknowledgement.

With the default price of 1,250.00 VFV the fee is 1.11 VFV (0.57 + 0.27 +
0.16 + 0.11), so A pays 1,251.11 VFV and B receives 1,250.00 VFV.

## From zxvd's socket

The same flow is three JSON-RPC methods (`docs/ZXVD.md`):

```sh
zxvd --rpc ~/.zxv/run/zxvd.sock --net lan --peer-serve 125000      # node B
zxvd --rpc ~/.zxv/run/zxvd.sock --net lan --peer-float 1000000     # node A
```

```json
{"jsonrpc":"2.0","id":1,"method":"peer.ask","params":{"peer":"192.168.1.23","prompt":"hello","max_price_minor":200000}}
```

`peer.serve {"price_minor": N}` starts or reprices serving at run time and
`peer.serve {}` stops it; `peer.status` shows the wallet, the fee buckets
and the last 16 settlements.

## Honest limits

- **Demo money.** VFV in the peer wallet comes from `--peer-float`, issued
  by the node's own book. It is not backed by anything and the peer book is
  separate from the owner's libzxv books and reset at every start.
- **Who holds the fee.** The payer's book holds the four fee buckets. There
  is no network-wide treasury that collects them yet.
- **One request at a time.** The daemon's loop waits while a request runs
  (up to 10 s per message, 180 s for the answer).
- **Disputes are evidence, not enforcement.** A posts before it sends the
  payment. If B then refuses, A keeps B's signed refusal and its own signed
  payment; nothing is reversed automatically.
- IPv4 only, same LAN (or `--net online` and an address both can reach); no
  NAT traversal. The TCP stream carries Vinea session records; it is not
  UBH-168 encoded on the wire (UBH-168 is advertised as a session feature).
- In CI the two nodes are two loopback addresses on one machine, which tests
  real sockets and real processes but not a real network.
