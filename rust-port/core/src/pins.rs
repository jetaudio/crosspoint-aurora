//! Exact GPIO pin map for the Xteink X4 (and X3 variant).
//!
//! These numbers are transcribed verbatim from the original C++ firmware —
//! see `discovered_pins.md` at the repo root for the source `file:line` of every
//! one. DO NOT change these without checking the hardware: GPIO13 is a battery
//! latch and a wrong drive can keep the board powered or block shutdown.

// ── E-ink display SPI (custom pins, not the C3 hardware-SPI defaults) ─────────
pub const EPD_SCLK: u8 = 8; // SPI clock
pub const EPD_MOSI: u8 = 10; // SPI data out
pub const EPD_MISO: u8 = 7; // SPI data in (shared with SD card)
pub const EPD_CS: u8 = 21; // display chip select
pub const EPD_DC: u8 = 4; // data/command
pub const EPD_RST: u8 = 5; // reset
pub const EPD_BUSY: u8 = 6; // busy (input)

// ── Buttons ──────────────────────────────────────────────────────────────────
// Front buttons are analog, multiplexed on two ADC pins via a resistor ladder.
pub const BTN_ADC_1: u8 = 1; // group 1: Back / Confirm / Left / Right
pub const BTN_ADC_2: u8 = 2; // group 2: Up / Down
pub const POWER_BTN: u8 = 3; // digital, INPUT_PULLUP, pressed = LOW

// ── Power management ─────────────────────────────────────────────────────────
pub const BAT_LATCH: u8 = 13; // battery latch MOSFET — drive LOW + hold for sleep
pub const BAT_ADC: u8 = 0; // battery voltage (X4), divider ×2.0
pub const USB_DETECT: u8 = 20; // UART0_RXD, HIGH when USB present (X4)

// ── SD card (shares the display SPI bus) ─────────────────────────────────────
pub const SD_CS: u8 = 12;

// ── X3-only I²C (fuel gauge / RTC / IMU); unused on X4 ───────────────────────
pub const X3_I2C_SDA: u8 = 20;
pub const X3_I2C_SCL: u8 = 0;

// ── SPI / display parameters ─────────────────────────────────────────────────
pub const SPI_FREQ_X4_HZ: u32 = 40_000_000;
pub const SPI_FREQ_X3_HZ: u32 = 16_000_000;

// SSD1677 panel on X4: 800×480, 1 bit/pixel.
pub const X4_WIDTH: u16 = 800;
pub const X4_HEIGHT: u16 = 480;
/// One bit per pixel → 800/8 * 480 = 48000 bytes per plane.
pub const X4_BUFFER_SIZE: usize = (X4_WIDTH as usize / 8) * X4_HEIGHT as usize;

// UC81xx panel on X3: 792×528.
pub const X3_WIDTH: u16 = 792;
pub const X3_HEIGHT: u16 = 528;
/// X3 plane size = 792/8 * 528 = 52272 bytes (larger than the X4's 48000).
pub const X3_BUFFER_SIZE: usize = (X3_WIDTH as usize / 8) * X3_HEIGHT as usize;

/// The framebuffer must hold the larger of the two panels' planes.
pub const MAX_BUFFER_SIZE: usize = if X3_BUFFER_SIZE > X4_BUFFER_SIZE {
    X3_BUFFER_SIZE
} else {
    X4_BUFFER_SIZE
};

/// Logical button indices, matching the C++ `HalGPIO::BTN_*` constants.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Button {
    Back = 0,
    Confirm = 1,
    Left = 2,
    Right = 3,
    Up = 4,
    Down = 5,
    Power = 6,
}
