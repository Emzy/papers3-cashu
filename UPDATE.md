# Update an existing PaperS3 Cashu installation

Version **0.2.0-papers3** adds token selection and larger QR codes. This command updates only the application at `0x100000`; it does not erase or write the wallet's NVS region. No seed regeneration or storage initialization is needed.

On this Mac, copy the entire block below. It uses the Python environment that already has esptool installed. If the USB port has changed, substitute its current name (`ls /dev/cu.usbmodem*`).

```sh
cd "/Users/emzy/Documents/Codex/2026-09-30/i-have-this-hardware-https-docs/outputs/papers3-cashu"

"/Users/emzy/Documents/Codex/2026-09-30/i-have-this-hardware-https-docs/work/idf-python/bin/python" \
  -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 \
  write_flash 0x100000 firmware/papers3_cashu.bin
```

If connecting fails, enter download mode using the PaperS3 power button and retry. Do not run an erase command for this update.

After reboot: **Send ecash → select denominations → Review → Create token**. If a saved outgoing bundle already exists, Send ecash reopens that bundle. Keep it until every code is redeemed or backed up; then remove its saved copy to start another send. Next/Previous on the QR screen move between individual redeemable codes.
