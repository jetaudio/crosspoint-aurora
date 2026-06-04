//! Application entry: board bring-up + the blocking superloop.
//!
//! Mirrors the C++ firmware's model: initialise the HAL, render a screen, then
//! loop polling input and re-rendering on demand. All hardware wiring uses the
//! exact pins from `discovered_pins.md`.

use core::cell::RefCell;
use core::fmt::Write as _;

use embedded_hal_bus::spi::RefCellDevice;

use embedded_graphics::mono_font::ascii::{FONT_10X20, FONT_6X10};
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::{Line, PrimitiveStyle};
use embedded_graphics::text::{Baseline, Text};
use embedded_hal::delay::DelayNs;

use esp_hal::analog::adc::{Adc, AdcCalCurve, AdcConfig, Attenuation};
use esp_hal::delay::Delay;
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::peripherals::Peripherals;
use esp_hal::peripherals::ADC1;
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::spi::Mode;
use esp_hal::time::Rate;

use crosspoint_core::driver::{Eink, RefreshMode};
use crosspoint_core::input::{decode_group1, decode_group2, ButtonState};
use crosspoint_core::layout::PageMetrics;
use crosspoint_core::pins;
use crosspoint_core::ui::{self, Event, Menu, Reader};

/// 1bpp framebuffer for the SSD1677 (48 KB). Zero-initialised so it lands in
/// `.bss` (no 48 KB of flash init data); `Eink::init` fills it with 0xFF before
/// the first draw, and every render clears it first.
static mut FRAMEBUFFER: [u8; pins::X4_BUFFER_SIZE] = [0; pins::X4_BUFFER_SIZE];

/// Buffer a book file is read into from the SD card (32 KB → ~paginates a short
/// book/chapter). Lives in `.bss`. Falls back to the bundled sample if empty.
const BOOK_BUF_SIZE: usize = 32 * 1024;
static mut BOOK_BUF: [u8; BOOK_BUF_SIZE] = [0; BOOK_BUF_SIZE];

/// Buffer a whole EPUB (ZIP) is read into before its first chapter is inflated
/// into BOOK_BUF. 64 KB caps the EPUB size we can open in RAM; larger books need
/// streaming (roadmap). Lives in `.bss`.
const EPUB_BUF_SIZE: usize = 64 * 1024;
static mut EPUB_BUF: [u8; EPUB_BUF_SIZE] = [0; EPUB_BUF_SIZE];

