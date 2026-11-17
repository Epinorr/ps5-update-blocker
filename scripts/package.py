#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check PS5 ELF outputs and create a console test archive, without deploying."""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ELFS = ("Status.elf", "Disable-Updates.elf", "Enable-Updates.elf")
VERSION = "0.1.0-experimental"


def source_files():
    names = ["README.md", "LICENSE", "THIRD_PARTY.md", "CHANGELOG.md",
             "Makefile", "SDK_REVISION", ".gitignore", ".gitattributes"]
    files = [ROOT / name for name in names]
    for folder in (".github", "docs", "include", "src", "scripts", "tests"):
        files.extend(path for path in (ROOT / folder).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts
                     and path.suffix != ".pyc")
    return sorted(files)


def write_source_zip(destination, prefix=""):
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in source_files():
            name = str(path.relative_to(ROOT)).replace("\\", "/")
            archive.write(path, f"{prefix}{name}")


def verify_elf(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError(f"{path.name}: expected a 64-bit little-endian ELF")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if machine != 62 or elf_type not in (2, 3):
        raise ValueError(f"{path.name}: unexpected ELF machine/type")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    if phsize < 56 or not phcount or phoff + phsize * phcount > len(data):
        raise ValueError(f"{path.name}: invalid program header table")
    loads = 0
    entry = struct.unpack_from("<Q", data, 24)[0]
    entry_is_executable = False
    for i in range(phcount):
        kind, flags, offset, vaddr, _, filesz, memsz, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phsize
        )
        if kind == 1:
            loads += 1
            if filesz > memsz or offset + filesz > len(data):
                raise ValueError(f"{path.name}: invalid LOAD segment")
            if flags & 1 and vaddr <= entry < vaddr + memsz:
                entry_is_executable = True
    if not loads or not entry_is_executable:
        raise ValueError(f"{path.name}: no executable entry segment")
    if b"UB_TEST_FAULT" in data or b"--test-root" in data:
        raise ValueError(f"{path.name}: host-only test hooks leaked into payload")
    return {"file": path.name, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "machine": "x86_64", "elf_type": elf_type, "load_segments": loads}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-revision", required=True)
    parser.add_argument("--sdk-source", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if len(args.sdk_revision) != 40 or any(c not in "0123456789abcdef" for c in args.sdk_revision):
        parser.error("SDK revision must be a complete lowercase commit hash")
    dist = ROOT / "dist"
    actual = subprocess.check_output(
        ["git", "-C", str(args.sdk_source), "rev-parse", "HEAD"], text=True).strip()
    if actual != args.sdk_revision:
        raise ValueError("SDK source does not match the build revision")
    records = [verify_elf(dist / name) for name in ELFS]
    if len({item["sha256"] for item in records}) != len(ELFS):
        raise ValueError("Action payloads unexpectedly have identical contents")
    source_hash = hashlib.sha256()
    for path in source_files():
        source_hash.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\x00")
        source_hash.update(path.read_bytes() + b"\x00")
    manifest = {"version": VERSION, "sdk_repository": "https://github.com/ps5-payload-dev/sdk",
                "sdk_revision": args.sdk_revision, "firmware_target_max": "13.60",
                "project_source_sha256": source_hash.hexdigest(),
                "hardware_tested": False, "reboot_persistence_verified": False,
                "files": records}
    (dist / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    sums = "".join(f"{item['sha256']}  {item['file']}\n" for item in records)
    (dist / "SHA256SUMS.txt").write_text(sums, encoding="ascii")
    own_source = ROOT / "build/ps5-update-blocker-source.zip"
    sdk_tar = ROOT / "build/ps5-payload-sdk.tar"
    write_source_zip(own_source)
    subprocess.run(["git", "-C", str(args.sdk_source), "archive", "--format=tar",
                    "--prefix=ps5-payload-sdk/", f"--output={sdk_tar}",
                    args.sdk_revision], check=True)
    bundle = dist / "PS5-Update-Blocker-v0.1.0-experimental.zip"
    members = [(dist / name, name) for name in (*ELFS, "build-info.json", "SHA256SUMS.txt")]
    members += [(ROOT / name, name) for name in
                ("README.md", "LICENSE", "THIRD_PARTY.md", "CHANGELOG.md",
                 "docs/README.fa.md", "docs/TESTING.md", "docs/TESTING.fa.md")]
    members += [(own_source, "source/ps5-update-blocker-source.zip"),
                (sdk_tar, "source/ps5-payload-sdk.tar")]
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, name in members:
            archive.write(path, name)
    with zipfile.ZipFile(bundle) as archive:
        if archive.testzip() is not None:
            raise ValueError("Archive integrity check failed")
    print(json.dumps(manifest, indent=2))
    print(f"Console test bundle: {bundle}")


if __name__ == "__main__":
    main()
