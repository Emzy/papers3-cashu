# Cashu for M5Stack PaperS3

An experimental native port of [Nucula](https://github.com/zeugmaster/nucula) to the [M5Stack PaperS3](https://docs.m5stack.com/en/core/PaperS3). Wallet cryptography, proofs, and mint requests run on the ESP32-S3. A computer supplies pasted input over USB; it is not a wallet server.

**Status: cross-compiled successfully; host storage tests pass; not flashed or tested on a physical PaperS3. Use a development mint with valueless test ecash. This is not a production wallet.** Network transaction recovery and physical device validation remain unfinished; see the limitations below.

## Included

- Native 960×540 landscape e-paper UI, GT911 touch controls, 16MB flash and octal PSRAM configuration.
- Up to three mints, with selected-mint balances in sats.
- Touch amount entry, Lightning deposit invoice QR, persisted invoice and a Check payment button.
- USB import of Cashu v3/v4 tokens; redemption happens directly with the mint over Wi-Fi.
- USB BOLT11 payment input with amount, routing reserve and mint shown for physical confirmation.
- Send all sat proofs from the selected mint as a Cashu v4 token. The saved outbox survives reboot; it is debited before being displayed. Arbitrary-amount ecash sends are not implemented.
- Saved Wi-Fi setup over USB. No credentials are included in the firmware.
- CA/hostname and certificate-date validation, HTTPS-only mint requests, and disabled HTTP redirects.
- No automatic erasure of wallet NVS after storage errors. Boot stops if wallet self-tests or outbox recovery fail.

No camera, NFC, external keypad, LAN control service, SD backup, or automatic sleep is implemented. Original reference-board display/keypad/NFC drivers remain in the source for provenance but are not initialized or linked into the application logic.

## Install the prebuilt firmware

The `firmware/` directory contains the bootloader, partition table and application. These are separate images so an update need not overwrite wallet storage. Use ESP-IDF 5.5.1's esptool 4.12 or a compatible esptool installation.

1. Back up the device's existing firmware/data if needed. Installation replaces its current application and partition table.
2. Connect the PaperS3 to USB. Hold its power button until the rear status light flashes red to enter download mode.
3. Substitute the actual serial port for `PORT` below. On macOS it is usually `/dev/cu.usbmodem…`; on Linux, `/dev/ttyACM…`.

From this project directory, in an activated ESP-IDF shell:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 firmware/bootloader.bin \
  0x8000 firmware/partition-table.bin \
  0x100000 firmware/papers3_cashu.bin
```

When changing from factory firmware, the previous NVS contents may be incompatible. If the screen reports a storage error, **only on first installation to a device without wallet funds or needed data**, initialize this port's NVS region:

```sh
python -m esptool --chip esp32s3 --port PORT erase_region 0x9000 0xF6000
```

That command permanently deletes everything in the specified region. Do not use it as an upgrade or recovery step for a funded wallet. The firmware deliberately does not run it automatically.

For subsequent updates of this port with the same partition layout, flash only the application:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 \
  write_flash 0x100000 firmware/papers3_cashu.bin
```

## First setup

Open a serial terminal on the USB serial port at 115200 baud. With a local source build, `idf.py -p PORT monitor` works; use Ctrl-] to exit. A generic serial terminal also works. Boot may take a few seconds while Wi-Fi initialization and self-tests run.

Enter these commands in order, substituting your own network and **development mint**:

```text
seed generate
wifi Your network name|your Wi-Fi password
mint add https://your-development-mint.example
status
selftest
```

Write down the generated seed before proceeding. Seed generation/restoration is only allowed before any mint has been configured and before a seed exists. The current port does not implement a complete seed-based recovery workflow.

The Wi-Fi command splits on the first `|`, so spaces in SSIDs/passwords work; SSIDs containing `|` are unsupported. A blank password selects an open network. Credentials are stored in flash. USB input is echoed; do not share terminal logs containing passwords, seeds or tokens. Allow network time synchronization before the first mint operation; UDP access to `pool.ntp.org` is needed by this build.

## Use

**Receive Lightning:** choose the mint, tap Receive, type an amount and tap Invoice. Scan its QR with a Lightning wallet. After payment, tap Check payment. A pending invoice survives reboot and is reopened from Receive. `invoice show` prints the full saved invoice and quote over USB. `invoice discard` explicitly forgets a saved invoice; use it only for an unpaid/unneeded invoice.

