# Bearers: the links your devices talk over

Roadmap steps R2.2 to R2.6 (edge and mesh transport).

A **bearer** is one kind of link that can carry bytes from one of your devices to another: your
home network, Bluetooth LE, Wi-Fi Direct, a LoRa radio, a serial cable, a dial-up or radio
modem, your phone's mobile data, or amateur (ham) radio through a software-defined radio.

Your devices already form a private mesh (`kernel/src/devmesh`). The mesh encrypts and signs
everything itself and only needs "something that delivers frames". The bearer layer is that
something: it picks the best link for each message, refuses links that must never carry that
message, and switches to another link when one fails. The mesh code was not changed to do
this; it plugs in through its normal transport hook (see "How it plugs into the mesh").

The code is in `kernel/src/bearer/`, the computer-side serial glue is in
`kernel/arch/hosted/bearer_host.c`, and the tests run with:

    cd kernel && make -f Makefile -f mk/bearer.mk test-bearer

At the end of this page is exactly what was tested and what was not.

## What each bearer is for

| Bearer | Good for | Default size limits | Metered | Notes |
|---|---|---|---|---|
| LAN | everything | 1 MiB message, 1200-byte frames | no | Same network as the apps' existing UDP transport |
| Wi-Fi Direct | everything, phone to phone with no router | 1 MiB | no | Android only (iOS has no Wi-Fi Direct API; see below) |
| Bluetooth LE | everything, slowly; pairing nearby | 128 KiB message, 20..512-byte pieces | no | Radio belongs to the phone's OS |
| Serial | everything, over a cable or a radio modem | 1 MiB | no | COBS + CRC-32 frames |
| Modem | everything, over a dial-up or AT-command data link | 1 MiB | **yes** | Hayes AT driver |
| Cellular | everything | 1 MiB | **yes** | The phone's normal mobile data; no baseband code |
| LoRa | settlement records, small signed records, emergency messages | 6 KiB message, 255-byte packets | no, but air time is rationed by law | Never AI traffic |
| Ham radio (SDR) | signed, readable emergency text only | 8 KiB text, 256-byte AX.25 frames | no | Off unless a licensed operator turns it on |

Every message is given a **class** by the code that sends it:

- **AI** — questions to an assistant and the answers: the bulky traffic.
- **Settlement** — ledger and settlement records.
- **Chat** — messages between people.
- **Emergency** — emergency messages.
- **Record** — small signed records (roster changes, receipts, acknowledgements).

## How a link is chosen

Each link keeps two running measurements, updated every time the device hears back (or does
not): the round-trip time and the share of messages lost. Both are smoothed averages done in
whole numbers (new round-trip time counts 1/8, new loss sample counts 1/16), the same method
TCP uses for its timers.

For a message of a given size and class, each allowed link gets a score: the time it would take
(round-trip time plus the time to send the bytes at that link's speed, stretched by the expected
retries for its loss), plus a penalty for loss, plus the money it would cost. The weights depend
on the class:

| Class | Time | Loss | Cost | Effect |
|---|---|---|---|---|
| AI | 1 | 0 | 100 | Strongly avoids metered links: a slow home network beats fast mobile data |
| Settlement | 1 | 4 | 1 | Prefers the most reliable link |
| Chat | 2 | 1 | 10 | Prefers the fastest link |
| Emergency | 1 | 8 | 0 | Ignores cost entirely: whatever gets through |
| Record | 1 | 2 | 10 | In between |

The lowest score wins. If two links score exactly the same, the order is fixed: LAN, Wi-Fi
Direct, serial, Bluetooth LE, cellular, modem, LoRa, ham radio, then the lower link number. So
the same situation always gives the same choice.

**Failover.** If the chosen link reports an error, it is marked down and the next best link is
tried straight away. A LoRa radio that is only waiting out its legal air-time limit says "busy"
instead; it is skipped for this message but stays up. A link that went down stays down until the
app says it is back (for example when Bluetooth reconnects).

The app can lower a link's numbers to match reality (the Bluetooth packet size it negotiated, the
real speed of a serial modem, its mobile data tariff). It can never loosen a link's rules.

## The rules some bearers must never break

These checks fail closed: anything unknown, unclassified or doubtful is refused.

### Ham radio: readable, signed emergency messages only

Amateur radio is not for business or for secrets. The ham bearer is designed to follow the US
rules in 47 CFR 97.113 (no messages for money or business) and 97.113(a)(4) / 97.309 (no codes
or ciphers meant to hide the meaning). So it only carries:

- the **Emergency** class — never AI traffic, settlement, payments or chat;
- **plain printable text** — no encrypted or binary data, and no session encryption of any kind;
- with a **signature** — the station's post-quantum signature is added at the end in the clear
  (as base64). A signature proves who sent the text and that nobody changed it, but it hides
  nothing: anyone can read the message.

