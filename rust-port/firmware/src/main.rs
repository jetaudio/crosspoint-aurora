#![no_std]
#![no_main]

//! CrossPoint e-reader firmware — Rust no_std port for the ESP32-C3 (Xteink X4/X3).
//!
//! Architecture: a single blocking superloop (mirrors the original C++ firmware's
//! Activity update/render model). No async/embassy — see README for why. The
//! portable logic lives in the `crosspoint-core` crate; this binary is just the
//! esp-hal board bring-up + superloop.

use esp_backtrace as _;

mod app;
mod power;
mod sd;

// esp-hal 1.x stable boots through the ESP-IDF 2nd-stage bootloader, which needs
// an application descriptor embedded in the image.
esp_bootloader_esp_idf::esp_app_desc!();

#[esp_hal::main]
fn main() -> ! {
    let peripherals = esp_hal::init(esp_hal::Config::default());
    esp_println::println!("crosspoint-rs: boot");
    app::run(peripherals)
}
