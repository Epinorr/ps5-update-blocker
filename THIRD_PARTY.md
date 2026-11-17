# Dependencies and provenance

## Linked runtime and build dependency

- **ps5-payload-dev/sdk**, pinned in `SDK_REVISION`: <https://github.com/ps5-payload-dev/sdk/tree/8816aee4cd88fc47b07819d2b6f4769c095a074b>
- Upstream includes GPLv3+ runtime components and FreeBSD/BSD-licensed headers/components. Preserve their individual notices. The exact tracked SDK source tree is included as `source/ps5-payload-sdk.tar` in the console test bundle, including its upstream LICENSE and source-file notices. Our source is included separately as `source/ps5-update-blocker-source.zip`.
- The build uses Clang/LLVM, GNU Make, Python, Git and system host tools. These are build tools, not vendored console exploit payloads.

## Related research (not linked or copied into payloads)

- Scene-Collective PS4 disable/enable updates documents the dummy-directory approach.
- etaHEN bootstrapper documents the different `/update` unmount approach.
- logic-68/Enable-Disable-UPD-PS5 is historical PS5 research context.
- itsPLK's Payload Manager and ps5-payload-dev/elfldr are separate programs used to launch compatible payloads. They are not bundled in this project.

The experimental blocking code is original to this project. No Sony executable, PUP, PS4 exploit binary, decrypted firmware file or firmware-specific updater replacement is distributed.
