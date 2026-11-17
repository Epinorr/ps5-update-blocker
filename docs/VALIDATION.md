# Validation record for the initial release

Date: 2026-11-17. Source SDK: commit `8816aee4cd88fc47b07819d2b6f4769c095a074b`.

Completed locally:

- Built the SDK from the pinned source using Clang/LLVM 18 on Ubuntu under WSL2 (Ubuntu 26.04 host; the Actions runner is Ubuntu 24.04).
- Compiled Status, Disable Updates and Enable Updates as three x86-64 PS5 ELF payloads with warnings treated as errors.
- Passed 28 real filesystem tests with AddressSanitizer and UndefinedBehaviorSanitizer. Tests use isolated Linux temporary directories; no console or network is contacted.
- Parsed workflow YAML and checked Bash/Python syntax. Ran the same `scripts/build.sh` pipeline used by Actions, including native build, ELF checks, checksums and packaging.
- Validated all ELF program header tables, LOAD bounds and executable entry segments. Checked that host-only fault/test-root hooks are absent from production binaries.
- Checked Persian documentation with the installed Persian text linter.

Not performed:

- A remote run on GitHub Actions (the project has not been uploaded to a GitHub repository in this session).
- PS5 loading/execution, notifications, mount-structure ABI or filesystem API behavior on console.
- Actual firmware-downloader behavior, cold-boot/restart/rest persistence, storage repair, or every firmware through 13.60.
- USB/disc/recovery protection or fault/power-loss durability on console storage.

The host tests and native build are useful implementation checks. They do not establish a permanent block. Future hardware results should include the exact build SHA-256, logs and test conditions from TESTING.md. Do not change `hardware_tested` or `reboot_persistence_verified` in the build manifest based on host checks.
