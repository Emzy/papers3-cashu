# PaperS3 validation sequence

Use a development mint with valueless tokens. These checks have not been run on hardware.

1. Flash and boot. Confirm landscape 960×540 rendering with no PSRAM error. Run `selftest`; require `PASSED`. Inspect `heap` and `tasks` after each later step.
2. Generate a seed on a fresh wallet. Configure Wi-Fi, reboot, and verify the credentials persist. Add a development mint and confirm it is restored after another reboot. Confirm HTTPS fails for an invalid certificate and before valid network time is available.
3. Test all touch buttons, keypad digits, Delete and Cancel. Holding a finger down must not activate controls on the next screen. Check the display after several partial refreshes and a forced Refresh.
4. Create a 16-sat invoice, scan it with another wallet, and reboot before payment. Reopen Receive and verify the invoice is identical. Pay with the test mint's development Lightning backend and Check payment; verify the balance and persisted proofs after reboot.
5. Import a v3 and a v4 development token over USB. Verify the net redeemed amount and fee accounting. Try an already-spent token, malformed token, HTTP mint URL and an input longer than 4095 characters; none should be treated as successful redemption.
6. Paste a BOLT11 invoice with `melt`. Verify no spend occurs before touching Pay now. Cancel once; then retry with a fresh quote and pay. Compare amount, fees and change with the mint. Interrupt networking to check that uncertain results are not labelled completed. Recovery from that uncertainty is an outstanding implementation task.
7. Export all sat proofs from one mint. Scan and redeem the token with another wallet. Verify only that mint's sat balance is removed. Reboot before and after scan and verify the same saved token remains accessible. Hide the QR and verify the e-paper image is cleared.
8. With development tokens, cut power between committing the outbox and removing local proofs (instrumentation/debugger may be needed). On reboot, the outbox must survive and its proofs must be absent from the spendable balance. Simulate NVS write failure; never continue spending with an ambiguous export.
9. Test large tokens: unreadable-sized QR payloads should show the USB fallback. Run `outbox` and compare the entire returned token, including its prefix and final characters. Test importing the largest supported 4095-character command without missing bytes.
10. Test Wi-Fi loss/reconnect, mint outages, low battery and extended idle use. Measure current draw; this version has no automatic deep sleep. Confirm factory/other-firmware flashing and NVS erasure are excluded from any funded-device workflow.

Passing these checks is necessary for hardware bring-up, but does not complete a security review or the missing mint-transaction recovery work listed in the README.