/// Bring up the board and run the superloop forever.
pub fn run(p: Peripherals) -> ! {
    let delay = Delay::new();

    // ── Display control GPIOs (exact pins) ───────────────────────────────────
    let dc = Output::new(p.GPIO4, Level::Low, OutputConfig::default());
    let cs = Output::new(p.GPIO21, Level::High, OutputConfig::default());
    let rst = Output::new(p.GPIO5, Level::High, OutputConfig::default());
    let busy = Input::new(p.GPIO6, InputConfig::default().with_pull(Pull::None));

    // SD card chip-select on the shared bus (idle high = deselected). Kept owned
    // and mutable so a fresh RefCellDevice can be made per SD operation (passing
    // `&mut sd_cs` as the CS), leaving the card free to be re-accessed later.
    let mut sd_cs = Output::new(p.GPIO12, Level::High, OutputConfig::default());

    // ── Shared SPI bus: SCLK=8, MOSI=10, MISO=7, Mode 0, 40 MHz ─────────────
    // The display and SD card share SPI2; each gets a RefCellDevice with its own
    // CS so only one is asserted at a time.
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
    let spi_bus: RefCell<_> = RefCell::new(spi);
    let display_dev = RefCellDevice::new(&spi_bus, cs, delay).expect("display SPI device");

    // ── E-ink driver (over its shared SPI device) ────────────────────────────
    let fb: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(FRAMEBUFFER) };
    let mut eink = Eink::new(
        display_dev,
        dc,
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
    // Battery sense on GPIO0 with curve calibration → reads come back in mV
    // (the ÷2 divider is undone in `battery::percentage_from_adc_millivolts`).
    let mut bat_pin =
        adc_cfg.enable_pin_with_cal::<_, AdcCalCurve<ADC1<'static>>>(p.GPIO0, Attenuation::_11dB);
    let mut adc = Adc::new(p.ADC1, adc_cfg);

    // Power button (GPIO3, active low). Held long here would trigger power_off.
    let power_btn = Input::new(p.GPIO3, InputConfig::default().with_pull(Pull::Up));

    // ── Screens: Home menu + the Reader ──────────────────────────────────────
    // FONT_6X10 is a fixed 6 px-wide cell, so the wrap advance is a constant 6.
    let reader_metrics = PageMetrics {
        width: pins::X4_WIDTH,
        height: pins::X4_HEIGHT,
        margin_x: 16,
        margin_y: 34, // leaves a title strip at the top
        line_height: 14,
    };
    let book_buf: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(BOOK_BUF) };
    let epub_buf: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(EPUB_BUF) };
    let mut home = Menu::from_items(HOME_ITEMS);
    let mut browser = Menu::new();
    // The reader is created when a file is opened from the browser.
    let mut reader: Option<Reader> = None;
    let mut active = Active::Home;

    // List the SD root into the browser. If there's no card / no files, fall back
    // to a single sample entry so the demo still works without an SD card.
    {
        let dev = RefCellDevice::new(&spi_bus, &mut sd_cs, delay).expect("SD device");
        let n = crate::sd::list_files(dev, delay, &mut browser);
        esp_println::println!("crosspoint-rs: {} files on SD", n);
        if n == 0 {
            let _ = browser.push(SAMPLE_NAME);
        }
    }

    // Read the battery once at boot; refreshed on each redraw below. With curve
    // calibration `read_oneshot` yields millivolts; spin to block (WouldBlock).
    let mut battery_pct = {
        let mv = loop {
            if let Ok(v) = adc.read_oneshot(&mut bat_pin) {
                break v;
            }
        };
        crosspoint_core::battery::percentage_from_adc_millivolts(mv)
    };

    // ── First render ─────────────────────────────────────────────────────────
    draw_active(
        &mut eink,
        active,
        &home,
        &browser,
        reader.as_ref(),
        battery_pct,
    );
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
            let event = ui::map_button(btn);
            // Returns whether to redraw, and whether it was a screen change
            // (which warrants a stronger full refresh).
            let (redraw, screen_change) = match active {
                Active::Home => match event {
                    Some(Event::Up) => (home.up(), false),
                    Some(Event::Down) => (home.down(), false),
                    // Item 0 ("Open book") opens the file browser; else no-op.
                    Some(Event::Select) if home.selected() == 0 => {
                        active = Active::Browser;
                        (true, true)
                    }
                    _ => (false, false),
                },
                Active::Browser => match event {
                    Some(Event::Up) => (browser.up(), false),
                    Some(Event::Down) => (browser.down(), false),
                    // Open the selected file: drop any current reader (releasing
                    // its book_buf borrow), read the file into book_buf, then make
                    // a fresh reader over it. Falls back to the bundled sample if
                    // the read returns nothing.
                    Some(Event::Select) => {
                        if let Some(name) = browser.selected_item() {
                            // Drop any current reader first so its borrow of
                            // book_buf ends before we read into book_buf again.
                            drop(reader.take());
                            let dev =
                                RefCellDevice::new(&spi_bus, &mut sd_cs, delay).expect("SD device");
                            let mut n;
                            if is_epub(name) {
                                // EPUB: read the ZIP into epub_buf, extract the
                                // first XHTML chapter into book_buf, then strip
                                // tags to plain text.
                                let raw = crate::sd::load_named(dev, delay, name, epub_buf);
                                n = match crosspoint_core::archive::extract_first_html(
                                    &epub_buf[..raw],
                                    book_buf,
                                ) {
                                    Ok(html_len) => crosspoint_core::parser::extract_text_inplace(
                                        book_buf, html_len,
                                    ),
                                    Err(_) => 0,
                                };
                            } else {
                                n = crate::sd::load_named(dev, delay, name, book_buf);
                                // HTML/XHTML → strip to plain text in place.
                                if is_html(name) {
                                    n = crosspoint_core::parser::extract_text_inplace(book_buf, n);
                                }
                            }
                            let text: &[u8] = if n > 0 { &book_buf[..n] } else { SAMPLE_TEXT };
                            esp_println::println!("crosspoint-rs: open '{}' -> {} bytes", name, n);
                            reader = Some(Reader::new(text, reader_metrics, adv6));
                        }
                        active = Active::Reader;
                        (true, true)
                    }
                    Some(Event::Back) => {
                        active = Active::Home;
                        (true, true)
                    }
                    _ => (false, false),
                },
                Active::Reader => match (event, reader.as_mut()) {
                    (Some(Event::NextPage), Some(r)) => (r.next_page(), false),
                    (Some(Event::PrevPage), Some(r)) => (r.prev_page(), false),
                    (Some(Event::Back), _) => {
                        active = Active::Browser;
                        (true, true)
                    }
                    _ => (false, false),
                },
            };
            if redraw {
                battery_pct = {
                    let mv = loop {
                        if let Ok(v) = adc.read_oneshot(&mut bat_pin) {
                            break v;
                        }
                    };
                    crosspoint_core::battery::percentage_from_adc_millivolts(mv)
                };
                draw_active(
                    &mut eink,
                    active,
                    &home,
                    &browser,
                    reader.as_ref(),
                    battery_pct,
                );
                let mode = if screen_change {
                    RefreshMode::Full
                } else {
                    RefreshMode::Fast
                };
                eink.display(mode, false);
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

/// Which screen currently has focus.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Active {
    Home,
    Browser,
    Reader,
}

