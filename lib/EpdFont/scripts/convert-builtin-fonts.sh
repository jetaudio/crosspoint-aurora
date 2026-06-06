#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# System (UI) font faces, selectable at runtime via the "System Font" setting.
# Both carry a Hebrew fallback (0x05D0-0x05EA) so every shipped language renders.
#   ubuntu_*     -> the Vietnamese-localized Ubuntu cut (Latin + Vietnamese +
#                   Cyrillic/Greek from the cut, Hebrew from Noto). "Ubuntu" option.
#   notosansui_* -> Noto Sans (the default Aurora look). "Noto Sans" option.
for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    lc=$(echo $style | tr '[:upper:]' '[:lower:]')
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    python fontconvert.py "ubuntu_${size}_${lc}" $size \
      "../builtinFonts/source/Ubuntu/Ubuntu-VN-${style}.ttf" $hebrew_path \
      --additional-intervals 0x05D0,0x05EA > "../builtinFonts/ubuntu_${size}_${lc}.h"
    echo "Generated ../builtinFonts/ubuntu_${size}_${lc}.h"

    python fontconvert.py "notosansui_${size}_${lc}" $size \
      "../builtinFonts/source/NotoSans/NotoSans-${style}.ttf" $hebrew_path \
      --additional-intervals 0x05D0,0x05EA > "../builtinFonts/notosansui_${size}_${lc}.h"
    echo "Generated ../builtinFonts/notosansui_${size}_${lc}.h"
  done
done

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA > ../builtinFonts/notosans_8_regular.h

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
