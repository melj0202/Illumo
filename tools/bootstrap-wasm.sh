#!/usr/bin/env bash
# Linux x86_64 counterpart of tools/bootstrap-wasm.ps1: downloads the pinned
# Wasmtime C API and CLI and the WASI SDK, verifies each archive's SHA256 and
# extracts it into build-wasm-tools/ (or the directory given). Keep the
# versions in step with bootstrap-wasm.ps1 and cmake/IllumoWasm.cmake.
#
#   bash tools/bootstrap-wasm.sh [destination]

set -euo pipefail

destination="${1:-$(dirname "$0")/../build-wasm-tools}"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "This script fetches the Linux x86_64 toolchains; on Windows x64 run tools/bootstrap-wasm.ps1." >&2
  exit 1
fi
for tool in curl sha256sum tar xz; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "bootstrap-wasm.sh needs '$tool' on PATH." >&2
    exit 1
  fi
done

# name|archive|url|sha256
packages=(
  "wasmtime-v48.0.7-x86_64-linux-c-api|wasmtime-v48.0.7-x86_64-linux-c-api.tar.xz|https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.7/wasmtime-v48.0.7-x86_64-linux-c-api.tar.xz|3d65ab7a10127632ec6d41f0340866272abd9dab8bc7b9fdc9deaea811ee3719"
  "wasmtime-v48.0.7-x86_64-linux|wasmtime-v48.0.7-x86_64-linux.tar.xz|https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.7/wasmtime-v48.0.7-x86_64-linux.tar.xz|2192e2b5b911dc42321b9eac2b7f1f0b0d03356058c1e2b2c2b892625bdd0f45"
  "wasi-sdk-34.0-x86_64-linux|wasi-sdk-34.0-x86_64-linux.tar.gz|https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-34.0-x86_64-linux.tar.gz|b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4"
)

hash_of() {
  sha256sum "$1" | cut -d ' ' -f 1
}

mkdir -p "$destination"
destination="$(cd "$destination" && pwd)"
for package in "${packages[@]}"; do
  IFS='|' read -r name archive url hash <<<"$package"
  archive_path="$destination/$archive"
  if [ ! -f "$archive_path" ]; then
    partial_path="$archive_path.partial"
    curl --fail --location --silent --show-error --output "$partial_path" "$url"
    if [ "$(hash_of "$partial_path")" != "$hash" ]; then
      echo "SHA256 mismatch for $archive; refusing extraction." >&2
      exit 1
    fi
    mv "$partial_path" "$archive_path"
  fi
  if [ "$(hash_of "$archive_path")" != "$hash" ]; then
    echo "SHA256 mismatch for $archive_path; refusing extraction." >&2
    exit 1
  fi
  # Always extract from the verified archive, including when a prior run was interrupted.
  tar -xf "$archive_path" -C "$destination"
  echo "Verified $name"
done
echo "Toolchains are local to $destination; no PATH or global installation was changed."
