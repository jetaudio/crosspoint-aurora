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
use embedded_graphics::primitives::{Line, PrimitiveStyle, Rectangle};
use embedded_graphics::text::{Baseline, Text};

use esp_hal::analog::adc::{Adc, AdcCalCurve, AdcConfig, Attenuation};
use esp_hal::delay::Delay;
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::i2c::master::{Config as I2cConfig, I2c};
use esp_hal::peripherals::Peripherals;
use esp_hal::peripherals::ADC1;
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::spi::Mode;
use esp_hal::time::Rate;

use crosspoint_core::driver::{Eink, RefreshMode, Variant};
use crosspoint_core::input::{decode_group1, decode_group2, ButtonState};
use crosspoint_core::layout::PageMetrics;
use crosspoint_core::pins;
use crosspoint_core::ui::{
    self, AuroraHome, Event, HomeAction, Menu, Reader, Settings, Zone, TAB_LABELS,
};

/// 1bpp framebuffer, sized for the larger panel (X3 = 52272 B) so either
/// variant fits. Zero-initialised → lands in `.bss`; `Eink::init` fills 0xFF
/// before the first draw, and every render clears it first.
static mut FRAMEBUFFER: [u8; pins::MAX_BUFFER_SIZE] = [0; pins::MAX_BUFFER_SIZE];

/// Buffer a book file is read into from the SD card (32 KB → ~paginates a short
/// book/chapter). Lives in `.bss`. Falls back to the bundled sample if empty.
const BOOK_BUF_SIZE: usize = 32 * 1024;
static mut BOOK_BUF: [u8; BOOK_BUF_SIZE] = [0; BOOK_BUF_SIZE];

/// Buffer a whole EPUB (ZIP) is read into before its first chapter is inflated
/// into BOOK_BUF. 32 KB on the Wi-Fi build (the radio's heap + task arena take
/// RAM); larger books need streaming (roadmap). Lives in `.bss`.
const EPUB_BUF_SIZE: usize = 32 * 1024;
static mut EPUB_BUF: [u8; EPUB_BUF_SIZE] = [0; EPUB_BUF_SIZE];

