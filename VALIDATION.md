# Validation report — 2026-09-30

- ESP-IDF 5.5.1 cross-build for ESP32-S3: **PASS**.
- Application image: **1,292,816 bytes** (1.233 MiB); 6MiB application partition, approximately 79% free.
- Host fault-injection tests (`bash tests/run.sh`): **PASS**. Real port storage code; mocked NVS, wallet debit and token codecs. This is not a hardware or Cashu interoperability test.
- Configuration checked: 16MB flash, octal PSRAM, native USB Serial/JTAG, CA bundle, certificate-date validation and boot self-tests enabled.
- Firmware contains empty compile-time SSID/password and no wallet seed or bearer proofs. Provision over USB.
- Compiler/linker reported no warnings or errors on the final incremental build after regenerating configuration from defaults. The initial complete build also compiled all vendored dependencies.
- Connected PaperS3: **none detected**. No flash, boot, touch, QR scan, Wi-Fi, payment, power-loss-on-real-flash, battery or on-device cryptographic self-test has been performed.
- No mint was funded and no financial transaction was submitted during development.

`firmware/manifest.json` and `firmware/SHA256SUMS` identify the delivered images. `firmware/sdkconfig.verified` records the actual build configuration, and the matching ELF is included for serial backtrace decoding.

The SDK/dependency downloads and compilation ran locally. To accommodate this environment's process-enumeration restrictions, the normal ESP-IDF component manager was disabled and all application dependencies were vendored. No SDK source modifications were required.

The outbox journal does not solve interrupted network-transaction recovery. See the explicit remaining-work list in README.md before hardware testing.
