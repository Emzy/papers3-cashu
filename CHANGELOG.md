# Changes

## 0.2.0-papers3 — 2026-09-30

- Send ecash now lets you select stored tokens by denomination, with checkboxes, pagination, a running total and a confirmation screen.
- Only the selected proofs are removed. Confirming an obsolete or ambiguous selection is rejected; unrelated wallet additions do not invalidate a valid selection.
- Saved ecash displays one complete proof per QR, navigated manually. Each code is redeemable separately; this is not a multipart QR protocol.
- QR area enlarged from 412 to 520 pixels square, with minimum module width increased from 3 to 5 pixels. QR screens use a full-quality refresh. The quiet zone remains four modules or greater; no cryptographic proof data is removed.
- `outbox N` prints the corresponding individual QR token over USB; `outbox` retains the complete backup bundle.
- Existing NVS, partition offsets and saved outboxes remain compatible. Update only the application at `0x100000`.
- Fixed the bundled QR encoder interface to return normalized boolean module values to C++ callers; an independent image check exposed missing pixels in the host rendering.
- Added host tests for selection, changed/spent proofs, 64-bit totals, token paging, metadata and QR geometry.
