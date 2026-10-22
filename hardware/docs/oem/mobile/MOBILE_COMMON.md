<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Handheld and laptop common architecture

What Signet, Folio and Courier share. Every part line is [CONFIRM]
([README.md](README.md)).

## 1. Split trust

A flagship phone is one chip that does everything: applications, the
cellular modem, the secure enclave, the camera pipeline. Its maker's
signed firmware sits under all of it. A ZXV handheld splits that into
parts that do not have to trust each other:

```
 +-------------------------------------------------------------------------+
 | ARBITER  (the trust chip; the briefcase's arbiter, phone-sized)         |
 |  owns: power rails of every block below, radio kill switches,           |
 |  the seal display and seal button (section 4), the secure element,      |
 |  tamper sensors, cell heater, honeycomb window lighting, port enables   |
 +-------------------------------------------------------------------------+
        | power + reset of each      | I2C/SPI                | GPIO
        v                            v                        v
 +-------------------------+  +----------------------+  +------------------+
 | APPLICATION PROCESSOR   |  | SECURE ELEMENT       |  | SEAL DISPLAY     |
 | Rockchip RK3588S/RK3588 |  | NXP SE051, CC EAL6+  |  | + SEAL BUTTON    |
 | runs ZXV (arm64):       |  | holds the held-device|  | drawn only by    |
 | apps, integer tensor    |  | signing key          |  | the arbiter      |
 | engine, NVMe storage    |  +----------------------+  +------------------+
 +-------------------------+
        | PCIe / USB (IOMMU on)       each link power-gated by the arbiter
        v
 +-----------+ +-----------+ +-----------+ +---------+ +--------+ +------+
 | CELLULAR  | | SATELLITE | | Wi-Fi 7 + | | LoRa +  | | UWB    | | GNSS |
 | M.2 5G    | | IoT-NTN   | | BT 6.0    | | sat L/S | | ranging| |      |
 | (untrusted| | messaging | |           | | band    | |        | |      |
 +-----------+ +-----------+ +-----------+ +---------+ +--------+ +------+
```

- **The arbiter** is a small FPGA SoC (Microchip PolarFire SoC, the
  smallest variant that holds the design; the briefcase and servers use
  the MPFS250T family) [CONFIRM part per device]. It is the root of trust:
  it verifies the signed ZXV image before releasing the processor from
  reset, and it holds every block's power.
- **The application processor is not the root of trust.** It runs ZXV,
  which is trusted code, but it is a large chip with a large attack
  surface; the keys that move money live in the secure element, and the
  approval the user sees is drawn by the arbiter (section 4).
- **The modem is a separate M.2 module** on PCIe or USB with the
  processor's IOMMU on, so it cannot read memory it was not given. The
  arbiter can cut its power with the kill switch, and does when the user
  turns radios off: off means unpowered, not "asked to stop".
- **Hardware kill switches** on every model: cellular and satellite,
  Wi-Fi and Bluetooth, cameras, microphones. They switch power through
  the arbiter, and the arbiter reports their state on the seal display.

## 2. Parts every model uses

| Function | Part | Why |
|---|---|---|
| Application processor | Rockchip RK3588S (Signet), RK3588 (Folio, Courier open board): 4 × Cortex-A76 + 4 × Cortex-A55, 8 nm, up to 32 GB LPDDR4X/5, 6 TOPS NPU, Mali-G610 [CONFIRM] | OEM-fusable secure boot; mainline Linux support (USB, network, display, GPU, video decode upstreamed) to start ZXV's drivers from [CONFIRM] |
| Storage | NVMe M.2 SSD (2230 in Signet and Folio, 2280 in Courier) on PCIe | ZXV's NVMe driver exists and is boot-checked (`kernel/src/hwdrv/nvme.c`); no UFS driver is needed |
| Secure element | NXP SE051, Common Criteria EAL6+ (AVA_VAN.5), 15-year availability [CONFIRM] | Holds the held-device signing key (section 4) |
| Cellular | Quectel RM520N-GL (Qualcomm SDX62, 5G Rel-16, M.2) [CONFIRM] | Mature, carrier-certified, separate module |
| Satellite messaging | Quectel BG95-S5 (Rel-17 IoT-NTN + LTE-M, Skylo-certified) [CONFIRM] | Text and approvals from anywhere with sky; no broadband satellite M.2 module was found shipping |
| Wi-Fi 7 + Bluetooth | MediaTek MT7925 (Filogic 360), mainline Linux driver (mt76) [CONFIRM] | Best-documented Wi-Fi 7 part to write ZXV's driver from |
| Bluetooth 6.0 Channel Sounding | Nordic nRF54L-series companion [CONFIRM] | Distance-bounded proximity: a paired device knows it is really next to you |
| UWB | Qorvo DW3000 (IEEE 802.15.4z, FiRa) [CONFIRM] | Secure ranging for pairing and unlock |
| LoRa + satellite IoT | Semtech LR1121: 150–960 MHz, 2.4 GHz, and L/S-band satellite; +22 dBm sub-GHz [CONFIRM] | Off-grid text and approvals between ZXV devices, kilometres apart, no network |
| GNSS | Multi-band (L1 + L5) receiver with 1PPS | Position and time without a network |
| Battery | Silicon-carbon anode Li-ion (≈ 900 Wh/L class, as in 2026 phones) [CONFIRM] with a thin-film cell heater | Highest density available; the heater makes cold charging safe ([../HARDENING.md](../HARDENING.md) section 5) |

