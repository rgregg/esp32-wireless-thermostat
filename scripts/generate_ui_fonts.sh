#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_TTF="$ROOT_DIR/third_party/fonts/Montserrat-Regular.ttf"
MDI_TTF="$ROOT_DIR/third_party/fonts/materialdesignicons-webfont.ttf"
OUT_DIR="$ROOT_DIR/src/thermostat/ui/fonts"

if [[ ! -f "$FONT_TTF" ]]; then
  echo "Missing font file: $FONT_TTF" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

generate_font() {
  local size="$1"
  local symbols="$2"
  local out="$OUT_DIR/thermostat_font_montserrat_${size}.c"
  echo "Generating ${out}"
  npx --yes lv_font_conv \
    --size "$size" \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --font "$FONT_TTF" \
    --range 0x20-0x7E \
    --symbols "$symbols" \
    --lv-font-name "thermostat_font_montserrat_${size}" \
    -o "$out"
  perl -0pi -e 's/#include "lvgl\/lvgl\.h"/#include "lvgl.h"/g' "$out"
}

# Full UI text sizes from ESPHome layout.
generate_font 20 "°%:-./"
generate_font 26 "°%:-./"
generate_font 28 "°%:-./"
generate_font 30 "°%:-./"
generate_font 40 "°%:-./"
generate_font 48 "°%:-./"
generate_font 80 "°%:-./"

# Large indoor temp value font only needs temperature glyphs and N/A fallback.
generate_font 120 "0123456789N/A .-°CF"

generate_icon_font() {
  if [[ ! -f "$MDI_TTF" ]]; then
    echo "Missing MDI font file: $MDI_TTF (skipping icon font)" >&2
    return
  fi
  local out="$OUT_DIR/thermostat_font_mdi_weather_30.c"
  echo "Generating ${out}"
  npx --yes lv_font_conv \
    --size 30 \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --font "$MDI_TTF" \
    --range 0xF0590-0xF0599,0xF059D,0xF067E,0xF0F36 \
    --lv-font-name "thermostat_font_mdi_weather_30" \
    -o "$out"
  perl -0pi -e 's/#include "lvgl\/lvgl\.h"/#include "lvgl.h"/g' "$out"
}

generate_icon_font

echo "Done. Generated fonts in $OUT_DIR"
