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

### Async embassy executor — and the esp-hal version trade-off
The firmware runs on the **embassy** async executor (`#[esp_hal_embassy::main]`),
with the app loop as one async task and `embassy_time::Timer::after` between input
polls (Step 2 of the goal asks for `embassy-executor` + `embassy-time`).

This forces an esp-hal **version trade-off baked into the goal itself**. Step 2
asks for *both* "latest **stable** esp-hal" *and* embassy — but they are mutually
exclusive: `esp-hal-embassy` (the crate that supplies embassy's time driver +
executor integration) gates on the private `esp-hal/__esp_hal_embassy` feature,
which **exists only up to `esp-hal 1.0.0-rc.0`** and was removed in every stable
release (`1.0.0` … `1.1.1`). So:

- **Embassy build (this branch):** `esp-hal 1.0.0-rc.0` + `esp-hal-embassy 0.9.0`
  — satisfies the async requirement, at the cost of a *pre-release* HAL.
- A **stable** variant (`esp-hal 1.1.1` + a blocking superloop) is preserved in
  this repo's git history; it satisfies "latest stable esp-hal" but cannot use
  embassy. An e-reader is naturally a low-frequency blocking UI (the C++ firmware
  is itself a blocking `Activity::update/render` loop), so both are sound; embassy
  is chosen here because the goal lists it explicitly.

The portable `core` crate is esp-hal-independent, so it is **unchanged** between
the two — only the firmware entry point and the poll delay differ.

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
        ├── main.rs   # #[esp_hal_embassy::main] async entry
        ├── app.rs    # board bring-up (SPI/GPIO/ADC on exact pins) + async app loop
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
| Aurora-style status bar (title + battery + divider) | ✅ from `AuroraTheme::drawHeaderBar` |
| Aurora home: featured card + library list + tab bar + two-zone nav | ✅ (state machine host-tested) |
| Settings screen (line spacing / margin → reader layout) | ✅ (host-tested) |
| Settings persistence to SD (`SETTINGS.CFG`, load on boot / save on exit) | ✅ |
| Battery-latch shutdown (GPIO13) | ✅ |
| Text wrap / pagination | ✅ (host-tested) |
| Reader: paginate + Left/Right page navigation | ✅ (host-tested) |
| Menu widget + Home → Browser → Reader navigation | ✅ (host-tested) |
| Menu widget (owned strings) + 3-screen navigation | ✅ (host-tested) |
| Shared SPI2 bus (display + SD via SpiDevice/RefCellDevice) | ✅ |
| File browser lists the SD root + opens the selected file | ✅ from `SDCardManager.cpp` (needs on-HW speed check) |
| HTML/XHTML → text extraction (in-place) | ✅ (host-tested) |
| DEFLATE decompressor (zero-alloc, output-as-window) | ✅ (host-tested vs flate2) |
| EPUB: ZIP container → inflate chapter → text → paginate | ✅ (host-tested; in-RAM ≤64 KB) |
| Book text + `.bin` font parsing | ✅ scaffold (bounds-checked) |
| `.bin` bitmap-font renderer (variable-width, DrawTarget) | ✅ (host-tested; awaits a font asset) |
| X3 (UC81xx) display driver: init + full LUT bank + full refresh + active-low BUSY | ✅ ported (`Variant::X3`; diff fast/half LUTs deferred) |
| X3 auto-detection (I²C fingerprint) wired into boot → variant select | ✅ from `HalGPIO.cpp` (needs X3 hardware to confirm) |
| Wi-Fi / Calibre wireless | ⛔ **blocked by a constraint conflict** — see note |

> **Wi-Fi vs. the zero-allocation rule.** The only ESP32 Wi-Fi stack, `esp-wifi`
> (0.13, the version compatible with esp-hal 1.1.1), **requires a global
> allocator**: `src/lib.rs:98` is `extern crate alloc;` and its Wi-Fi/BLE/esp-now
> layers use `Box`/`Vec`/`VecDeque` (the `esp-alloc` feature wires the heap).
> That is mutually exclusive with this project's hard "`no_std` **without** global
> allocator" memory policy. EPUB was made genuinely zero-allocation (inflate uses
> the output buffer as its own window); Wi-Fi cannot be, because the dependency
> mandates the heap. Honouring the zero-alloc rule, Wi-Fi is intentionally not
> added. Adding it would mean introducing a global allocator and relaxing that
> policy — a deliberate decision for the project owner, not a silent change.

## Roadmap (next ports, in rough order)
1. SD init bus-speed split (≤400 kHz init, then 40 MHz) if real cards need it;
   scrolling the browser past a screenful of files (paged list).
2. Ship a `.bin` font asset and switch the reader to `glyphfont` (the renderer
   and variable-width `advance` are done — see `core/glyphfont.rs`).
3. EPUB refinements: OPF spine ordering (multi-chapter reading order) and
   streaming large EPUBs from SD instead of the 64 KB in-RAM cap.
4. Persist the last-read position per book (reader settings already persist to
   `SETTINGS.CFG` on the SD card).
4. Deep-sleep wake path (power-button wake; the latch shutdown is done).
5. Persist settings to the SD card (the in-memory Settings screen works); the
   File-Transfer tab (needs Wi-Fi, below).
6. The X3 differential fast/half LUT banks (full refresh is ported; X3 detection
   + driver are wired into boot). X3 battery is via the I²C fuel gauge (the X4
   ADC path is used as a placeholder on X3).
7. Wi-Fi / Calibre wireless — blocked by the zero-alloc rule (esp-wifi needs a
   global allocator); see the note above.