/// Bring up the board and run the async app loop forever.
pub async fn run(spawner: embassy_executor::Spawner, mut p: Peripherals) -> ! {
    // Our embassy-time driver on alarm0; the Wi-Fi radio uses alarm1.
    let systimer = esp_hal::timer::systimer::SystemTimer::new(p.SYSTIMER);
    crate::time_driver::init(systimer.alarm0);
    crate::wifi::init(systimer.alarm1, p.RNG, p.RADIO_CLK, p.WIFI, spawner);

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

    // ── Device detection: X3 vs X4 by I²C fingerprint ────────────────────────
    // SCL shares GPIO0 with the X4 battery ADC, so the bus is reborrowed here
    // and dropped before the ADC takes GPIO0 below. (SDA=20, SCL=0, 400 kHz.)
    let variant = {
        let mut i2c = I2c::new(
            &mut p.I2C0,
            I2cConfig::default().with_frequency(Rate::from_khz(400)),
        )
        .expect("I2C init")
        .with_sda(&mut p.GPIO20)
        .with_scl(&mut p.GPIO0);
        let mut d = delay;
        if crate::detect::is_x3(&mut i2c, &mut d) {
            Variant::X3
        } else {
            Variant::X4
        }
    };
    let (panel_w, panel_h) = match variant {
        Variant::X3 => (pins::X3_WIDTH, pins::X3_HEIGHT),
        Variant::X4 => (pins::X4_WIDTH, pins::X4_HEIGHT),
    };
    esp_println::println!(
        "crosspoint-rs: panel = {:?} ({}x{})",
        variant,
        panel_w,
        panel_h
    );

    // ── E-ink driver (over its shared SPI device) ────────────────────────────
    let fb: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(FRAMEBUFFER) };
    let mut eink = Eink::with_variant(
        display_dev,
        dc,
        rst,
        busy,
        delay,
        fb,
        panel_w,
        panel_h,
        variant,
    );
    eink.init();

    // ── Front-button ADC ladders (GPIO1 / GPIO2) ─────────────────────────────
    let mut adc_cfg = AdcConfig::new();
    let mut adc_pin1 = adc_cfg.enable_pin(p.GPIO1, Attenuation::_11dB);
    let mut adc_pin2 = adc_cfg.enable_pin(p.GPIO2, Attenuation::_11dB);
    // Battery sense on GPIO0 with curve calibration → reads come back in mV
    // (the ÷2 divider is undone in `battery::percentage_from_adc_millivolts`).
    let mut bat_pin =
        adc_cfg.enable_pin_with_cal::<_, AdcCalCurve<ADC1>>(p.GPIO0, Attenuation::_11dB);
    let mut adc = Adc::new(p.ADC1, adc_cfg);

    // Power button (GPIO3, active low). Held long here would trigger power_off.
    let power_btn = Input::new(p.GPIO3, InputConfig::default().with_pull(Pull::Up));

    // Reader settings (line spacing / margin); the reader's PageMetrics are
    // derived from these at open time. FONT_6X10 is a fixed 6 px-wide cell, so
    // the wrap advance is a constant 6.
    let mut settings = Settings::new();
    let book_buf: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(BOOK_BUF) };
    let epub_buf: &'static mut [u8] = unsafe { &mut *core::ptr::addr_of_mut!(EPUB_BUF) };

    // Build the Aurora home library list from the SD root. If there's no card /
    // no files, fall back to a single sample entry so the demo still works.
    let mut library = Menu::new();
    {
        let dev = RefCellDevice::new(&spi_bus, &mut sd_cs, delay).expect("SD device");
        let n = crate::sd::list_files(dev, delay, &mut library);
        esp_println::println!("crosspoint-rs: {} files on SD", n);
        if n == 0 {
            let _ = library.push(SAMPLE_NAME);
        }
    }
    let mut home = AuroraHome::new(library);

    // Load persisted reader settings from the SD card, if present.
    {
        let dev = RefCellDevice::new(&spi_bus, &mut sd_cs, delay).expect("SD device");
        if let Some((lh, mg)) = crate::sd::load_settings(dev, delay) {
            settings = Settings::from_raw(lh, mg);
            esp_println::println!("crosspoint-rs: loaded settings lh={} mg={}", lh, mg);
        }
    }

    // The reader is created when a book is opened from the home library.
    let mut reader: Option<Reader> = None;
    let mut active = Active::Home;

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
        &settings,
        reader.as_ref(),
        battery_pct,
    );
    eink.display(RefreshMode::Full, false);

    // ── Async app loop ───────────────────────────────────────────────────────
    let mut btn_state = ButtonState::new();
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
                // Aurora two-zone nav: side Up/Down browse the library, front
                // Left/Right move the tab bar, Select acts on the active zone.
                Active::Home => match event {
                    Some(Event::Up) => (home.up(), false),
                    Some(Event::Down) => (home.down(), false),
                    Some(Event::PrevPage) => (home.left(), false), // front Left
                    Some(Event::NextPage) => (home.right(), false), // front Right
                    Some(Event::Select) => match home.select() {
                        HomeAction::OpenContent(i) => {
                            if let Some(name) = home.content().item(i) {
                                // Drop any current reader so its borrow of book_buf
                                // ends before we read into book_buf again.
                                drop(reader.take());
                                let dev = RefCellDevice::new(&spi_bus, &mut sd_cs, delay)
                                    .expect("SD device");
                                let mut n;
                                if is_epub(name) {
                                    // EPUB: ZIP into epub_buf → inflate first XHTML
                                    // chapter into book_buf → strip tags to text.
                                    let raw = crate::sd::load_named(dev, delay, name, epub_buf);
                                    n = match crosspoint_core::archive::extract_first_html(
                                        &epub_buf[..raw],
                                        book_buf,
                                    ) {
                                        Ok(hl) => crosspoint_core::parser::extract_text_inplace(
                                            book_buf, hl,
                                        ),
                                        Err(_) => 0,
                                    };
                                } else {
                                    n = crate::sd::load_named(dev, delay, name, book_buf);
                                    if is_html(name) {
                                        n = crosspoint_core::parser::extract_text_inplace(
                                            book_buf, n,
                                        );
                                    }
                                }
                                let text: &[u8] = if n > 0 { &book_buf[..n] } else { SAMPLE_TEXT };
                                esp_println::println!(
                                    "crosspoint-rs: open '{}' -> {} bytes",
                                    name,
                                    n
                                );
                                reader = Some(Reader::new(text, metrics_from(&settings), adv6));
                                active = Active::Reader;
                                (true, true)
                            } else {
                                (false, false)
                            }
                        }
                        HomeAction::OpenTab(t) => {
                            // Browse/Recent focus the library; Settings opens the
                            // settings screen; Transfer is not yet ported.
                            esp_println::println!("crosspoint-rs: tab {}", TAB_LABELS[t]);
                            if t == 2 {
                                active = Active::Settings;
                                (true, true)
                            } else {
                                (true, false)
                            }
                        }
                        HomeAction::None => (false, false),
                    },
                    _ => (false, false),
                },
                // Settings: Up/Down select a field, Left/Right adjust it.
                Active::Settings => match event {
                    Some(Event::Up) => (settings.up(), false),
                    Some(Event::Down) => (settings.down(), false),
                    Some(Event::PrevPage) => (settings.dec(), false),
                    Some(Event::NextPage) => (settings.inc(), false),
                    Some(Event::Back) => {
                        // Persist settings to the SD card on the way out.
                        let dev =
                            RefCellDevice::new(&spi_bus, &mut sd_cs, delay).expect("SD device");
                        let ok = crate::sd::save_settings(
                            dev,
                            delay,
                            settings.line_height(),
                            settings.margin(),
                        );
                        esp_println::println!("crosspoint-rs: settings saved={}", ok);
                        active = Active::Home;
                        (true, true)
                    }
                    _ => (false, false),
                },
                Active::Reader => match (event, reader.as_mut()) {
                    (Some(Event::NextPage), Some(r)) => (r.next_page(), false),
                    (Some(Event::PrevPage), Some(r)) => (r.prev_page(), false),
                    (Some(Event::Back), _) => {
                        active = Active::Home;
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
                    &settings,
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
            crate::power::power_off(p.GPIO13, p.LPWR);
        }

        // ~25 Hz input poll (yields to the executor between iterations).
        embassy_time::Timer::after(embassy_time::Duration::from_millis(40)).await;
    }
}

