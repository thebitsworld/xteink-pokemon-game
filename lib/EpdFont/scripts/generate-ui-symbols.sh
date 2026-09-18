#!/bin/bash
set -e
cd "$(dirname "$0")"

# One shared 10-point icon/symbol glyph set for both UI scales (power icon,
# gender symbols, shiny star). Restrict the source faces so none of the
# converter's default character ranges are included.
"${PYTHON:-python3}" fontconvert.py ui_symbols_10 10 \
  ../builtinFonts/source/NotoSymbols/NotoSansSymbols2-Regular.ttf \
  ../builtinFonts/source/NotoSymbols/NotoSansSymbols-Regular.ttf \
  --no-default-intervals \
  --additional-intervals 0x23FB,0x23FB \
  --additional-intervals 0x2640,0x2640 \
  --additional-intervals 0x2642,0x2642 \
  --additional-intervals 0x2605,0x2605 \
  > ../builtinFonts/ui_symbols_10.h
