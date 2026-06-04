#![no_std]
#![no_main]

//! CrossPoint e-reader firmware — Wi-Fi-enabled variant (ESP32-C3, Xteink X4/X3).
//!
//! Adds Wi-Fi, which forces a global allocator (esp-wifi needs a heap) and a
//! pre-release esp-hal (esp-wifi 0.13 only builds on `esp-hal 1.0.0-beta.0`
//! internals). The app's own data stays zero-allocation; the heap exists solely
//! for the network stack. embassy runs via our own time driver (esp-hal-embassy
//! that pairs with beta.0 conflicts with esp-wifi's esp-hal pin). The stable,
//! zero-alloc, no-Wi-Fi build lives on the `rust` branch — see README.

use esp_backtrace as _;

mod app;
mod detect;
mod power;
mod sd;
mod time_driver;
mod wifi;

use embassy_executor::Executor;
use esp_hal::peripherals::Peripherals;
use static_cell::StaticCell;

#[esp_hal::main]
fn main() -> ! {
    let p = esp_hal::init(esp_hal::Config::default());

    // Heap for esp-wifi ONLY (app data stays static/heapless). 72 KB fits the C3.
    esp_alloc::heap_allocator!(size: 72 * 1024);

    esp_println::println!("crosspoint-rs: boot (wifi build)");

    static EXECUTOR: StaticCell<Executor> = StaticCell::new();
    let executor = EXECUTOR.init(Executor::new());
    executor.run(|spawner| {
        spawner.spawn(main_task(spawner, p)).ok();
    });
}

#[embassy_executor::task]
async fn main_task(spawner: embassy_executor::Spawner, p: Peripherals) {
    app::run(spawner, p).await;
}