/// Which screen currently has focus.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Active {
    Home,
    Settings,
    Reader,
}

/// Derive the reader's page metrics from the current settings.
fn metrics_from(s: &Settings) -> PageMetrics {
    PageMetrics {
        width: pins::X4_WIDTH,
        height: pins::X4_HEIGHT,
        margin_x: s.margin(),
        margin_y: 34, // leaves the status-bar strip at the top
        line_height: s.line_height(),
    }
}

/// Library entry shown when no SD card / files are present; opens the sample.
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
    home: &AuroraHome,
    settings: &Settings,
    reader: Option<&Reader>,
    pct: u8,
) where
    D: DrawTarget<Color = BinaryColor>,
{
    match active {
        Active::Home => draw_home(target, home, pct),
        Active::Settings => draw_settings(target, settings, pct),
        Active::Reader => {
            if let Some(r) = reader {
                draw_reader(target, r, pct);
            }
        }
    }
}

/// Render the settings screen: each field as "Label  < value >", caret on the
/// selected row.
fn draw_settings<D>(target: &mut D, settings: &Settings, pct: u8)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white
    draw_status_bar(target, "Settings", pct);

    let style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);
    let mut y = 70i32;
    for i in 0..crosspoint_core::ui::settings::SETTING_COUNT {
        let caret = if i == settings.cursor() { ">" } else { " " };
        let mut line: heapless::String<48> = heapless::String::new();
        let _ = write!(
            line,
            "{} {}   < {} >",
            caret,
            settings.label(i),
            settings.value(i)
        );
        let _ = Text::with_baseline(&line, Point::new(24, y), style, Baseline::Top).draw(target);
        y += 28;
    }

    let hint = "Up/Down: select   Left/Right: adjust   Back: home";
    let _ = Text::with_baseline(
        hint,
        Point::new(24, pins::X4_HEIGHT as i32 - 24),
        style,
        Baseline::Top,
    )
    .draw(target);
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

