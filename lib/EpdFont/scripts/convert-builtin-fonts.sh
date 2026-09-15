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
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --force-autohint --zopfli > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --force-autohint --zopfli > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

python generate-ui-noto-fonts.py

# System (UI) font faces, selectable at runtime via the "System Font" setting.
# Both carry a Hebrew fallback (0x05D0-0x05EA) so every shipped language renders.
#   ubuntu_*     -> the Vietnamese-localized Ubuntu cut (Latin + Vietnamese +
#                   Cyrillic/Greek from the cut, Hebrew from Noto). "Ubuntu" option.
#   notosansui_* -> Noto Sans (the default Aurora look). "Noto Sans" option.
for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    arabic_path="../builtinFonts/source/NotoSansArabic/NotoSansArabic-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path $hebrew_path $arabic_path $viet_path \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > $output_path
    echo "Generated $output_path"
  done
done

# --- Aurora-only UI families -------------------------------------------------
# Upstream's loop above covers Ubuntu, and generate-ui-noto-fonts.py covers the
# Noto Sans UI faces. These two exist only on aurora, where the System Font
# setting offers them alongside Noto Sans and Ubuntu. They keep --force-autohint
# rather than upstream's --mono: neither has an optically monochrome cut to pair
# with, and the autohinter is what gave them even stems at 10/12px.
UI_EXTRA_HEBREW_REGULAR="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-UIMedium.ttf"
UI_EXTRA_HEBREW_BOLD="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-UIBold.ttf"

# notosansui_*: the "Noto Sans" System Font option. Kept here rather than
# hand-run, so a change to the generated EpdFontData layout regenerates them
# with everything else -- they were left out of this script once and went stale.
for size in 10 12; do
  python fontconvert.py "notosansui_${size}_regular" $size     ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf     ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/notosansui_${size}_regular.h"
  python fontconvert.py "notosansui_${size}_bold" $size     ../builtinFonts/source/NotoSans/NotoSans-Bold.ttf     ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Bold.ttf     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/notosansui_${size}_bold.h"
  echo "Generated notosansui_${size}_{regular,bold}.h"
done

# EB Garamond UI font (extract static instances from variable font first with instancer).
# Source: google/fonts main/ofl/ebgaramond/EBGaramond[wght].ttf -> instanced at wght=400/700.
for size in 10 12; do
  python fontconvert.py "ebgaramond_${size}_regular" $size     "../builtinFonts/source/EBGaramond/EBGaramond-Regular.ttf" "$UI_EXTRA_HEBREW_REGULAR"     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/ebgaramond_${size}_regular.h"
  python fontconvert.py "ebgaramond_${size}_bold" $size     "../builtinFonts/source/EBGaramond/EBGaramond-Bold.ttf" "$UI_EXTRA_HEBREW_BOLD"     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/ebgaramond_${size}_bold.h"
  echo "Generated ebgaramond_${size}_{regular,bold}.h"
done

# SFU Goudy UI font (single Medium weight used for both regular and bold positions).
# Source: SFUGoudyMedium.TTF (local, copy into builtinFonts/source/SFUGoudy/).
for size in 10 12; do
  python fontconvert.py "sfugoudy_${size}_regular" $size     "../builtinFonts/source/SFUGoudy/SFUGoudyMedium.ttf" "$UI_EXTRA_HEBREW_REGULAR"     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/sfugoudy_${size}_regular.h"
  python fontconvert.py "sfugoudy_${size}_bold" $size     "../builtinFonts/source/SFUGoudy/SFUGoudyMedium.ttf" "$UI_EXTRA_HEBREW_BOLD"     --additional-intervals 0x05D0,0x05EA --force-autohint > "../builtinFonts/sfugoudy_${size}_bold.h"
  echo "Generated sfugoudy_${size}_{regular,bold}.h"
done

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" --force-autohint > ../builtinFonts/notosans_8_regular.h

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
