//! Application entry: board bring-up + the blocking superloop.
//!
//! Mirrors the C++ firmware's model: initialise the HAL, render a screen, then
//! loop polling input and re-rendering on demand. All hardware wiring uses the
//! exact pins from `discovered_pins.md`.

use embedded_graphics::mono_font::ascii::{FONT_10X20, FONT_6X10};
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;
use embedded_graphics::text::Text;
use embedded_hal::delay::DelayNs;

use esp_hal::analog::adc::{Adc, AdcConfig, Attenuation};
use esp_hal::delay::Delay;
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::peripherals::Peripherals;
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::spi::Mode;
use esp_hal::time::Rate;

use crosspoint_core::driver::{Eink, RefreshMode};
use crosspoint_core::input::{decode_group1, decode_group2, ButtonState};
use crosspoint_core::pins::{self, Button};

/// 1bpp framebuffer for the SSD1677 (48 KB). Zero-initialised so it lands in
/// `.bss` (no 48 KB of flash init data); `Eink::init` fills it with 0xFF before
/// the first draw, and every render clears it first.
static mut FRAMEBUFFER: [u8; pins::X4_BUFFER_SIZE] = [0; pins::X4_BUFFER_SIZE];

/// Bring up the board and run the superloop forever.
pub fn run(p: Peripherals) -> ! {
    let delay = Delay::new();

    // ── Display control GPIOs (exact pins) ───────────────────────────────────
    let dc = Output::new(p.GPIO4, Level::Low, OutputConfig::default());
    let cs = Output::new(p.GPIO21, Level::High, OutputConfig::default());
    let rst = Output::new(p.GPIO5, Level::High, OutputConfig::default());
    let busy = Input::new(p.GPIO6, InputConfig::default().with_pull(Pull::None));

    // Deselect the SD card so it stays off the shared SPI bus (CS idle high).
    let _sd_cs = Output::new(p.GPIO12, Level::High, OutputConfig::default());

    // ── Shared SPI bus: SCLK=8, MOSI=10, MISO=7, Mode 0, 40 MHz ─────────────
    let spi = Spi::new(
        p.SPI2,
        SpiConfig::default()
            .with_frequency(Rate::from_mhz(40))
            .with_mode(Mode::_0),
    )
    .expect("SPI init")
    .with_sck(p.GPIO8)
    .with_mosi(p.GPIO10)
    .with_miso(p.GPIO7);

    // ── E-ink driver ─────────────────────────────────────────────────────────
    let fb: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(FRAMEBUFFER) };
    let mut eink = Eink::new(
        spi,
        dc,
        cs,
        rst,
        busy,
        delay,
        fb,
        pins::X4_WIDTH,
        pins::X4_HEIGHT,
    );
    eink.init();

    // ── Front-button ADC ladders (GPIO1 / GPIO2) ─────────────────────────────
    let mut adc_cfg = AdcConfig::new();
    let mut adc_pin1 = adc_cfg.enable_pin(p.GPIO1, Attenuation::_11dB);
    let mut adc_pin2 = adc_cfg.enable_pin(p.GPIO2, Attenuation::_11dB);
    let mut adc = Adc::new(p.ADC1, adc_cfg);

    // Power button (GPIO3, active low). Held long here would trigger power_off.
    let power_btn = Input::new(p.GPIO3, InputConfig::default().with_pull(Pull::Up));

    // ── First render ─────────────────────────────────────────────────────────
    draw_home(&mut eink, None);
    eink.display(RefreshMode::Full, false);

    // ── Superloop ────────────────────────────────────────────────────────────
    let mut btn_state = ButtonState::new();
    let mut delay_loop = delay;
    loop {
        // Poll both ADC ladders. On RISC-V the only blocking primitive is the
        // nb-style `read_oneshot`; its sole error is WouldBlock, so spin to block.
        let v1: u16 = loop {
            if let Ok(v) = adc.read_oneshot(&mut adc_pin1) {
                break v;
            }
        };
        let v2: u16 = loop {
            if let Ok(v) = adc.read_oneshot(&mut adc_pin2) {
                break v;
            }
        };
        let pressed = decode_group1(v1).or_else(|| decode_group2(v2));

        if let Some(btn) = btn_state.update(pressed) {
            draw_home(&mut eink, Some(btn));
            // Page-turn style: fast differential refresh.
            eink.display(RefreshMode::Fast, false);
        }

        // Power button held (active low) → shut down via the latch.
        if power_btn.is_low() {
            eink.deep_sleep();
            crate::power::power_off(p.GPIO13);
        }

        delay_loop.delay_ms(40); // ~25 Hz input poll, matches a debounce window.
    }
}

/// Render the placeholder home screen, optionally echoing the last button.
fn draw_home<D>(target: &mut D, last: Option<Button>)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white

    let title = MonoTextStyle::new(&FONT_10X20, BinaryColor::On);
    let body = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);

    let _ = Text::new("CrossPoint (Rust)", Point::new(40, 60), title).draw(target);
    let _ = Text::new(
        "no_std / esp-hal 1.1 / SSD1677 800x480",
        Point::new(40, 90),
        body,
    )
    .draw(target);

    let label = match last {
        Some(Button::Back) => "Last: Back",
        Some(Button::Confirm) => "Last: Confirm",
        Some(Button::Left) => "Last: Left",
        Some(Button::Right) => "Last: Right",
        Some(Button::Up) => "Last: Up",
        Some(Button::Down) => "Last: Down",
        Some(Button::Power) => "Last: Power",
        None => "Press a button...",
    };
    let _ = Text::new(label, Point::new(40, 130), body).draw(target);
}
