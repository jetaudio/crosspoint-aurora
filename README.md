# CrossPoint Reader

[![Fund contributors](https://img.shields.io/badge/%F0%9F%91%91_Fund_contributors-royalty.dev-BB953A?style=for-the-badge&labelColor=1a1a1a)](https://app.royalty.dev/crosspoint-reader/crosspoint-reader)

## 🌅 Aurora — a personal fork

> You are looking at the **`aurora`** branch of [jetaudio/crosspoint-aurora](https://github.com/jetaudio/crosspoint-aurora), a personal fork of
> [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader). It adds a **LilyGo T5 S3 Pro / Pro Lite** target, a
> touch-first UI, fully configurable buttons, and complete **Vietnamese** support. `master` here mirrors upstream untouched — every
> change below lives on `aurora`, and firmware updates come from this repository's releases, not upstream's.
>
> *Bản fork cá nhân: hỗ trợ máy LilyGo T5 S3 Pro / Pro Lite, giao diện cảm ứng, phím bấm cấu hình được, và tiếng Việt trọn vẹn.*

### Everything this fork adds over upstream

**New hardware target**

- **LilyGo T5 S3 Pro / Pro Lite** (`pio run -e lilygo`, or `-e lilygo_pro` for the LoRa/GPS-fitted Pro) — ESP32-S3 driving the
  4.7" 960×540 16-gray panel over the S3's i80 LCD peripheral (no on-glass controller), through a TPS65185 PMIC and a PCA9535
  expander, with GT911 touch, PCF8563 RTC, BQ27220 fuel gauge + BQ25896 charger, the frontlight on GPIO11, and the expander key
  on the case (IO48). Brought up and verified on real hardware.
- **Panel waveform from the vendor tables**, temperature-ranged, with anti-aliasing greys carried in the fast bank so text edges
  are smoothed without the mid-grey flash a clean refresh costs.
- **Three solderable key pads** on the Pro Lite, where the LoRa module would sit (GPIO10 / GPIO1 / GPIO47). The fourth pad,
  GPIO46, is *not* free — the i80 bus holds it as its DC pin — and the firmware says so rather than offering a key that would
  fight the LCD peripheral for the pad.
- **Two separate release builds**, one per variant, each carrying its own board tag so a device is only ever offered the
  firmware that matches it.
- **Web flasher** — [jetaudio.github.io/crosspoint-aurora](https://jetaudio.github.io/crosspoint-aurora/) installs either
  variant straight from the browser (ESP Web Tools), with a separate button for each board.
- **Serial debug harness** — `scripts/serial_screen_capture.py` pulls the framebuffer as a PNG and injects key presses, taps,
  long-presses and swipes over USB serial, so the UI can be exercised and diffed without touching the device. Alongside it:
  `CMD:IDLE` (what is holding the inactivity clock open and which guard is refusing light sleep, per key, with the raw pad level
  beside the debounced verdict), `CMD:DSLEEP:<sec>[:held]` (drive the real deep-sleep path and come back on a timer — the one
  thing a bench otherwise cannot do, since a physical button is the only way in and out and the console dies on the way down),
  `CMD:BATTLOG`, `CMD:GPIO`, `CMD:GET`/`CMD:SET` for any setting, and `CMD:BATTSIM` to exercise the low-battery paths on a full
  pack.

**Touch-first UI**

- **Control Center** — an iOS-style sheet pulled from the top edge (or a status-bar tap): brightness slider with real **−/+**
  buttons, a lamp on/off button, **1% steps** with 1% as the floor, plus quick tiles for night mode, ghost-cleanup refresh,
  reading orientation, the touch kill-switch, a screenshot, and sleep. **Which tiles it shows is a setting** (Settings → Display →
  Customise Control Center); the frontlight row always stays.
- **Touch kill-switch** — turn the digitiser off for reading with a palm on the glass, from a tile or a key.
- **Full touch routing for the Aurora chrome** — taps, drags and swipes across home, settings, the reader and every popup.
- **Pure 1-bit chrome** — the tab bar, option popups and control center drop their dithered-gray washes for solid/outlined shapes,
  which is what an e-ink panel actually renders cleanly.

**Configurable buttons**

- **Every key gets its own tap and hold action**, chosen from one shared list: nothing, next/previous page, back, home, reader
  menu, control center, night mode, refresh, frontlight, touch on/off, sleep. Covers the capacitive **Home** key, the **IO48**
  expander key (which previously did nothing useful), **BOOT**, and any key soldered to the spare pads.
- **Key actions live on their own screen** (Settings → Controls → Key actions). A dozen picker rows used to crowd out the
  handful of settings people actually change; the sub-screen names the way *back* in its header rather than naming itself,
  because the screen you are on is the one you can see.
- Keys go through the HAL like any other board key, so pull-ups, debounce, press/release edges, wake-from-light-sleep and
  counting as user activity all come for free. A pin polled by hand outside the HAL gets none of that — GPIO10 spent a version
  doing exactly that, and paid with ~600 ms of tap latency and a device that slept while its owner was reading.
- Two-zone navigation for button-only devices, plus a three-way **button hints** setting (off / front only / front + edge).

**Vietnamese**

- Complete **Vietnamese UI translation**, and **UI fonts regenerated from Noto Sans** so every diacritic is covered.
- Selectable **system font**: Noto Sans or Ubuntu (Vietnamese).
- **NFC normalisation** of EPUB text, so books stored as NFD still render their tone marks correctly.

**Reader and typography**

- **Drop caps** — an enlarged decorative initial on chapter openings, with **selectable drop-cap faces** loaded from the SD card.
- **Small caps** — the opening line of a chapter set in all-caps.
- **Redesigned reader** — clean page plus an overlay toolbar (Contents / Text / More), font changes applied in place with no book
  reopen, hold-to-jump in the panel lists, and overlays that open on a HALF refresh so no gray ghost is left behind.
- **Aurora home screen** — slim status bar, a "Continue Reading" hero card with cover and progress, card-style recent books, and a
  persistent bottom icon tab bar. Night mode inverts the whole UI, not just the page.
- **Screen orientation, not reading orientation** — rotating used to turn the reader and nothing else, so a device held sideways
  had a sideways book and an upright library. Orientation is now the screen's: applied at boot before the first frame, so it
  survives a reset, a battery pull and a deep sleep alike, and leaving a book no longer un-rotates the device. A four-way picker
  (Portrait / Landscape / Portrait 180° / Landscape reversed) replaces stepping a quarter turn per tap, and touch follows the
  pixels everywhere.
- **Clock** — PCF8563 RTC on the T5 S3, shown in the status bar (12/24 h, either side), with a UTC offset picker and NTP sync.

**Power and battery**

- **Battery Monitor** (Settings → System) — where the charge went since the cable came out. The gauge sits in series with the
  pack, so the only measurable number is the *total*; the split across deep sleep, awake, light sleep, frontlight, CPU clock,
  Wi-Fi and screen refreshes is an estimate from bench coefficients, and the leftover is shown as its own signed row rather than
  spread around to make the columns add up. A breakdown whose residual is hidden is one that cannot be caught being wrong.
- **Light sleep between page turns** — a page held on screen mid-book is the only state that is genuinely idle for minutes, so
  that is the only place the CPU is halted. Every key, the touch INT and the expander INT stay armed as wake sources.
- **Battery telemetry log** — `/.crosspoint/battery.csv`, one row every five minutes plus one at every boot and sleep, carrying
  the gauge reading alongside the state the device was in. `scripts/fetch_battery_log.py` pulls it over serial and
  `scripts/analyze_battery_log.py` fits per-component costs offline — and reports the conditioning and the noise floor rather
  than a confident-looking number, because two loads that are always on together cannot be separated by any amount of data.
- **Sleep that actually sleeps.** Three faults lived on the deep-sleep path, all invisible to the log because it runs after the
  card is unmounted and the console is down: an unbounded power-button release poll that parked the device in a ~25 mA idle loop
  looking exactly like a real sleep (two nights cost 240 mAh and 110 mAh); a panel PMIC that was never unconditionally powered
  down, so one silent I2C failure left it drawing all night while the *awake* current still read normal; and a key mapped to a
  pad the LCD peripheral owns, which read permanently pressed and — since a bound key counts as user activity — reset the
  inactivity clock every loop pass, killing idle light sleep and the auto-sleep timeout together.
- **Sleep findings survive the sleep.** The log gained `dsleep_ms`, `stall_ms`, `stalls`, `park`, `rst` and `wake`: how long the
  device was really away (from the wall clock, the only one that survives deep sleep), how much of that was spent awake inside
  the sleep path, whether the panel PMIC was verified off, and the raw reset/wake causes — which separate a clean button wake
  from a crash on the way down and from a brownout. "It showed the sleep screen and then restarted" used to be any of the three.
- **Low-battery protection and a charging indicator**, the latter read from the charger IC on boards with no USB-detect pin.

**Library and system**

- **File browser context menu** (long-press): rename and delete.
- **SD-card fonts and per-family drop-cap faces** — reader families and their decorative initials are loaded from the card, so a
  drop cap matches the face it opens.
- **~300 KB of flash reclaimed** by compiling hyphenation only for the languages actually shipped (en/fr/es/it).
- **Fast wake** — boot no longer blocks waiting for the power button to be released, so the UI draws immediately.
- **OTA from this fork** — update checks read this repository's releases (asset `firmware-lilygo.bin` for the T5 S3, `firmware.bin`
  for the C3 X4/X3), so an update never overwrites Aurora with stock CrossPoint.

> 📌 The Vietnamese translation and fonts have been contributed back upstream. The rest (Aurora theme, touch UI, configurable
> buttons, LilyGo target, drop caps) continues to live on this branch.

---

CrossPoint is open-source e-reader firmware - community-built, fully hackable, free forever. It's maintained by a growing community of developers and readers who believe your device should do what you want - not what a manufacturer decided for you.

**Now running on:** ESP32C3-based Xteink [X4](https://www.xteink.com/products/xteink-x4) and [X3](https://www.xteink.com/products/xteink-x3).

![CrossPoint Reader running on Xteink device](./docs/images/cover.jpg)

> If you're planning to buy an Xteink device, consider purchasing an **X3/X4 Developer Edition** through https://crosspointreader.com. CrossPoint receives a small share of each sale, helping fund development costs.

---

## What can CrossPoint do?

- **Reader engine**: EPUB 2/3 rendering with embedded-style option, image handling, hyphenation, kerning, chapter navigation, footnotes, bookmarks, dictionary lookups ([StarDict](docs/dictionary.md)), go-to-percent, auto page turn, orientation control, focus reading, KOReader progress sync and more. 

- **Various formats**: native handling for `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`.

- **Screenshots.**

- **Custom fonts**: install your favorite fonts on the SD card.

- **Tilt page turn (X3 only)**.

- **Library workflow**: folder browser, hidden-file toggle, long-press delete, recent books, SD-cache management.

- **Wireless workflows**:
  
  - File transfer web UI
  - EPUB Optimizer
  - Web settings UI/API (edit many device settings from browser)
  - WebSocket fast uploads
  - WebDAV handler
  - AP mode (hotspot) and STA mode (join existing Wi-Fi), both with QR helpers
  - Calibre wireless connect flow
  - OPDS browser with saved servers (up to 8), search, pagination, and direct download
  - OTA update checks and installs from GitHub releases

- **Customization**: multiple themes (Classic, Lyra, Lyra Extended, RoundedRaff), sleep screen modes including transparent overlays, front/side button remapping, status bar controls, power-button behavior, refresh cadence, and more.

- **Localization**: 24 UI languages and counting. RTL support.

### Coming soon:

- More themes.

- Much more! stay tuned.

---

## USB-locked devices (Xteink Unlocker)

Some Xteink units purchased from third-party stores (e.g. AliExpress) ship with USB flashing locked from the factory.
If your device is locked, you will need to use the **Xteink Unlocker** tool available at
https://crosspointreader.com/#unlock-tool before you can flash CrossPoint.

**You do not need this tool if you bought your device directly from xteink.com.** Those units are not locked.

**Not sure if your device is locked?** Power it on, connect the USB-C cable, and try flashing via the web flasher first (see
[Install firmware](#install-firmware) below). If the browser's serial device picker does not show your device, try a different
USB port or browser before assuming the device is locked. Only reach for the unlocker if the device still doesn't appear.

> ### ⚠️ WARNING: READ THIS BEFORE USING THE UNLOCKER ⚠️
> 
> **The only officially supported firmwares in the unlock tool are CrossPoint and CrossInk.**
> 
> Flashing any other firmware on a USB-locked device may **permanently brick the device** or leave it **permanently
> stuck on that firmware with no recovery path**. Once USB flashing is re-locked, your only way back is via OTA, and if
> the firmware you flashed doesn't support OTA, **there is no way out**.

## Install firmware

### Web installer (recommended)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Go to https://crosspointreader.com/#flash-tools, select device (X3 or X4), and choose an official CrossPoint release.

### Web installer (specific version)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Download a `firmware.bin` from [Releases](https://github.com/crosspoint-reader/crosspoint-reader/releases), local build, or continuous integration artifact.
3. Go to https://crosspointreader.com/#flash-tools, select device (X3 or X4), click "Custom .bin" and upload a `firmware.bin`.

### Revert to Official Firmware

To revert to the official firmware, you can also flash the latest official firmware using https://crosspointreader.com/#flash-tools.

### Command line

1. Install [`esptool`](https://github.com/espressif/esptool):

```bash
pip install esptool
```

2. Download `firmware.bin` from the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases).
3. Connect your device via USB-C.
4. Find the device port. On Linux, run `dmesg` after connecting. On macOS:

```bash
log stream --predicate 'subsystem == "com.apple.iokit"' --info
```

5. Flash:

```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Adjust `/dev/ttyACM0` to match your system.

### Manual

See [Development quick start](#development-quick-start) below.

---

## Custom SD-card fonts

Convert your own TTF/OTF files into `.cpfont` files that load from the SD card. No firmware reflash is needed.

1. Go to https://crosspointreader.com/fonts and open the "SD-card font builder" form.
2. Upload up to four styles (regular, bold, italic, bold-italic), set the family name, point sizes, and Unicode range.
3. Download the generated `.cpfont` files.
4. Copy them to your SD card under `/fonts/YourFont/` (or `/.fonts/YourFont/` to hide the folder).
5. Select the font on the device from the font settings.

Conversion runs the firmware repo's `lib/EpdFont/scripts/fontconvert_sdcard.py` script unmodified, so output matches a local host build.

---

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Project scope](./SCOPE.md)
- [Contributing docs](./docs/contributing/README.md)
- [Touch and UI development](./docs/contributing/touch-and-ui.md) - how to build new screens on the FreeInkUI activity bases (UiListActivity and friends), plus build envs for the non-Xteink touch devices

---

## Development quick start

### Prerequisites

- [pioarduino](https://github.com/pioarduino/pioarduino) or VS Code + pioarduino plugin
- Python 3.8+
- `clang-format` 21
- USB-C cable supporting data transfer

### Setup

```bash
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader
cd crosspoint-reader

# if cloned without --recursive:
git submodule update --init --recursive
```

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash a connected ESP32-C3 device, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Build / flash / monitor

```bash
pio run --target upload
```

### Contributor pre-PR checks

```bash
./bin/clang-format-fix
pio check -e default
pio run -e default
```

### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```

After that run the script:

```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

Minor adjustments may be required for Windows.

---

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:

```text
.crosspoint/
├── epub_<hash>/         # one directory per book, named by content hash
│   ├── progress.bin     # reading position (chapter, page, etc.)
│   ├── cover.bmp        # generated cover image
│   ├── book.bin         # metadata: title, author, spine, TOC
│   ├── css_rules.cache  # parsed CSS rule cache
│   ├── img_*            # rendered image cache files
│   └── sections/        # per-chapter layout cache
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Removing `/.crosspoint` clears all cached metadata and forces a full regeneration on next open. Book deletes, overwrites, and moves done through the firmware or web UI clear or re-key matching caches; manual SD-card edits may leave stale cache directories behind.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

---

## Contributing

Contributions are welcome. If you're new to the codebase, start with the [contributing docs](./docs/contributing/README.md). For things to work on, check the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) — leave a comment before starting so we don't duplicate effort.

Everyone here is a volunteer, so please be respectful and patient. For governance and community expectations, see [GOVERNANCE.md](./GOVERNANCE.md).

---

## Community forks

One of the best things about open source is that anyone can take the code in a different direction. If you need something outside CrossPoint's [scope](./SCOPE.md), check out the community forks:

- [CrossInk](https://github.com/uxjulia/CrossInk) — Typography and reading tracking: Bionic Reading (bolds word stems to create fixation points), guide dots between words, improved paragraph indents, and replaces the default fonts with ChareInk/Lexend/Bitter.

- [papyrix-reader](https://github.com/bigbag/papyrix-reader) — Adds FB2 and MD format support. Actively maintained with Arabic script support. Custom themes via SD card.

- ~~[crosspet](https://github.com/trilwu/crosspet) — A Vietnamese fork that adds a Tamagotchi-style virtual chicken that grows based on your reading milestones (pages read, streaks, care). Also: Flashcards, Weather, Pomodoro timer, and mini-games.~~ (Unmaintained)

- [crosspoint-reader-cjk](https://github.com/aBER0724/crosspoint-reader-cjk) — Purpose-built for Chinese, Japanese, and Korean reading.

- [inx](https://github.com/obijuankenobiii/inx) — Completely reimagines the user interface with tabbed navigation.

- ~~[PlusPoint](https://github.com/ngxson/pluspoint-reader) — custom JS apps support.~~ (Unmaintained)

- [crosspoint-reader-papers3](https://github.com/juicecultus/crosspoint-reader-papers3) — Crosspoint port for M5Stack Paper S3. 

- [t5s3-reader](https://github.com/ShallowGreen123/t5s3-reader) — Crosspoint port for LilyGo T5 ePaper S3 / T5S3 4.7-inch e-paper device.

**Note:** Many of these features will make their way into CrossPoint over time. We maintain a slower pace to ensure rock-solid stability and squash bugs before they reach your device.

Want to build your own device? Be sure to check out the [de-link](https://github.com/iandchasse/de-link) project.

---

CrossPoint Reader is **not affiliated with Xteink or any device manufacturer**.

Huge shoutout to [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader), which inspired this project.