On air the message goes as AX.25 "UI" frames, the standard packet-radio format. The signed text
is tied to the sender's call sign, so a signature cannot be reused under another call sign, and
the receiver checks the signature before showing the message.

**It is off by default.** Transmitting needs a licence. The app must ask the user for their own
call sign and for confirmation that they hold an amateur licence before it turns the switch on
(`licensed_operator` in `br_ham.h`). Receiving needs no switch. Rules differ by country; an app
must not offer the switch where local rules forbid this traffic.

The code cannot read meaning: a licensed user could still type a business message as plain text.
The rules put that responsibility on the licensed operator, and so does this design.

A post-quantum signature is large. An ML-DSA-87 signature is about 4.6 KB, which becomes about
32 frames and close to a minute of air time at 1200 bit/s. That is the price of a signature that
will still hold against quantum computers.

### LoRa: small signed records, never AI traffic

LoRa travels kilometres on very little power, but slowly: a few hundred to a few thousand bits
per second, and the law limits how much of each hour you may transmit. It carries settlement
records, small signed records and emergency messages, each of which must be signed or
authenticated. It never carries AI traffic or chat, and nothing over 6 KiB.

The time each packet spends on air is computed exactly with Semtech's published formula (in
whole microseconds, no floating point), and the regional limits are tracked:

- **Europe (EU868)**: each sub-band has a duty-cycle limit (1%, 0.1% or 10%). After sending for
  *T* milliseconds the sub-band stays closed until *T* × 100 milliseconds (for 1%) after the
  packet started. This is LoRaWAN's own method; over a fixed clock hour it can go over by at most
  the one packet that started just before the hour ended.
- **US (US915)**: one transmission may last at most 400 ms, and one channel may be used for at
  most 400 ms in any 20 seconds. At the slowest US setting this leaves 24 bytes per packet, which
  matches the LoRaWAN regional parameters. If the device cannot prove it is inside the limit
  (its short history is full), it waits rather than guessing.
- **No region set**: nothing is transmitted.

A 6 KB record under a 1% duty cycle takes minutes. That is the physics and the law, not a bug.

### Everything else

LAN, Wi-Fi Direct, Bluetooth, serial, modem and cellular carry every class, encrypted, up to
their size limit. Modem and cellular are marked metered, so AI traffic avoids them when anything
else is up.

## How it plugs into the mesh

The device mesh hands every outgoing frame to one function the app provides
(`dm_host_t.send`). `br_dm_init()` (in `kernel/src/bearer/br_devmesh.h`) takes the app's own
callbacks and returns a set in which `send` goes through the bearer layer and every other
callback goes straight back to the app. Nothing in `kernel/src/devmesh` was changed.

The adapter decides each frame's class: the mesh's handshake frames are records (and are too
big for LoRa, so they never go there); ordinary mesh records get the class the app sets just
before sending (`br_dm_set_class()`, for one send), and are treated as **AI**, the most
restricted class, when the app says nothing. So a mesh frame of unknown purpose never goes over
LoRa, and no mesh frame ever goes over ham radio (they are encrypted).

On the receiving side each link undoes its own framing (Bluetooth pieces, serial frames, the
length prefix) and passes the frame to `dm_receive()`, as the apps already do for LAN.

## What needs real hardware

| Piece | What exists | What a device must provide |
|---|---|---|
| Bluetooth LE | Splitting into ATT-sized pieces (ATT_MTU 23..517, payload MTU − 3) and joining them back; a length-prefixed stream for L2CAP channels | The radio, from the phone's OS |
| Wi-Fi Direct | The same length-prefixed stream, over a TCP socket in the group | The group, from Android |
| LoRa | Time on air, duty cycle and dwell time, paced fragment sending | A radio module (SX127x / SX126x) and its driver |
| Serial / modem | Frames, the AT command driver, raw-mode serial port glue for macOS/Linux | A cable, USB adapter or modem |
| Cellular | A bearer marked metered | Nothing new: it is the phone's normal data connection |
| Ham radio | AX.25 frames with their checksum, optional bit stuffing, the signed-message format | A licensed operator, and a KISS TNC or an SDR modem |

### Hook points for the mobile apps (not yet wired)

The apps were not changed. This is where the bearers attach when they are:

- **Both apps** already have the right shape: `mobile/core/zxv_mobile.h` takes a `zxv_host_t`
  whose `send` delivers a frame and gives received frames to `zxv_receive()`. The existing
  `LanTransport` (Kotlin and Swift) is one transport; Bluetooth and Wi-Fi Direct become two more
  next to it, with the bearer layer choosing between them.
- **Android** (`mobile/android/app/src/main/java/org/zedec/zxv/core/`, next to
  `LanTransport.kt`): Bluetooth LE through `BluetoothGatt` / `BluetoothGattServer` (one write and
  notify characteristic, values cut by `br_frag` at the negotiated `onMtuChanged` size) or an
  L2CAP channel (`BluetoothDevice.createL2capChannel`, Android 10+, with `br_lps`); Wi-Fi Direct
  through `WifiP2pManager` and a TCP socket with `br_lps`. The manifest will need
  `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE` and `NEARBY_WIFI_DEVICES` (Android
  12/13+); today it only has network permissions.
