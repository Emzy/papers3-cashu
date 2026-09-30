# Validation report — 0.2.0-papers3 — 2026-09-30

- ESP-IDF 5.5.1 cross-build for ESP32-S3: **PASS**, no compiler warnings or errors in the update build.
- Application image: **1,300,960 bytes** (1.241 MiB); 6MiB application partition, approximately 79% free.
- Host storage fault-injection tests: **PASS**. Real port storage module with simulated NVS and wallet/token boundaries.
- Selection tests: **PASS**. No default selection, same-denomination proofs, toggling, stale/spent/modified proof rejection, live proof reordering/additions, duplicate rejection, and 64-bit totals.
- QR page tests: **PASS**. Each page contains one original proof; sums, mint, unit, memo, DLEQ and witness are preserved. Out-of-range pages are rejected.
- QR geometry and C/C++ interface tests: **PASS**. Minimum five-pixel modules, four-module quiet zone, bounded payloads, and standard finder patterns checked using the actual encoder.
- Independent raster QR decoding with jsQR 1.4.0: **PASS**. The complete 364-character nonspendable token fixture decoded exactly (version 12, 65×65 modules, 7 pixels/module in the 520×520 image). A separate version-1 HELLO fixture also decoded exactly.
- The macOS Vision/Core Image attempts could not decode even the short control fixture in this execution environment; jsQR supplied the independent decoder instead.
- Configuration: 16MB flash, octal PSRAM, USB Serial/JTAG, CA bundle, certificate-date validation and boot self-tests. Partition offsets and NVS schema are unchanged from 0.1.
- No credentials, seed or funded proofs are embedded. The QR test fixture is explicitly nonspendable.
- Physical PaperS3 validation: **not performed by the agent**. The USB port was detected during setup assistance, but this update has not been flashed or tested on device here. Physical scan quality still needs confirmation.
- No mint was funded and no financial transaction was submitted during this update.

Reproduce host tests with `bash tests/run.sh`. Optionally render the included QR fixture:

```sh
bash tests/run.sh tests/fixtures/nonspendable-token.txt /tmp/papers3-test-qr.pgm
```

`firmware/manifest.json`, `firmware/SHA256SUMS`, the matching ELF and `firmware/sdkconfig.verified` identify the delivered build. The independent decoder was downloaded into the working directory solely for validation; it is not a firmware dependency.

The QR patch is recorded in DEPENDENCIES.md. Existing limitations around interrupted network-transaction recovery remain; see README.md. UPDATE.md contains the application-only flashing command that leaves the NVS region untouched.
