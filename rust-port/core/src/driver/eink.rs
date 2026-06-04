//! SSD1677 e-ink driver for the Xteink X4 (GDEQ0426T82, 800×480, 1bpp).
//!
//! Ported 1-to-1 from `open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp`
//! (the X4 / SSD1677 path), preserving the exact command sequences:
//! reset → soft-reset → temp-sensor → booster soft-start → driver-output →
//! border → RAM area → auto-clear; and the single-buffer refresh policy
//! (BW plane write → update-control → master-activation → sync RED plane).
//!
//! The driver is generic over `embedded-hal` 1.0 traits so it is decoupled from
//! esp-hal and unit-testable on a host with mock pins/SPI. CS and DC are driven
//! manually as GPIOs (the panel needs CS toggled per command, like the C++).
//!
//! NOTE: only the X4 (SSD1677) path is ported here. The X3 (UC81xx) path in the
//! original uses a large bank of custom LUTs and is intentionally deferred — the
//! goal targets the X4. `RefreshMode` and the public API leave room for it.

use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;
use embedded_hal::delay::DelayNs;
use embedded_hal::digital::{InputPin, OutputPin};
use embedded_hal::spi::SpiDevice;

// ── SSD1677 command set (from EInkDisplay.cpp:8-41) ──────────────────────────
const CMD_SOFT_RESET: u8 = 0x12;
const CMD_BOOSTER_SOFT_START: u8 = 0x0C;
const CMD_DRIVER_OUTPUT_CONTROL: u8 = 0x01;
const CMD_BORDER_WAVEFORM: u8 = 0x3C;
const CMD_TEMP_SENSOR_CONTROL: u8 = 0x18;
const CMD_DATA_ENTRY_MODE: u8 = 0x11;
const CMD_SET_RAM_X_RANGE: u8 = 0x44;
const CMD_SET_RAM_Y_RANGE: u8 = 0x45;
const CMD_SET_RAM_X_COUNTER: u8 = 0x4E;
const CMD_SET_RAM_Y_COUNTER: u8 = 0x4F;
const CMD_WRITE_RAM_BW: u8 = 0x24;
const CMD_WRITE_RAM_RED: u8 = 0x26;
const CMD_AUTO_WRITE_BW_RAM: u8 = 0x46;
const CMD_AUTO_WRITE_RED_RAM: u8 = 0x47;
const CMD_DISPLAY_UPDATE_CTRL1: u8 = 0x21;
const CMD_DISPLAY_UPDATE_CTRL2: u8 = 0x22;
const CMD_MASTER_ACTIVATION: u8 = 0x20;
const CMD_WRITE_TEMP: u8 = 0x1A;
const CMD_DEEP_SLEEP: u8 = 0x10;

const CTRL1_NORMAL: u8 = 0x00; // compare RED vs BW (fast/partial)
const CTRL1_BYPASS_RED: u8 = 0x40; // bypass RED (full refresh)
const TEMP_SENSOR_INTERNAL: u8 = 0x80;

/// Refresh waveform tier (matches the C++ `RefreshMode`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RefreshMode {
    /// Differential, ~fastest. Most page turns.
    Fast,
    /// Stronger differential; periodic ghost cleanup.
    Half,
    /// Full white-baseline refresh; boot/wake/explicit.
    Full,
}

/// SSD1677 driver bound to one SPI **device** (CS managed by the device, so the
/// bus can be shared with the SD card) + the DC/RST/BUSY GPIOs and a delay.
///
/// Each command/data write is its own SpiDevice transaction (CS asserted for the
/// write, released after), which matches the original C++ that toggles CS per
/// `sendCommand`/`sendData`. DC is changed between transactions, while CS is high.
///
/// `fb` is the 1bpp framebuffer (`X4_BUFFER_SIZE` bytes, 0xFF = white). It is
/// borrowed `'static` so the large 48 KB buffer lives in a `static`, never on
/// the stack.
pub struct Eink<SPI, DC, RST, BUSY, DELAY> {
    spi: SPI,
    dc: DC,
    rst: RST,
    busy: BUSY,
    delay: DELAY,
    fb: &'static mut [u8],
    width: u16,
    height: u16,
    screen_on: bool,
}

