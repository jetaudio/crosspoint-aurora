//! Power management — battery latch and deep-sleep shutdown.
//!
//! SAFETY-CRITICAL: GPIO13 drives the battery-latch MOSFET. Driving it LOW (and
//! holding it) cuts power to the MCU entirely — the documented shutdown path
//! (`HalPowerManager.cpp:79-94`). At runtime the firmware must NOT drive GPIO13;
//! it is only touched here, on the way to sleep.

use esp_hal::gpio::{Level, Output, OutputConfig};
use esp_hal::peripherals::{GPIO13, LPWR};
use esp_hal::rtc_cntl::Rtc;

/// Shut down: drive the battery latch LOW (hardware removes MCU power), then
/// enter deep sleep as the C++ does (`HalPowerManager.cpp:79-94`).
///
/// On the X4 the latch cuts power outright, so the side power button — which is
/// hard-wired to briefly re-power the MCU — produces a cold-boot wake. That is
/// why no explicit RTC GPIO wake source is armed here: the wake is electrical,
/// not an `esp_deep_sleep` wake trigger (matching the original's comment that
/// "the MCU will be completely powered off during sleep, including RTC"). The
/// `sleep_deep` call mirrors the original's `esp_deep_sleep_start()` and parks
/// the core in the brief window before the rails collapse. Never returns.
pub fn power_off(latch_pin: GPIO13<'static>, lpwr: LPWR<'static>) -> ! {
    // Drive the latch MOSFET low → hardware removes MCU power.
    let _latch = Output::new(latch_pin, Level::Low, OutputConfig::default());

    // Mirror esp_deep_sleep_start(): enter deep sleep with no wake source (the
    // power button re-powers the board electrically → cold boot).
    let mut rtc = Rtc::new(lpwr);
    rtc.sleep_deep(&[])
}