## 3. One system: how work moves between devices

### 3.1 The mesh

Every ZXV device, from a Signet to a Hoard, is a member of its owner's
**device mesh** (`kernel/src/devmesh`; user guide
[docs/MOBILE_AND_DEVICES.md](../../../../docs/MOBILE_AND_DEVICES.md)). Paired
devices hold each other's post-quantum public keys and talk over Vinea
v3 sessions (ML-KEM-1024 + ML-DSA-87). The roles already exist:

| Role (`dm_role_t`) | Devices |
|---|---|
| HOME: always on, runs the large models, holds files | Hearth, Homestead, Guildhall, Strongbox, Count House, Hoard, and a briefcase at its base |
| THIN: a client of the home devices | Signet, Folio, Courier (by default) |
| STANDALONE: works alone | Any device set so by its owner (a Courier off-grid, a briefcase in the field) |
| HELD flag: may approve money | Signet (always); Folio and Courier if the owner allows |

### 3.2 Where a request runs

The owner chooses, per device and per kind of task:

| Setting | Behaviour |
|---|---|
| **Automatic** (default) | The routing ZXV already implements: a home device if it is reachable and can run the model; else this device if a model fits; else bought capacity if online; else "unavailable", said plainly. If the home device stops answering for 20 s, this device takes over and says so |
| **Always here** | Never leaves the device. Slower, fully private, works off-grid |
| **Always home** | Always sent to the owner's servers, even on mobile data |
| **Pinned** | Always a named device (for example: drawing on the Folio, simulations on the Hoard) |

So a request typed on a Signet in a field can run on the owner's
Homestead at the farm, on the Guildhall the village shares, or on the
Signet itself, as the owner configured, and the answer says which.

### 3.3 Which link carries it

The device picks the best link it has, in this order:

| Link | Range | Carries |
|---|---|---|
| Ethernet or USB-C (docked) | cable | everything |
| Wi-Fi 7 (direct or through a router) | building | everything |
| Cellular 5G | anywhere with coverage | everything; metered rules apply |
| Satellite IoT-NTN | anywhere with sky | text, approvals and short results only (bytes, not media) |
| LoRa (LR1121) | kilometres, line of sight; meshes through other ZXV devices | text, approvals, alerts |
| Bluetooth / UWB | metres | pairing, proximity unlock, hand-off |

The links are ZXV's bearers (BLE, LoRa, serial, modem, cellular and amateur
radio, and the device mesh, in `kernel/src/bearer`, arriving with the
retail track). A request that cannot fit the link it has (a large model's
answer over LoRa) waits for a better one, and says so.

### 3.4 Trusting an answer that came from somewhere else

ZXV's tensor engine computes in integers (`kernel/src/tensor/zt.h`): the
same model and the same prompt give **bit-identical** tokens on any ZXV
device, x86 or ARM. That makes something possible that no current phone
can do: **a handheld can check a server's answer.**

- The home device returns the answer with its signature and a digest of
  the run (model hash, prompt, settings).
- The handheld re-runs a sample (for example the first tokens, or a
  random span) with the same small model locally, where it fits, and
  compares bit for bit. A mismatch is a FALSE verdict: the answer is
  refused and the device that sent it is flagged.
- For a model too large to re-run, the check is done by a second home
  device instead (a Strongbox checking a Count House), the same
  reconciliation the coprocessor tiers already use (`kernel/src/cotier`).

State: [R&D]. The determinism is built and tested; the sampling protocol
is not.

## 4. Approvals: the seal

The held device approves payments with a post-quantum signature over the
exact amount, rail, currency, payee, memo and expiry (the rules in
[docs/MOBILE_AND_DEVICES.md](../../../../docs/MOBILE_AND_DEVICES.md)). On a ZXV
handheld that approval does not go through the main screen:

- A **seal display**, a small always-on memory-in-pixel panel on the
  device's side (Signet) or above the keyboard (Courier), is wired to the
  arbiter alone. It shows the amount and payee being signed.
- A physical **seal button** next to it, also wired to the arbiter alone,
  approves. Software on the application processor cannot draw on the seal
  display or press the button.
- The signature is made inside the secure element, released only by the
  arbiter after the button press.

Malware that takes over the main screen can show anything it likes, but
it cannot make the seal display say something other than what is being
signed. This is the briefcase's trusted display path, on a phone.

## 5. Hardening

Every model meets [../HARDENING.md](../HARDENING.md): 1.8 m drops (laptop
1.22 m), IP68 at 2 m and IP69, HEMP E1 at 50 kV/m (MIL-STD-461G RS105),
the surge levels of its ports, −30 to +60 °C operating (laptop +55 °C),
salt fog and humidity. Each carries the **honeycomb window**
([../HARDENING.md](../HARDENING.md) section 6): a titanium hexagonal grille over
sapphire, with the bonded copper micro-mesh that closes the Faraday cage,
showing the arbiter's status lighting and the board beneath, with the
backlit ILLUMAHEART FOUNDATION emblem at its centre (the standard design,
[../HARDENING.md](../HARDENING.md) section 6.5). The display
carries the finer shield mesh of section 6.3. Ports follow
[../PORTS.md](../PORTS.md); the handhelds' full port array is on their sleds.