impl<SPI, DC, RST, BUSY, DELAY> Eink<SPI, DC, RST, BUSY, DELAY>
where
    SPI: SpiDevice<u8>,
    DC: OutputPin,
    RST: OutputPin,
    BUSY: InputPin,
    DELAY: DelayNs,
{
    /// Construct (does not touch the panel — call [`Eink::init`] next).
    pub fn new(
        spi: SPI,
        dc: DC,
        rst: RST,
        busy: BUSY,
        delay: DELAY,
        fb: &'static mut [u8],
        width: u16,
        height: u16,
    ) -> Self {
        Self {
            spi,
            dc,
            rst,
            busy,
            delay,
            fb,
            width,
            height,
            screen_on: false,
        }
    }

    fn buffer_size(&self) -> usize {
        (self.width as usize / 8) * self.height as usize
    }

    // ── Low-level SPI primitives (EInkDisplay.cpp:588-613) ───────────────────
    // The SpiDevice asserts/releases CS around each `write`; DC selects
    // command (low) vs data (high) and is set while CS is idle high.
    fn send_command(&mut self, cmd: u8) {
        let _ = self.dc.set_low(); // command mode
        let _ = self.spi.write(&[cmd]);
    }

    fn send_data(&mut self, data: u8) {
        let _ = self.dc.set_high(); // data mode
        let _ = self.spi.write(&[data]);
    }

    fn send_data_bulk(&mut self, data: &[u8]) {
        let _ = self.dc.set_high();
        let _ = self.spi.write(data);
    }

    /// X4: BUSY is held HIGH while busy, drops LOW when done (EInkDisplay.cpp:550).
    fn wait_while_busy(&mut self) {
        // Bounded poll (~30 s like the C++); each iteration is ~1 ms.
        let mut guard: u32 = 30_000;
        while self.busy.is_high().unwrap_or(false) && guard > 0 {
            self.delay.delay_ms(1);
            guard -= 1;
        }
    }

    /// Hardware reset pulse (EInkDisplay.cpp:527-542).
    fn reset(&mut self) {
        let _ = self.rst.set_high();
        self.delay.delay_ms(20);
        let _ = self.rst.set_low();
        self.delay.delay_ms(2);
        let _ = self.rst.set_high();
        self.delay.delay_ms(20);
    }

    /// Full power-up + SSD1677 controller init (EInkDisplay.cpp:751-860).
    pub fn init(&mut self) {
        self.reset();

        self.send_command(CMD_SOFT_RESET);
        self.wait_while_busy();

        self.send_command(CMD_TEMP_SENSOR_CONTROL);
        self.send_data(TEMP_SENSOR_INTERNAL);

        // Booster soft-start (GDEQ0426T82-specific values).
        self.send_command(CMD_BOOSTER_SOFT_START);
        for b in [0xAE, 0xC7, 0xC3, 0xC0, 0x40] {
            self.send_data(b);
        }

        // Driver output control: height + scan direction.
        self.send_command(CMD_DRIVER_OUTPUT_CONTROL);
        let gates = self.height - 1;
        self.send_data((gates % 256) as u8);
        self.send_data((gates / 256) as u8);
        self.send_data(0x02); // SM=1 (interlaced), TB=0

        self.send_command(CMD_BORDER_WAVEFORM);
        self.send_data(0x01);

        self.set_ram_area(0, 0, self.width, self.height);

        // Auto-clear both planes to white.
        self.send_command(CMD_AUTO_WRITE_BW_RAM);
        self.send_data(0xF7);
        self.wait_while_busy();
        self.send_command(CMD_AUTO_WRITE_RED_RAM);
        self.send_data(0xF7);
        self.wait_while_busy();

        // Match begin()'s framebuffer baseline.
        for b in self.fb.iter_mut() {
            *b = 0xFF;
        }
        self.screen_on = false;
    }

    /// Configure the RAM window for subsequent writes (EInkDisplay.cpp:862-896).
    /// Y is reversed because the panel gates are flipped.
    fn set_ram_area(&mut self, x: u16, y: u16, w: u16, h: u16) {
        const DATA_ENTRY_X_INC_Y_DEC: u8 = 0x01;
        let y = self.height - y - h;

        self.send_command(CMD_DATA_ENTRY_MODE);
        self.send_data(DATA_ENTRY_X_INC_Y_DEC);

        self.send_command(CMD_SET_RAM_X_RANGE);
        self.send_data((x % 256) as u8);
        self.send_data((x / 256) as u8);
        self.send_data(((x + w - 1) % 256) as u8);
        self.send_data(((x + w - 1) / 256) as u8);

        self.send_command(CMD_SET_RAM_Y_RANGE);
        self.send_data(((y + h - 1) % 256) as u8);
        self.send_data(((y + h - 1) / 256) as u8);
        self.send_data((y % 256) as u8);
        self.send_data((y / 256) as u8);

        self.send_command(CMD_SET_RAM_X_COUNTER);
        self.send_data((x % 256) as u8);
        self.send_data((x / 256) as u8);

        self.send_command(CMD_SET_RAM_Y_COUNTER);
        self.send_data(((y + h - 1) % 256) as u8);
        self.send_data(((y + h - 1) / 256) as u8);
    }

    fn write_ram(&mut self, ram_cmd: u8) {
        self.send_command(ram_cmd);
        // Bulk plane write. `self.spi.write` only borrows spi/fb disjointly, so a
        // direct slice of the framebuffer is fine.
        let len = self.buffer_size();
        let _ = self.dc.set_high();
        let _ = self.spi.write(&self.fb[..len]);
    }

    /// Push the framebuffer to the panel and refresh
    /// (single-buffer policy, EInkDisplay.cpp:1433-1464 + refreshDisplay).
    pub fn display(&mut self, mode: RefreshMode, turn_off: bool) {
        // Waking from off forces a stronger (Half) waveform.
        let mode = if !self.screen_on && !turn_off {
            RefreshMode::Half
        } else {
            mode
        };

        self.set_ram_area(0, 0, self.width, self.height);

        if mode != RefreshMode::Fast {
            self.write_ram(CMD_WRITE_RAM_BW);
            self.write_ram(CMD_WRITE_RAM_RED);
        } else {
            // Fast: BW only; RED already holds the previous frame.
            self.write_ram(CMD_WRITE_RAM_BW);
        }

        self.refresh(mode, turn_off);

        // Single-buffer: sync RED with the just-shown frame for the next diff.
        self.set_ram_area(0, 0, self.width, self.height);
        self.write_ram(CMD_WRITE_RAM_RED);
    }

    /// Update-control + master-activation sequence (EInkDisplay.cpp:1656-1722).
    fn refresh(&mut self, mode: RefreshMode, turn_off: bool) {
        self.send_command(CMD_DISPLAY_UPDATE_CTRL1);
        self.send_data(if mode == RefreshMode::Fast {
            CTRL1_NORMAL
        } else {
            CTRL1_BYPASS_RED
        });

        let mut display_mode: u8 = 0x00;
        if !self.screen_on {
            self.screen_on = true;
            display_mode |= 0xC0; // CLOCK_ON | ANALOG_ON
        }
        if turn_off {
            self.screen_on = false;
            display_mode |= 0x03; // ANALOG_OFF_PHASE | CLOCK_OFF
        }
        match mode {
            RefreshMode::Full => display_mode |= 0x34,
            RefreshMode::Half => {
                self.send_command(CMD_WRITE_TEMP);
                self.send_data(0x5A); // high temp → faster
                display_mode |= 0xD4;
            }
            RefreshMode::Fast => display_mode |= 0x1C,
        }

        self.send_command(CMD_DISPLAY_UPDATE_CTRL2);
        self.send_data(display_mode);
        self.send_command(CMD_MASTER_ACTIVATION);
        self.wait_while_busy();
    }

    /// Power the analog rails down and enter controller deep sleep
    /// (EInkDisplay.cpp:1759-1785).
    pub fn deep_sleep(&mut self) {
        if self.screen_on {
            self.send_command(CMD_DISPLAY_UPDATE_CTRL1);
            self.send_data(CTRL1_BYPASS_RED);
            self.send_command(CMD_DISPLAY_UPDATE_CTRL2);
            self.send_data(0x03);
            self.send_command(CMD_MASTER_ACTIVATION);
            self.wait_while_busy();
            self.screen_on = false;
        }
        self.send_command(CMD_DEEP_SLEEP);
        self.send_data(0x01);
    }

    /// Fill the framebuffer (0xFF = white, 0x00 = black).
    pub fn clear(&mut self, white: bool) {
        let fill = if white { 0xFF } else { 0x00 };
        for b in self.fb.iter_mut() {
            *b = fill;
        }
    }
}

// ── embedded-graphics integration ────────────────────────────────────────────

impl<SPI, DC, RST, BUSY, DELAY> OriginDimensions for Eink<SPI, DC, RST, BUSY, DELAY> {
    fn size(&self) -> Size {
        Size::new(self.width as u32, self.height as u32)
    }
}

impl<SPI, DC, RST, BUSY, DELAY> DrawTarget for Eink<SPI, DC, RST, BUSY, DELAY> {
    type Color = BinaryColor;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        let row_bytes = self.width as usize / 8;
        let w = self.width as i32;
        let h = self.height as i32;
        for Pixel(coord, color) in pixels {
            let (x, y) = (coord.x, coord.y);
            if x < 0 || y < 0 || x >= w || y >= h {
                continue;
            }
            let idx = y as usize * row_bytes + (x as usize / 8);
            let bit = 7 - (x as usize % 8);
            match color {
                // 0 = black, 1 = white. On = foreground = black.
                BinaryColor::On => self.fb[idx] &= !(1 << bit),
                BinaryColor::Off => self.fb[idx] |= 1 << bit,
            }
        }
        Ok(())
    }
}
