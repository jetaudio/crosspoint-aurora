#![no_std]
#![no_main]

//! CrossPoint e-reader firmware — Rust no_std port for the ESP32-C3 (Xteink X4/X3).
//!
//! Runs on the embassy async executor (`#[esp_hal_embassy::main]`); the app loop
//! is one async task. The portable logic lives in the `crosspoint-core` crate;
//! this binary is the esp-hal board bring-up + the async app loop.

use esp_backtrace as _;

mod app;
mod detect;
mod power;
mod sd;

#[esp_hal_embassy::main]
async fn main(_spawner: embassy_executor::Spawner) {
    let peripherals = esp_hal::init(esp_hal::Config::default());
    esp_println::println!("crosspoint-rs: boot");
    app::run(peripherals).await
}