/// Home menu entries.
const HOME_ITEMS: &[&str] = &["Open book", "About"];

/// Browser entry shown when no SD card / files are present; opens the sample.
const SAMPLE_NAME: &str = "sample.txt";

/// Fixed advance for FONT_6X10 (6 px per cell), so wrap widths match the render.
fn adv6(_c: char) -> u16 {
    6
}

/// True if the filename looks like HTML/XHTML (case-insensitive extension).
fn is_html(name: &str) -> bool {
    let ext = name.rsplit('.').next().unwrap_or("");
    ext.eq_ignore_ascii_case("html")
        || ext.eq_ignore_ascii_case("htm")
        || ext.eq_ignore_ascii_case("xhtml")
}

/// True if the filename is an EPUB (a ZIP of XHTML).
fn is_epub(name: &str) -> bool {
    name.rsplit('.')
        .next()
        .unwrap_or("")
        .eq_ignore_ascii_case("epub")
}

/// Dispatch a render to whichever screen is active.
fn draw_active<D>(
    target: &mut D,
    active: Active,
    home: &Menu,
    browser: &Menu,
    reader: Option<&Reader>,
    pct: u8,
) where
    D: DrawTarget<Color = BinaryColor>,
{
    match active {
        Active::Home => draw_menu(target, "CrossPoint", home, pct),
        Active::Browser => draw_menu(target, "Books", browser, pct),
        Active::Reader => {
            if let Some(r) = reader {
                draw_reader(target, r, pct);
            }
        }
    }
}

/// Aurora-style slim status bar: page title (left, bold), battery % (right), and
/// a divider rule below. Ported from `AuroraTheme::drawHeaderBar`.
fn draw_status_bar<D>(target: &mut D, title: &str, pct: u8)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let title_style = MonoTextStyle::new(&FONT_10X20, BinaryColor::On);
    let body_style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);

    let _ = Text::with_baseline(title, Point::new(16, 6), title_style, Baseline::Top).draw(target);

    // Battery %, right-aligned (FONT_6X10 is 6 px/char).
    let mut buf: heapless::String<8> = heapless::String::new();
    let _ = write!(buf, "{pct}%");
    let bx = pins::X4_WIDTH as i32 - 16 - (buf.len() as i32) * 6;
    let _ = Text::with_baseline(&buf, Point::new(bx, 8), body_style, Baseline::Top).draw(target);

    // Divider rule below the bar.
    let _ = Line::new(
        Point::new(16, 28),
        Point::new(pins::X4_WIDTH as i32 - 16, 28),
    )
    .into_styled(PrimitiveStyle::with_stroke(BinaryColor::On, 1))
    .draw(target);
}

/// Render a titled vertical menu (Home / Browser).
fn draw_menu<D>(target: &mut D, title: &str, menu: &Menu, pct: u8)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white
    draw_status_bar(target, title, pct);

    let body_style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);
    let menu_metrics = PageMetrics {
        width: pins::X4_WIDTH,
        height: pins::X4_HEIGHT,
        margin_x: 24,
        margin_y: 60,
        line_height: 16,
    };
    let _ = ui::render_menu(target, menu, &menu_metrics, body_style);
}

/// Render the reader: status bar, the current page's wrapped lines, and a
/// `page X/Y` footer.
fn draw_reader<D>(target: &mut D, reader: &Reader, pct: u8)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white
    draw_status_bar(target, "Reading", pct);

    let body_style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);
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