/// Render the Aurora home: status bar + "Now Reading" featured card + library
/// list + bottom tab bar. Ports the Aurora home redesign.
fn draw_home<D>(target: &mut D, home: &AuroraHome, pct: u8)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let _ = target.clear(BinaryColor::Off); // white
    draw_status_bar(target, "Library", pct);

    let body_style = MonoTextStyle::new(&FONT_6X10, BinaryColor::On);
    let w = pins::X4_WIDTH as i32;

    // Featured "Now Reading" card (bordered) — shows the highlighted item.
    let _ = Rectangle::new(Point::new(16, 36), Size::new((w - 32) as u32, 52))
        .into_styled(PrimitiveStyle::with_stroke(BinaryColor::On, 1))
        .draw(target);
    let _ = Text::with_baseline("Now Reading", Point::new(24, 42), body_style, Baseline::Top)
        .draw(target);
    let _ = Text::with_baseline(
        home.featured().unwrap_or("—"),
        Point::new(24, 62),
        body_style,
        Baseline::Top,
    )
    .draw(target);

    // Library list (the content zone).
    let list_metrics = PageMetrics {
        width: pins::X4_WIDTH,
        height: pins::X4_HEIGHT,
        margin_x: 24,
        margin_y: 104,
        line_height: 14,
    };
    let _ = ui::render_menu(target, home.content(), &list_metrics, body_style);

    draw_tab_bar(target, home);
}

/// Bottom icon tab bar: four evenly-spaced labels. The current tab is boxed; if
/// the tab bar is the active zone it is filled (inverted).
fn draw_tab_bar<D>(target: &mut D, home: &AuroraHome)
where
    D: DrawTarget<Color = BinaryColor>,
{
    let w = pins::X4_WIDTH as i32;
    let h = pins::X4_HEIGHT as i32;
    let bar_top = h - 36;
    let cell = w / TAB_LABELS.len() as i32;
    let tab_active_zone = home.zone() == Zone::TabBar;

    // Divider above the bar.
    let _ = Line::new(Point::new(0, bar_top - 4), Point::new(w, bar_top - 4))
        .into_styled(PrimitiveStyle::with_stroke(BinaryColor::On, 1))
        .draw(target);

    for (i, label) in TAB_LABELS.iter().enumerate() {
        let x0 = i as i32 * cell;
        let selected = i == home.tab();
        let rect = Rectangle::new(
            Point::new(x0 + 6, bar_top),
            Size::new((cell - 12) as u32, 30),
        );
        // Highlight the selected tab; invert it when the tab bar has focus.
        let (text_color, fill) = if selected && tab_active_zone {
            let _ = rect
                .into_styled(PrimitiveStyle::with_fill(BinaryColor::On))
                .draw(target);
            (BinaryColor::Off, true)
        } else if selected {
            let _ = rect
                .into_styled(PrimitiveStyle::with_stroke(BinaryColor::On, 1))
                .draw(target);
            (BinaryColor::On, true)
        } else {
            (BinaryColor::On, false)
        };
        let _ = fill; // (kept for clarity)

        let style = MonoTextStyle::new(&FONT_6X10, text_color);
        // Centre the label within the cell (6 px/char).
        let tx = x0 + (cell - label.len() as i32 * 6) / 2;
        let _ = Text::with_baseline(label, Point::new(tx, bar_top + 10), style, Baseline::Top)
            .draw(target);
    }
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
