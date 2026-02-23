#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT_DIR/.pio/build/native-ui-preview/program"
BASELINE_DIR="$ROOT_DIR/testdata/ui-golden"
TMP_DIR="$ROOT_DIR/.tmp/ui-golden-current"
MANIFEST="$BASELINE_DIR/sha256.txt"

update_mode=0
if [[ "${1:-}" == "--update" ]]; then
  update_mode=1
fi

mkdir -p "$TMP_DIR"

if [[ ! -x "$PROGRAM" ]]; then
  pio run -e native-ui-preview
fi

SDL_VIDEODRIVER=dummy "$PROGRAM" --capture-dir "$TMP_DIR"

if [[ "$update_mode" -eq 1 ]]; then
  mkdir -p "$BASELINE_DIR"
  cp "$TMP_DIR"/*.bmp "$BASELINE_DIR"/
  (
    cd "$BASELINE_DIR"
    shasum -a 256 home.bmp fan.bmp mode.bmp settings.bmp > sha256.txt
  )
  echo "Updated UI golden baselines in $BASELINE_DIR"
  exit 0
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "Missing baseline manifest: $MANIFEST" >&2
  echo "Run: scripts/ui_golden_check.sh --update" >&2
  exit 1
fi

actual_manifest="$TMP_DIR/sha256.txt"
(
  cd "$TMP_DIR"
  shasum -a 256 home.bmp fan.bmp mode.bmp settings.bmp > sha256.txt
)

if ! diff -u "$MANIFEST" "$actual_manifest"; then
  echo "UI golden check failed: screenshots changed." >&2
  echo "If intentional, run: scripts/ui_golden_check.sh --update" >&2
  exit 1
fi

echo "UI golden check passed."
