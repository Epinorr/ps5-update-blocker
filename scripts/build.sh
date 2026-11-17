#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk_revision=$(tr -d '\r\n' < "$project_dir/SDK_REVISION")
if [[ ! "$sdk_revision" =~ ^[0-9a-f]{40}$ ]]; then
  echo "Invalid SDK_REVISION" >&2
  exit 1
fi
sdk_source=${SDK_SOURCE_DIR:-"$project_dir/build/sdk-source"}
sdk_install="$project_dir/build/sdk"
if [[ ! -d "$sdk_source/.git" ]]; then
  if [[ -e "$sdk_source" ]]; then
    echo "SDK source path exists without Git metadata; refusing to overwrite it" >&2
    exit 1
  fi
  mkdir -p -- "$sdk_source"
  git -C "$sdk_source" init
  git -C "$sdk_source" remote add origin https://github.com/ps5-payload-dev/sdk.git
  git -C "$sdk_source" fetch --depth=1 origin "$sdk_revision"
  git -C "$sdk_source" checkout --detach FETCH_HEAD
fi
actual_revision=$(git -C "$sdk_source" rev-parse HEAD)
if [[ "$actual_revision" != "$sdk_revision" ]]; then
  echo "SDK source revision mismatch: $actual_revision" >&2
  exit 1
fi
# Upstream make rebuilds a tracked host executable through an implicit rule.
# Permit that generated file only; verify every other tracked source and do
# not trust the generated tool: rebuild its installed copy below from source.
if ! git -C "$sdk_source" diff --quiet HEAD -- . ':(exclude)host/bin/prospero-nid' ||
   [[ -n "$(git -C "$sdk_source" ls-files --others --exclude-standard)" ]]; then
  echo "SDK source has modifications or extra files; use a pristine checkout" >&2
  exit 1
fi
make -C "$sdk_source" -j2 install \
  LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-18}" DESTDIR="$sdk_install"
"${HOST_CC:-cc}" -O2 "$sdk_source/host/bin/prospero-nid.c" -o "$project_dir/build/prospero-nid"
install "$project_dir/build/prospero-nid" "$sdk_install/bin/prospero-nid"
make -B -C "$project_dir" -j2 PS5_PAYLOAD_SDK="$sdk_install" all
python3 "$project_dir/scripts/package.py" --sdk-revision "$sdk_revision" --sdk-source "$sdk_source"
