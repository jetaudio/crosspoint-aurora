# Aurora optimization notes

Measured baseline before this work (env `gh_release`):
- Flash: **78.4%** (5,140,291 / 6,553,600 B of the app partition)
- Static RAM: 30.9% (101,204 / 327,680 B)

`.rodata` (3.36 MB) dominates the image; `.text` (code) is 1.69 MB. The linker
already runs `--gc-sections`, so every byte left in the image is *referenced* —
there is no dead data to delete, only referenced things to stop referencing.

## Applied

### Hyphenation language trim (−293 KB flash) — DONE
The EPUB reader bundles Liang/TeX hyphenation pattern tries per language. They are
large (German alone **206 KB**; de+ru+sv+uk+pl total ~300 KB), were compiled in
unconditionally, and selected only at runtime by the book's `dc:language`.
Hyphenation is **off by default** (`hyphenationEnabled = 0`) and a missing language
degrades gracefully to default min-prefix/suffix break rules (the same path
Vietnamese/CJK already take), so dropping unused languages is safe.

- `LanguageRegistry.cpp` now guards each language behind `HYPHENATION_LANG_xx`
  (default **1**, so upstream/default/host-test builds keep all 9).
- `platformio.ini` opts this (Vietnamese-focused) fork out of de/ru/sv/uk/pl,
  keeping en/fr/es/it.
- Result: **gh_release flash 78.4% → 73.9%** (5,140,291 → 4,839,841 B).
- To re-enable a language, delete its `-DHYPHENATION_LANG_xx=0` line in
  `platformio.ini`.

## Not applied — needs on-device verification (emulator is off-limits)

After the trim, flash sits at 73.9% with ~1.7 MB headroom, so flash pressure is no
longer the constraint; the items below trade rising risk for shrinking value and
should each be verified on real hardware before shipping.

1. **UI language trim (~179 KB).** 25 languages are generated (`gen_i18n.py`),
   already deduped against English and stripped of unused keys. Keeping only VI+EN
   would save ~179 KB. **Caveat:** `gen_i18n.py` emits a `V1_LANGUAGES` migration
   table that hard-references 22 specific `Language` enum values via `static_assert`;
   dropping any of those 22 needs that table updated and risks mis-migrating old
   `language.bin` settings. VI is safe (not in the V1 table). Treat as invasive.

2. **LTO (`-flto`), `-fno-rtti`.** No `dynamic_cast`/`typeid` exists, so `-fno-rtti`
   is safe but only worth a few KB. LTO gives a modest `.text` cut (most `.text` is
   precompiled framework that LTO can't touch) and must be checked against the
   `-Wl,--wrap=panic_*` handlers on hardware. Low priority now.

3. **Font set.** `--gc-sections` already drops unreferenced glyph data, so the
   Ubuntu/NotoSerif/NotoSans bitmaps present are all referenced. Further cuts mean
   changing what a `EpdFontFamily` references (e.g. dropping leftover Ubuntu UI fonts
   if the Aurora UI is fully NotoSans, or dropping italic/bold-italic for UI-only
   families) — visual regression risk, verify on hardware.

4. **Web-transfer assets** (`jszip` 28 KB, `FilesPageHtml` 45 KB, etc.) are only
   needed for the WiFi file-transfer page; gate behind a build flag if that feature
   is unused on this device.

## Not worth touching
- **`display` (52 KB BSS)** is the e-ink framebuffer; `EINK_DISPLAY_SINGLE_BUFFER_MODE`
  is already on. Not reducible without changing the display.
- The font symbols at `0x3C…` are flash-mapped `.rodata`, **not** RAM.