**Receive ecash:** paste `receive cashuB…` or `receive cashuA…` over USB. The device must be online to redeem it. A token may add its HTTPS mint if a slot is available; inspect its mint before importing.

**Pay Lightning:** paste `melt lnbc…` over USB. With multiple mints, append `w=0`, `w=1` or `w=2`. The console requests a quote; the device displays the amount and routing reserve. Tap Pay now to spend or Cancel to abandon the quote. Mint input fees may also apply. A PENDING result is not reported as payment completion.

**Send ecash:** tap Send ecash and review the amount and mint, then Create token. This exports **all sat proofs from that mint**, retaining any other units. The recipient scans the QR. If the token exceeds the readable QR limit, the screen says so; use `outbox` over USB to retrieve the full token. Only one saved outgoing token is supported. Its receiver may pay mint redemption fees.

**Keep the saved token until it is redeemed or backed up.** The wallet subtracts exported proofs from the balance before showing the token. Removing the saved copy does not refund them. Home / hide QR clears the visible token, but an e-paper image remains visible if power is cut while its QR is displayed.

Useful commands: `help`, `balance`, `mint list`, `mint info`, `invoice 100 w=0`, `claim QUOTE_ID w=0`, `outbox`, `heap`, `tasks`, `selftest`, `reboot`. Mint removal, destructive seed replacement, and the original unsaved `stickup` drain are disabled. Touch screens and Lightning confirmation support sats/bolt11 only; the inherited core has broader unit support.

## Build from source

Install and activate **ESP-IDF v5.5.1** with its ESP32-S3 tools, then run:

```sh
IDF_COMPONENT_MANAGER=0 idf.py build
IDF_COMPONENT_MANAGER=0 idf.py -p PORT flash monitor
```

All application dependencies are vendored and pinned in [DEPENDENCIES.md](DEPENDENCIES.md). No Arduino installation or separate EPDIY checkout is required for these pinned native M5 drivers. `sdkconfig.defaults` selects `esp32s3`, 16MB flash, octal PSRAM, USB Serial/JTAG, the certificate bundle and boot self-tests. Do not substitute the original M5Paper/ESP32 board target.

The NVS region is `0x9000..0xFEFFF`; the application begins at `0x100000` and has a 6MiB partition. There is no OTA partition or firmware-update UI. Preserve these offsets across updates.

Host storage tests require a C/C++17 compiler:

```sh
bash tests/run.sh
```

These compile the real `papers3_storage.cpp` against simulated NVS and wallet/token boundaries. They test commit-before-debit ordering, power loss between commits, replay, failed writes/debits, overwrite prevention, invoice round trips and corrupt-record rejection. They do **not** validate the Cashu protocol, real flash behavior, the display, or the network stack. Crypto/keyset/codec/wallet self-tests are compiled into the firmware and run on boot; they have not been executed on hardware here.

## Remaining work before real funds

- **Power loss during mint/swap/melt:** upstream does not provide complete durable journaling and recovery of blinded outputs and uncertain HTTP results. A lost response or reboot during a mint operation can strand funds. The new outbox protects local export/debit ordering only. Do not infer transaction-wide recovery from it.
- **Pending Lightning payments:** the inherited melt flow removes submitted proofs after PENDING, but does not provide a complete persistent pending-payment/change-recovery workflow. Do not retry an uncertain payment blindly.
- **Storage/security:** proofs, seeds and credentials are in ordinary unencrypted flash. There is no PIN, secure-element integration, flash-encryption setup, secure-boot setup or tamper resistance. USB exposes development commands, including seed viewing. This is not a hardened hardware wallet.
- **Recovery:** deterministic secrets are inherited, but a complete NUT-09 recovery interface is absent. A seed alone is not a demonstrated backup for this firmware; preserve the device's data and exported bearer tokens.
- **Physical validation:** display orientation, QR scans, touch hitboxes, PSRAM load, HTTPS handshakes, reconnection, battery use and power-cut tests still need the actual device. No PaperS3 was attached during development.
- **UX:** arbitrary-amount ecash sends, scanned input, phone provisioning, transaction history and low-power operation remain to be implemented.
- **Trust:** Cashu balances are claims on the selected mint. The touchscreen port does not remove mint custody or redemption risk.

See [HARDWARE-TEST.md](HARDWARE-TEST.md) for a test sequence and [VALIDATION.md](VALIDATION.md) for exactly what was checked locally. Original upstream documentation is retained as [README.upstream.md](README.upstream.md).
