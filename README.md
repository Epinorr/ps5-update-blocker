# PS5 Update Blocker

**v0.1.0-experimental** · [راهنمای فارسی](docs/README.fa.md)

An experimental, standalone PS5 payload project for testing persistent update-path placeholders. Build all payloads in **GitHub Actions** without installing a compiler on your PC.

**Console behavior and reboot persistence are unverified.** The target is jailbroken firmware recognized by the pinned SDK, up to **13.60**. This is a firmware target, not a tested compatibility list. Do not describe this release as a proven permanent update blocker.

## Payloads

| File | Operation |
| --- | --- |
| `Status.elf` | Inspect the real `/update` mount and both paths. Writes a diagnostic log under `/data`, but never changes update paths. |
| `Disable-Updates.elf` | Create nonempty, marked directories at `/update/PS5UPDATE.PUP` and `/update/PS5UPDATE.PUP.net.temp`. Repeat execution preserves the disabled intent. |
| `Enable-Updates.elf` | Remove only this project's recognized placeholders. Repeat execution is harmless. It does not download/install firmware or turn automatic updates on. |

This uses stored filesystem objects rather than a resident process or a session-only unmount. Those objects are intended to survive normal reboot. Whether PS5 startup, storage repair or the real updater removes/bypasses them is the central hardware test still needed. A nonempty marker resists a simple `rmdir`; it does not stop a recursive or privileged cleanup.

The project does not patch the kernel/updater, mount or unmount anything, modify executables, fill storage, or block network traffic. It does not establish protection against USB, disc, recovery updates or a reset.

## Build from GitHub's Actions page

1. Create a GitHub repository, for example `ps5-update-blocker`.
2. Upload the **contents** of this source project at its root. Include `.github/workflows/build.yml`; do not nest the project inside another folder.
3. Open **Actions → Build PS5 payloads → Run workflow** on the default branch. GitHub may ask you to enable Actions for the repository. Pushes also trigger a build.
4. Open the successful run. Under **Artifacts**, download `ps5-update-blocker-experimental-<run number>`.
5. Extract the Actions artifact, then extract `PS5-Update-Blocker-v0.1.0-experimental.zip` inside it.

No personal access token, repository secret, console IP or paid dependency is needed. The workflow has read-only repository permissions and uploads an artifact; it does not create a public Release or send anything to a console. GitHub Actions must be enabled/available on your account. `workflow_dispatch` must exist on the default branch for the Run workflow button.

The artifact contains the three ELF files, `SHA256SUMS.txt`, build information, test guides, and corresponding source archives for this project and the exact SDK revision. A successful workflow proves host tests and native compilation, not console protection.

## First console run

Read [the test procedure](docs/TESTING.md) before running a mutating payload. A working, privileged ELF loader is required. Payload Manager sends payloads to a separate loader; installing the manager alone does not provide one.

You can place the ELFs in the USB `pldmgr/` directory and import/run them through itsPLK's Payload Manager. Start with `Status.elf`. Logs are `/data/ps5-update-blocker/status.log`, `disable.log`, and `enable.log` (the latest run of each action replaces its own log).

**Run before etaHEN or other HENs that unmount `/update`.** If `/update` is not the actual mounted update filesystem, the payload refuses the operation. Reboot, jailbreak, start a compatible standalone ELF loader/manager and inspect again before loading such a HEN. This project deliberately does not guess how to remount the partition.

## Conflicts and interrupted operations

All target paths are checked before the first placeholder change. A real PUP, partial download, legacy empty directory, symlink, malformed marker, extra directory content or marker hard link is preserved and reported as a conflict. It is not automatically adopted or deleted. Stop active downloads before testing. The file lock serializes this project's mutations; Sony services do not honor that lock.

An interruption can leave one owned placeholder: rerunning the same action can finish it. A crash between `mkdir` and completing its marker can leave ambiguous content that requires manual inspection. There is no recursive cleanup or automatic rollback that would silently reopen updates after a failed Disable.

Enable preflights both paths too. If either contains foreign data, it leaves both alone. An I/O error during an operation can leave a partial state; read the log and use Status. The harmless empty `/update/.ps5-update-blocker.lock` is retained after Enable. A valid marker is an ownership convention, not cryptographic authentication against another privileged program.

| Code | Meaning |
| ---: | --- |
| 0 | Requested operation/inspection completed; check reported state. |
| 1 | I/O error; inspect the final state and log. |
| 2 | Foreign or ambiguous content preserved. |
| 3 | Partial placeholders / unexpected final state. |
| 4 | `/update` absent, unmounted, unusable, or read-only for a mutation. |
| 5 | Another copy of this project's mutating payload holds the lock. |
| 6 | Firmware outside the target policy or unavailable version. |

`PLACEHOLDERS_PRESENT` means two recognized directories exist **now**. It never means that reboot persistence or Sony's downloader behavior was verified. `NO_OWNED_PLACEHOLDERS` does not certify that other blockers are absent. The loader/SDK can fail before `main`; that may produce no project log or notification.

## Local build and checks

On Ubuntu 24.04:

```bash
sudo apt-get update
sudo apt-get install -y clang-18 lld-18 llvm-18-dev make gcc libc6-dev python3 git ca-certificates
make test
bash scripts/build.sh
```

The SDK is fetched at the full commit in `SDK_REVISION`, built from source and installed inside `build/sdk`. `SDK_SOURCE_DIR=/path/to/a/pristine/sdk` can reuse a checkout at that exact revision. Do not change SDK/firmware values just to bypass a failed compatibility check.

The host tests execute actual filesystem C code with ASan/UBSan in isolated temporary directories. Fault injection exists only in the host executable. These tests cover ownership, repeat runs, partial changes, symlinks, special files, failures and concurrent invocations; they do not emulate PS5 or its updater. The package script validates ELF headers/segments and records SHA-256 checksums. It does not run the payloads.

## Source and credits

Original implementation in this repository, informed by the filesystem technique documented in [Scene-Collective/ps4-disable-updates](https://github.com/Scene-Collective/ps4-disable-updates) and its counterpart. The repository does not reuse the PS4 exploit/binaries. [etaHEN's public bootstrapper](https://github.com/etaHEN/etaHEN/blob/dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c/Source%20Code/bootstrapper/source/main.cpp#L1151) uses a different, unmount-based technique. The older [logic-68 PS5 port](https://github.com/logic-68/Enable-Disable-UPD-PS5) is context, not a firmware compatibility claim for this implementation.

Built with [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk). Intended to be loaded with tools such as [itsPLK/ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager) and a compatible ELF loader. See [THIRD_PARTY.md](THIRD_PARTY.md).

GPL-3.0-or-later. See [LICENSE](LICENSE). PlayStation and PS5 are Sony trademarks. This is an independent experimental project.
