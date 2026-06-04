# CrossPoint Reader — Rust `no_std` port (ESP32-C3 / Xteink X4)

A ground-up Rust port of the CrossPoint e-reader firmware, targeting the
ESP32-C3 (RISC-V, `riscv32imc-unknown-none-elf`) on the Xteink **X4**. This is an
in-progress port that **compiles to a clean, flashable production binary today**
and lays down the hardware-faithful foundation (pins, SPI, SSD1677 driver,
input, power) plus the portable reader logic (parsers, text layout).

> Status: boots, initialises the SSD1677 panel, renders a screen via
> `embedded-graphics`, and runs a button-driven superloop. The full reader/UI
> (the bulk of the C++ activities) is not yet ported — see **Roadmap**.

## Why this exists

The original firmware is C++ on Arduino/PlatformIO. This port re-implements it in
safe Rust `no_std` with zero heap allocation, matching the original's exact GPIO
map so it is safe to flash on real hardware.

## Architecture decisions

### Blocking superloop, **not** async/embassy
The goal brief suggested `embassy`. We deliberately **do not** use it:

- The only published `esp-hal-embassy` (0.9.1) enables the private esp-hal
  feature `__esp_hal_embassy`, which **exists only up to `esp-hal 1.0.0-rc.0`** —
  it was removed in every stable release (`1.0.0` … `1.1.1`). So pulling embassy
  forces the firmware onto a *pre-release* HAL. (Verified against the crates.io
  index: `esp-hal-embassy 0.9.1 → esp-hal "^1.0.0-rc.0" + feature
  "requires-unstable"`, and `executors → esp-hal/__esp_hal_embassy`, last present
  in `esp-hal 1.0.0-rc.0`.)
- An e-reader is intrinsically a low-frequency, blocking UI device: wait for a
  button → lay out a page → push a slow (~hundreds of ms) e-ink refresh. The C++
  firmware itself is a blocking `Activity::update/render` loop, not async.
- A blocking design uses less RAM (no async executor arena), which matters on the
  C3's ~384 KB.

So we build on **`esp-hal 1.1.1` stable** with a blocking superloop.

### Workspace split: `core` (testable) + `firmware` (hardware)
```
rust-port/
├── core/        # pure no_std, NO esp-hal → builds + unit-tests on the host
│   └── src/
│       ├── pins.rs        # exact GPIO map (see ../../discovered_pins.md)
│       ├── driver/eink.rs # SSD1677 driver over embedded-hal traits + DrawTarget
│       ├── parser/        # book text + .bin font parsing (bounds-checked slices)
│       ├── layout/        # word-wrap + pagination (heapless, zero-alloc)
│       └── input.rs       # ADC button decode + edge debounce
└── firmware/    # the esp32-c3 binary
    └── src/
        ├── main.rs   # esp_hal::init + app::run
        ├── app.rs    # board bring-up (SPI/GPIO/ADC on exact pins) + superloop
        └── power.rs  # battery-latch (GPIO13) shutdown
```
Keeping the driver generic over `embedded-hal` traits means the whole `core`
crate — including the e-ink command sequences — compiles and is unit-tested on a
PC with no hardware.

## Memory

Zero global allocator. All buffers are static or `heapless`:
- Framebuffer: one 48 KB plane (`800/8 × 480`), zero-init in `.bss`.
- Static RAM footprint ≈ **50 KB** (`.data` + `.bss`), well under 384 KB.
- Flash (`.text`) ≈ 379 KB; app image ≈ 100 KB (2.4 % of the partition).

## Build

Prereqs: stable Rust with the RISC-V target (no nightly, no espup — the C3 is a
RISC-V core):
```bash
rustup target add riscv32imc-unknown-none-elf
```

Build the firmware (default target comes from `.cargo/config.toml`):
```bash
cargo build --release -p crosspoint-fw
```

Run the host unit tests for the ported logic (override the embedded target):
```bash
cargo test -p crosspoint-core --target x86_64-pc-windows-msvc   # or your host triple
```

## Flash

```bash
cargo install espflash
# Flash + monitor over USB (this is the `runner` in .cargo/config.toml):
cargo run --release -p crosspoint-fw
# …or produce standalone images:
espflash save-image --chip esp32c3 \
  target/riscv32imc-unknown-none-elf/release/crosspoint-fw crosspoint-fw.bin
espflash save-image --chip esp32c3 --merge \
  target/riscv32imc-unknown-none-elf/release/crosspoint-fw crosspoint-fw-merged.bin
```

## Hardware safety

Every GPIO is transcribed from the C++ source into `../discovered_pins.md`
(with `file:line` provenance) — **never guessed**. Highlights:
- **GPIO13** = battery-latch MOSFET. Untouched at runtime; only driven LOW (to
  power off) in `power.rs`. A stray drive can keep the board powered or block
  shutdown.
- Display SPI: SCLK=8, MOSI=10, MISO=7, CS=21, DC=4, RST=5, BUSY=6; Mode 0,
  MSB-first, 40 MHz. BUSY is active-HIGH on the X4 (SSD1677).
- Front buttons are **analog** (ADC GPIO1/GPIO2, decoded by voltage band);
  power button is digital GPIO3, active-low.

## What's ported vs. the C++ original

| Area | Status |
|------|--------|
| GPIO/pin map | ✅ exact, verified from source |
| SSD1677 (X4) init / RAM area / refresh / deep-sleep | ✅ 1-to-1 from `EInkDisplay.cpp` |
| `embedded-graphics` `DrawTarget` | ✅ |
| Front-button ADC decode + debounce | ✅ thresholds from `InputManager.cpp` |
| Battery % curve + status-bar read (GPIO0, calibrated) | ✅ from `BatteryMonitor.cpp` (host-tested) |
| Battery-latch shutdown (GPIO13) | ✅ |
| Text wrap / pagination | ✅ (host-tested) |
| Reader: paginate + Left/Right page navigation | ✅ (host-tested) |
| Menu widget + Home → Browser → Reader navigation | ✅ (host-tested) |
| Menu widget (owned strings) + 3-screen navigation | ✅ (host-tested) |
| Shared SPI2 bus (display + SD via SpiDevice/RefCellDevice) | ✅ |
| File browser lists the SD root + opens the selected file | ✅ from `SDCardManager.cpp` (needs on-HW speed check) |
| Book text + `.bin` font parsing | ✅ scaffold (bounds-checked) |
| X3 (UC81xx) display path | ⛔ deferred (huge custom-LUT path; X4 is the target) |
| EPUB/HTML parsing, SD/FAT, fonts on disk, file browser, Wi-Fi, settings | ⛔ roadmap |

## Roadmap (next ports, in rough order)
1. SD init bus-speed split (≤400 kHz init, then 40 MHz) if real cards need it;
   scrolling the browser past a screenful of files (paged list).
2. On-disk bitmap fonts → wire `parser::font` into a real glyph renderer (and a
   variable-width `advance` so wrapping matches proportional fonts).
3. Persist the last-read position per book.
4. Deep-sleep wake path (power-button wake; the latch shutdown is done).
5. Port the Aurora home layout (featured card + tab bar).
6. X3 variant (UC81xx) display path.