- **iOS** (`mobile/ios/Sources/ZXVKit/`, next to `LanTransport.swift`): Bluetooth LE through
  CoreBluetooth (`CBPeripheralManager` / `CBCentralManager`, `maximumWriteValueLength` for the
  piece size) or `CBL2CAPChannel` with `br_lps`. iOS has **no Wi-Fi Direct API**; the nearest
  thing is peer-to-peer Wi-Fi (AWDL) through Network.framework (`includePeerToPeer`) or
  MultipeerConnectivity, which would be another LAN-like bearer. Info.plist will need
  `NSBluetoothAlwaysUsageDescription`.
- **Mobile data**: nothing to write. When the OS reports the active network is cellular (Android
  `NetworkCapabilities.TRANSPORT_CELLULAR`, iOS `NWPath.usesInterfaceType(.cellular)`), the app
  registers its normal socket transport as a CELLULAR link instead of LAN, so it is treated as
  metered.

### Why there is no cellular baseband code

The baseband is the separate processor inside a phone or modem that runs the radio itself. Its
software is closed, signed by the chipset maker, certified with each mobile carrier, and cannot
legally or practically be replaced by an app. Using the phone's or modem's normal data
connection gives the same reach with none of that. Real baseband work would need partnerships
with carriers and chipset makers.

## Exactly what was tested, and what was not

Tested (all in `make -f mk/bearer.mk test-bearer`, under AddressSanitizer and UBSan):

- **Selection and gates** (`test_bearer.c`, 143 checks): every policy gate and every refusal,
  exact smoothed measurements and scores, class weights, the fixed tie-break order, failover,
  the busy case, and that tuning a link cannot loosen its rules.
- **LoRa** (`test_br_lora.c`, 98 checks): published air-time values (SF7 at 125 kHz: 10 bytes
  41.216 ms, 13 bytes 46.336 ms, 64 bytes 118.016 ms; SF12, 10 bytes 991.232 ms); the integer
  result equals the floating-point formula for 80,496 parameter combinations; the US dwell-time
  payload limits equal the LoRaWAN regional parameters for DR0 to DR3 (24, 66, 138 and 255
  bytes); EU sub-band limits over a simulated hour; US 400 ms per 20 s; a 600-byte message sent
  as paced fragments and put back together.
- **Serial frames** (`test_br_serial.c`, 37 checks): all eleven published COBS examples both
  ways, CRC-32 check values, exact frame bytes, and recovery after garbage, corruption,
  joining mid-frame and over-long frames.
- **Modem** (`test_br_modem.c`, 61 checks): a scripted fake modem with echo, every dial result,
  the "+++" guard time on both sides, hang-up, carrier loss, incoming calls, and every timeout.
- **Bluetooth framing** (`test_br_ble.c`, 57 checks): ATT payload sizes, exact piece bytes,
  messages up to 128 KiB at every MTU, and every way reassembly refuses bad input.
- **AX.25** (`test_br_ax25.c`, 59 checks): the address bytes of the AX.25 v2.2 specification's
  Figure 3.4 example (the specification itself misprints 'N' as 0x98; the correct value 0x9C is
  checked), the CRC-16/X.25 check value and good-frame residue from RFC 1662, every single-bit
  error caught, and bit stuffing.
- **Ham messages** (`test_br_ham.c`, 45 checks): real ML-DSA-65 and ML-DSA-87 signatures from
  the existing post-quantum code; every gate; tampered text, replay under another call sign,
  missing frames and malformed frames are all refused.
- **The mesh over bearers** (`test_br_devmesh.c`, 40 checks): two devices pair with the real
  post-quantum handshake over Bluetooth-sized 20-byte pieces, fail over to a serial link when
  Bluetooth breaks, send a settlement record over LoRa, and have an unclassified reply refused
  there.
- **Serial port glue** (`test_bearer_host.c`, 34 checks): a real pseudo-terminal pair; raw mode
  passes all 256 byte values unchanged with no echo; frames and the modem driver work across it.
- The library builds freestanding for aarch64 and i386 with no C library and no 64-bit
  division helpers.

Not tested:

- No real radio of any kind: no Bluetooth, Wi-Fi Direct, LoRa or ham transmission has been made.
- No real serial cable, USB adapter or modem; DTR and carrier-detect lines do nothing on a
  pseudo-terminal.
- The mobile apps do not use any of this yet.
- The regulatory behaviour is designed to follow the cited rules; it has not been reviewed by a
  lawyer or a regulator, and it is not legal advice.
- Windows serial ports are not covered by `bearer_host.c` (it uses POSIX termios).
