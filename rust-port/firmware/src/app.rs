//! Application entry: board bring-up + the blocking superloop.
//!
//! Mirrors the C++ firmware's model: initialise the HAL, render a screen, then
//! loop polling input and re-rendering on demand. All hardware wiring uses the
//! exact pins from `discovered_pins.md`.

use core::fmt::Write as _;

use embedded_graphics::mono_font::ascii::{FONT_10X20, FONT_6X10};
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;
use embedded_graphics::text::{Baseline, Text};
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
use crosspoint_core::layout::PageMetrics;
use crosspoint_core::pins;
use crosspoint_core::ui::{self, Event, Reader};

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

    // ── Reader: flow a sample book through wrap/paginate ─────────────────────
    // FONT_6X10 is a fixed 6 px-wide cell, so the wrap advance is a constant 6.
    let metrics = PageMetrics {
        width: pins::X4_WIDTH,
        height: pins::X4_HEIGHT,
        margin_x: 16,
        margin_y: 34, // leaves a title strip at the top
        line_height: 14,
    };
    let mut reader = Reader::new(SAMPLE_TEXT, metrics, adv6);

    // ── First render ─────────────────────────────────────────────────────────
    draw_reader(&mut eink, &reader);
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
            // Left/Right (or Up/Down) turn pages; redraw only on a real change.
            let changed = match ui::map_button(btn) {
                Some(Event::NextPage) | Some(Event::Down) => reader.next_page(),
                Some(Event::PrevPage) | Some(Event::Up) => reader.prev_page(),
                _ => false,
            };
            if changed {
                draw_reader(&mut eink, &reader);
                eink.display(RefreshMode::Fast, false); // fast diff for page turns
            }
        }

        // Power button held (active low) → shut down via the latch.
        if power_btn.is_low() {
            eink.deep_sleep();
            crate::power::power_off(p.GPIO13);
        }

        delay_loop.delay_ms(40); // ~25 Hz input poll, matches a debounce window.
    }
}

/// Fixed advance for FONT_6X10 (6 px per cell), so wrap widths match the render.
fn adv6(_c: char) -> u16 {
    6
}

/// Render the reader: title strip, the current page's wrapped lines, and a
/// `page X/Y` footer.
fn draw_reader<D>(target: &mut D, reader: &Reader)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white

    let title_style = MonoTextStyle::new(&FONT_10X20, BinaryColor::On);
    let body_style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);

    let _ = Text::with_baseline(
        "CrossPoint Reader",
        Point::new(16, 6),
        title_style,
        Baseline::Top,
    )
    .draw(target);

    let lines = reader.page_lines();
    let _ = ui::render_lines(target, &lines, reader.metrics(), body_style);

    // Footer: "page X/Y" (1-based), formatted without allocation.
    let mut footer: heapless::String<24> = heapless::String::new();
    let _ = write!(
        footer,
        "page {}/{}",
        reader.current_page() + 1,
        reader.total_pages()
    );
    let fy = pins::X4_HEIGHT as i32 - 18;
    let _ =
        Text::with_baseline(&footer, Point::new(16, fy), body_style, Baseline::Top).draw(target);
}

/// A short public-domain sample so the reader has something to paginate before
/// SD-card loading is wired up.
const SAMPLE_TEXT: &[u8] = b"CrossPoint Reader - Rust port demo.\n\n\
It was the best of times, it was the worst of times, it was the age of \
wisdom, it was the age of foolishness, it was the epoch of belief, it was \
the epoch of incredulity, it was the season of Light, it was the season of \
Darkness, it was the spring of hope, it was the winter of despair.\n\n\
We had everything before us, we had nothing before us, we were all going \
direct to Heaven, we were all going direct the other way.\n\n\
Press Left and Right to turn the page. Press the power button to shut down.";
