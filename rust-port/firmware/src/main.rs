#![no_std]
#![no_main]

//! CrossPoint e-reader firmware — Rust no_std port for the ESP32-C3 (Xteink X4/X3).
//!
//! Runs on the embassy thread-mode executor with our own `embassy-time` driver
//! (`time_driver`) so the whole project stays on **stable** esp-hal. The app loop
//! is one async task; the portable logic lives in the `crosspoint-core` crate.

use esp_backtrace as _;

mod app;
mod detect;
mod power;
mod sd;
mod time_driver;

use embassy_executor::Executor;
use esp_hal::peripherals::Peripherals;
use static_cell::StaticCell;

// esp-hal 1.x stable boots through the ESP-IDF 2nd-stage bootloader, which needs
// an application descriptor embedded in the image.
esp_bootloader_esp_idf::esp_app_desc!();

#[esp_hal::main]
fn main() -> ! {
    let p = esp_hal::init(esp_hal::Config::default());
    esp_println::println!("crosspoint-rs: boot");

    static EXECUTOR: StaticCell<Executor> = StaticCell::new();
    let executor = EXECUTOR.init(Executor::new());
    executor.run(|spawner| {
        spawner.spawn(main_task(p)).ok();
    });
}

#[embassy_executor::task]
async fn main_task(p: Peripherals) {
    app::run(p).await;
}
