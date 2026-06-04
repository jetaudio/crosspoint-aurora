//! Power management — battery latch and shutdown.
//!
//! SAFETY-CRITICAL: GPIO13 drives the battery-latch MOSFET. Driving it LOW (and
//! holding it) cuts power to the MCU entirely — the documented shutdown path
//! (`HalPowerManager.cpp:79-94`). At runtime the firmware must NOT drive GPIO13;
//! it is only touched here, on the way to sleep. The power button (GPIO3, active
//! low) is hard-wired to briefly re-power the MCU, so wake = cold boot.

use esp_hal::gpio::{Level, Output, OutputConfig};
use esp_hal::peripherals::GPIO13;

/// Cut power by driving the battery latch LOW. Does not return: the board loses
/// power within microseconds. The `loop` is only a guard for the brief window
/// before the rails collapse.
pub fn power_off(latch_pin: GPIO13<'static>) -> ! {
    // Drive the latch MOSFET low → hardware removes MCU power.
    let _latch = Output::new(latch_pin, Level::Low, OutputConfig::default());
    loop {
        core::hint::spin_loop();
    }
}
