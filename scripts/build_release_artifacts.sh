#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERSION="$(git describe --tags --long --dirty --always 2>/dev/null || echo dev)"
SAFE_VERSION="$(printf '%s' "$VERSION" | tr '/ :@' '____')"
OUT_DIR="dist/releases/${SAFE_VERSION}"

ENVS=(
  "esp32-furnace-controller"
  "esp32-furnace-thermostat"
)

echo "Building release artifacts for version: ${VERSION}"
mkdir -p "$OUT_DIR"

for env in "${ENVS[@]}"; do
  echo "==> pio run -e ${env}"
  pio run -e "$env"

  src_bin=".pio/build/${env}/firmware.bin"
  if [[ ! -f "$src_bin" ]]; then
    echo "Missing firmware binary for ${env}: ${src_bin}" >&2
    exit 1
  fi

  out_bin="${OUT_DIR}/${env}-${SAFE_VERSION}.bin"
  cp "$src_bin" "$out_bin"
  echo "Wrote ${out_bin}"
done

{
  echo "version=${VERSION}"
  echo "built_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "${OUT_DIR}/build-info.txt"

if command -v shasum >/dev/null 2>&1; then
  (
    cd "$OUT_DIR"
    shasum -a 256 ./*.bin > SHA256SUMS
  )
fi

echo "Release artifacts complete: ${OUT_DIR}"
