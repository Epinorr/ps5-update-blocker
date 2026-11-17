# Console test procedure

This release is for an owned, jailbroken test console. No console or updater tests have been performed by this project. Keep a separate working ELF loader. Target firmware: recognized SDK versions through 13.60. Record your exact firmware, loader, HEN and Payload Manager versions.

## Phase 1: placement and persistence

1. Keep firmware-download hosts blocked at the router/network during this phase. Disable both automatic system-software download and installation in console settings. An offline console/LAN without internet is suitable for checking files. This network precaution is not a function of this payload.
2. Reboot and jailbreak. Do not start etaHEN or another HEN that unmounts `/update`. Use a compatible standalone ELF loader and Payload Manager. Stop any active system-software download.
3. Run `Status.elf`. Save `status.log`. Require a real `/update` mount. If code 4 appears, do not create a replacement `/update` folder or attempt guessed mounts. If code 2 appears, inspect the preserved content before proceeding.
4. Run `Disable-Updates.elf` once. Require code 0 and `PLACEHOLDERS_PRESENT`; save `disable.log`. Run Status and save its report separately. With FTP, verify both target paths are directories containing only this project's marker. Do not try to upload a real PUP as a test.
5. Run Disable again. It should remain present, never toggle to Enable.
6. Remove this project's payloads from all autoload lists. Fully shut down and power on. **Do not rerun Disable.** With network blocking still active, re-jailbreak and run only Status before any unmounting HEN. Save the new log and compare it with the pre-reboot log. This checks retained files after reboot; it does not prove that the unjailbroken updater was blocked.
7. Repeat a normal restart and a rest/resume cycle. If storage repair occurs during ordinary testing, record its effect too; do not deliberately crash the console for an initial test.
8. Run `Enable-Updates.elf`. Require code 0 and `NO_OWNED_PLACEHOLDERS`. Verify both owned directories are absent. Other blockers/settings may still prevent updates. Enable does not start a download. Keep the network block in place.
9. Repeat Enable to check idempotence. Restore the intended test state explicitly with Disable if needed, then save the last logs.

## Phase 2: real updater behavior (not established by Phase 1)

File persistence, a successful ELF run and an Actions green check do not prove the PS5 downloader uses only these paths or cannot remove the placeholders. A controlled hardware investigation of the actual updater is required before calling the tool permanent. Do not use an unrestricted live firmware installation as a test. Document the network conditions and observed file/updater behavior; a network timeout alone is not evidence that the filesystem blocker worked.

USB, disc, Safe Mode, reset/reinstallation, forced cleanup and other firmware versions are separate scopes. Do not infer their behavior from an internet-download test. After a kernel panic or storage repair, recheck Status before relying on a previous result.

## If something fails

- Code 4 after etaHEN: reboot/re-jailbreak and run before it. No automatic remount is implemented.
- Code 2: copy out the log and inspect both paths. The project will not remove real PUPs, legacy folders or unfamiliar contents. Do not delete `/update` recursively.
- Code 1/3: changes may be partial. A complete owned marker may be safely handled by retrying the same action. An empty/truncated marker or unknown content requires manual inspection; Enable deliberately refuses it.
- No log: verify the external ELF loader and SDK/runtime compatibility. It may have failed before project code ran.
- Directory fsync unavailable: logs say so. Power-loss durability is unverified even if file creation succeeded.

## Useful test report

Record exact console firmware and model family, loader/manager/HEN versions and load order, ELF SHA-256/build SDK revision, each action and code, mount type/source from logs, cold boot/restart/rest results, whether Disable was excluded from autoload, network blocking conditions and whether storage repair occurred. Attach the separate pre/post logs. Distinguish observed placeholder survival from a demonstrated real-updater failure. The repository's Hardware test issue form captures these fields.
