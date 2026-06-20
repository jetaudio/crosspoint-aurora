# CrossPoint Reader — personal fork (Aurora)

Personal fork of the CrossPoint Reader e-reader firmware (ESP32-C3, Xteink X3/X4).

## Git workflow (fork)
- `origin`   → personal fork: https://github.com/jetaudio/crosspoint-aurora
- `upstream` → original repo:  https://github.com/crosspoint-reader/crosspoint-reader
- **`master`** is kept pristine — a mirror of `upstream/master`. Do NOT commit custom work here.
- **`aurora`** is the working branch for all custom changes (the Aurora theme). Push to `origin/aurora`.

### Sync upstream → aurora (rebase)
```bash
git fetch upstream
git switch aurora
git rebase upstream/master            # replay aurora commits on top of latest upstream
# ...resolve any conflicts, then:
git push --force-with-lease origin aurora
```
Keep local master mirroring upstream (optional):
```bash
git switch master && git fetch upstream && git merge --ff-only upstream/master
```

## The Aurora theme (custom work, lives on `aurora`)
A new selectable UI theme (`CrossPointSettings::UI_THEME::AURORA = 4`) that redesigns the home screen:
- Slim status bar + "Now Reading" featured card + "Library" recent-books list + a **bottom icon tab bar**
  (Browse Files / Recent Books / Settings / File Transfer).
- **Two-zone navigation**: side **Up/Down** browse the content list; front **Left/Right** move the bottom bar;
  **Select** activates whichever zone is active; **Back** is unused on home.
- New setting **Show Button Hints** (Settings → Controls) toggles the front hint row globally.
- "Lean" scope: no per-book dates / progress %, clock only on X3 (no RTC on X4).

Key files: `src/components/themes/aurora/AuroraTheme.{h,cpp}`, `src/activities/home/HomeActivity.{cpp,h}`,
`src/components/themes/BaseTheme.{h,cpp}` (the `drawHomeScreen`/`ownsHomeLayout` hook + the `showButtonHints`
guard in `drawButtonHints`), `src/components/UITheme.cpp` (theme registry), `src/SettingsList.h`,
`src/CrossPointSettings.h`, `lib/I18n/translations/english.yaml`.

## Building on Windows (this machine)
PlatformIO Core + clang-format are installed via pip. The RISC-V toolchain installer (`idf_tools.py`)
**refuses to run under Git Bash / MSys**, so always build from **PowerShell** with UTF-8 forced (otherwise
PlatformIO crashes printing non-ASCII language names):
```powershell
$env:PYTHONIOENCODING="utf-8"; $env:PYTHONUTF8="1"
python -m platformio run -e default
```
- Format before commit: `./bin/clang-format-fix -g` (needs clang-format 21+).
- First clone: `git submodule update --init --recursive` (the `open-x4-sdk` submodule is required).
- `core.symlinks=false` is set repo-locally — `CLAUDE.md` and `.github/skills/crosspoint-reader.md` are
  symlinks to `.skills/SKILL.md` upstream; on this fork's `aurora` branch CLAUDE.md is a regular file (this one).

## Firmware contribution guidelines
The upstream contributor skills auto-load from `.skills/` (HAL/abstractions, scope-discipline, heap-discipline,
refactor-for-review, control-flow-clarity) — see `.skills/SKILL.md`. Respect them: route through the HAL
(HalStorage/HalGPIO/HalDisplay), render via UITheme/GUI, use `tr()` for user-facing text, and mind the
~380 KB RAM budget on the ESP32-C3.
