# ZXV Companion

Your zxvd node on your phone, for iPhone and Android, in one Flutter app. It
shows the same five views as the Mac app ([macos/studio](../../macos/studio)):
Local AI Studio, Sicily Ledger, P2P Mesh, Storefront & Escrow, and Settings.
The phone holds no books and runs no model; it asks the zxvd on your Mac over
zxvd's encrypted phone link ([docs/ZXVD.md](../../docs/ZXVD.md#the-phone-link)).

This app is separate from the native `mobile/android` and `mobile/ios`
devmesh apps.

## Get it on a phone

There is no Apple Developer ID and no Play Store listing yet, so these are
sideload builds. CI (`.github/workflows/companion.yml`) builds both on every
pull request that touches the app; download them from the run's artifacts.

- **Android**: `zxv-companion-android-debug` holds `zxv-companion-debug.apk`,
  signed with the debug key. Copy it to the phone and open it (allow installs
  from that source when asked), or `adb install zxv-companion-debug.apk`.
- **iPhone**: `zxv-companion-ios-unsigned` holds an unsigned
  `zxv-companion-unsigned.ipa`. Sign it with your own Apple ID to install it:
  open `ios/Runner.xcworkspace` in Xcode, pick your Personal Team under
  Signing & Capabilities and run on the phone, or load the IPA into a
  sideloading tool such as Sideloadly or AltStore. With a free Apple ID the
  app must be re-signed every 7 days.

## Pair it with your Mac

1. In ZXV Studio on the Mac, open Settings and turn on **Let paired phones in on
   this network**.
2. Type a name for the phone, choose whether to **Allow money and network
   changes** (post ledger entries, open accounts, enable currencies, send
   network commands and paid peer questions), and choose **Pair a phone**.
3. On the phone, choose **Scan the code** and point it at the QR code, or
   paste the pairing link.

The phone and the Mac must be on the same private network (Wi-Fi, a
hotspot, or a VPN with a private address). If the Mac's address changes,
use **Mac moved? Change address** in the phone's Settings. To unpair, choose
**Revoke** next to the phone in ZXV Studio; that also cuts its connection.

## Security

- The link is off until you turn it on, and listens only on private,
  loopback and link-local IPv4 addresses.
- Each pairing has its own 32-byte key, shown once in the QR code. The phone
  keeps it in the Keychain or Android Keystore-backed storage; zxvd keeps it
  in a file only you can read. It never travels over the network.
- Every connection does a fresh X25519 exchange mixed with that key (a
  Noise NNpsk0-style handshake, so a recorded session cannot be decrypted
  later even with the key), and the Mac proves it knows the key before the
  phone sends anything. Every frame is ChaCha20-Poly1305 with a counter
  nonce, so a changed, replayed or reordered frame closes the connection.
- A phone can never stop zxvd, sell answers to peers, or pair or revoke
  phones. A phone paired without changes allowed is read-only.
- Limits: each connection starts with the pairing's id in the clear, so
  someone on your network can tell which paired phone is connecting (not
  what it asks). A stolen, unlocked phone has whatever its pairing allows
  until you revoke it.

## Develop

Flutter 3.47.5 (Dart 3.13).

```sh
kernel/arch/hosted/build_zxvd.sh /tmp/zxvd
cd mobile/companion
flutter pub get
flutter analyze
ZXVD_BIN=/tmp/zxvd/zxvd flutter test     # the live test pairs with a real zxvd
flutter run                              # on a connected phone or simulator
```

| Part | What it is |
|---|---|
| `lib/src/link.dart` | The zxv-phone/1 client: handshake, encrypted frames, JSON-RPC calls. |
| `lib/src/pairing.dart` | Parsing the pairing link, and keeping it in secure storage. |
| `lib/src/store.dart` | The app's state, polling and every RPC call the views make. |
| `lib/src/money.dart` | Exact minor-unit amounts (BigInt, never floating point). |
| `lib/src/net_command.dart` | Builds the `net.command` lines the Mesh and Storefront views send. |
| `lib/src/ui/` | The five views and the pairing screen. |

The app does not link libzxv or the Dart SDK in `sdk/mobile/dart`: zxvd does
all the book math, so the phone needs no native library.
